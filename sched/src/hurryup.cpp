#include "hurryup.hpp"
#include "calltracer.hpp"
#include <csignal>
#include <algorithm>
#include <thread>
#include <iostream>
#include <atomic>
#include <chrono>
#include <vector>
#include <tuple>
#include <fstream>
#include <sstream>

static std::thread scheduling_thread;
std::vector<std::tuple<int,int,int,int>> es_threads; // tid, cpuid, tstamp, dif
static std::atomic<bool> should_stop_scheduler;

static void hurryup_tick();

void hurryup_init()
{
    should_stop_scheduler.store(false);
    scheduling_thread = std::thread([] {
        // Avoid sampling this thread.
        sigset_t sigprof_mask;
        sigemptyset(&sigprof_mask);
        sigaddset(&sigprof_mask, SIGPROF);

        pthread_sigmask(SIG_BLOCK, &sigprof_mask, nullptr);

        while(!should_stop_scheduler.load(std::memory_order_relaxed))
        {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(100ms);
            hurryup_tick();
        }

        pthread_sigmask(SIG_UNBLOCK, &sigprof_mask, nullptr);
    });
}

void hurryup_shutdown()
{
    should_stop_scheduler.store(true);
    scheduling_thread.join();
}

void hurryup_freqchange(int coreid, int hotcalls) {
	std::stringstream dirstream;
	dirstream << "/sys/devices/system/cpu/cpu" << std::to_string(coreid) << "/cpufreq/scaling_setspeed";
	std::string dir = dirstream.str();

	std::ofstream ofs(dir, std::ofstream::trunc);

	if(hotcalls == 1) {
		ofs << "2600000";
	} else {
		ofs << "1000000";
	}
	ofs.close();
}

void hurryup_tick()
{
    // Please consume the entire queue here otherwise it may get full on the
    // producer side.
    CallTracerItem ct_item;
    while(calltracer_consume(ct_item)) {
	// Check for existence of tid
	auto it = std::find_if(es_threads.begin(), es_threads.end(), [ = ](const std::tuple<int,int,int,int>& e) {
			return std::get<0>(e) == ct_item.thread_id; });

	// It exists
	if (it != es_threads.end()) {
		// Check if the thread is running on another processor from the previous one
		if(ct_item.cpu_id != std::get<1>(*it)) {
				// Set previous processor as 1.0GHz frequency
				hurryup_freqchange(std::get<1>(*it), 0);
				// Update tuple
				std::get<1>(*it) = ct_item.cpu_id;
				// It changed processors so we put 'dif' as zero and update timestamp
				std::get<2>(*it) = ct_item.timestamp;
				std::get<3>(*it) = 0;
		}
		// On the same processor, do the processing
		else {
			// Check if hotpath is zero
			if(ct_item.is_hotpath == 0) {
			// Don't even bother, change freq to 1.0GHz and update tuple
				hurryup_freqchange(ct_item.cpu_id, 0);
				//std::cout << "Freq change to 1.0ghz: core " << ct_item.cpu_id << std::endl;
				std::get<2>(*it) = ct_item.timestamp;
				std::get<3>(*it) = 0;
			}
			// It IS on hotpath
			else {
				// Update dif and timestamp
				std::get<3>(*it) = std::get<3>(*it) + (ct_item.timestamp - std::get<2>(*it));
				std::get<2>(*it) = ct_item.timestamp;

				// If dif is bigger than 250ms or 250000us, then change freq to 2.6GHz
				if(std::get<3>(*it) > 500000) {
					//std::cout << "Freq change to 2.6ghz: core " << ct_item.cpu_id << std::endl;
					hurryup_freqchange(ct_item.cpu_id, 1);
				}

			}
		}
	}

	// Thread doesn't exist in vector of tuples
	else { 
		//DEBUG: std::cout << "Not found"<< std::endl;
		//DEBUG: std::cout << ct_item.thread_id << std::endl;
		
		//Create new tuple into vector
	        es_threads.push_back(std::make_tuple(ct_item.thread_id, ct_item.cpu_id, ct_item.timestamp, 0));
		//Set coreid to 1.0 GHz by default
		hurryup_freqchange(ct_item.cpu_id, 0);
	}

      /*fprintf(stderr, "hurryup_jvmti: timestamp=%lu tid=%d cpu=%d is_hotpath=%d, i=%d",
              ct_item.timestamp, ct_item.thread_id, ct_item.cpu_id,
              ct_item.is_hotpath);*/
    }
}
