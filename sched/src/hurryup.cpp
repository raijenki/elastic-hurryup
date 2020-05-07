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
#include <cstring>
//#include <fstream>
//#include <sstream>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string>

static std::thread scheduling_thread;
int fd[24];
int changes[24];
std::vector<std::tuple<int, int, uint64_t, uint64_t, int>> es_threads; // tid, cpuid, tstamp, dif
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
	
	// Variables
	int coreNum = 0;
	std::string concat_dir1 = "/sys/devices/system/cpu/cpu";
    	std::string concat_dir2;
	std::string concat_dir3;
	// Open frequency files
	while(coreNum < 24) {
		concat_dir2 = std::to_string(coreNum);
		concat_dir3 = concat_dir1 + concat_dir2 + "/cpufreq/scaling_setspeed";
		fd[coreNum] = open(concat_dir3.c_str(), O_RDWR);
		coreNum += 2;
	}

        while(!should_stop_scheduler.load(std::memory_order_relaxed))
        {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(10ms);
            hurryup_tick();
        }

        pthread_sigmask(SIG_UNBLOCK, &sigprof_mask, nullptr);
    });
}

void hurryup_shutdown()
{
    should_stop_scheduler.store(true);
    scheduling_thread.join();
    int coreNum = 0;
    while(coreNum < 24) {
	close(fd[coreNum]);
	coreNum += 2;
	}

}

void hurryup_freqchange(void) {
	
	for(auto i = 0; i < 23; i=i+2) { 
		if(changes[i] == 0) {
			char freq[8] = "1000000";
			write(fd[i], freq, strlen(freq));
		} else if (changes[i] == 1) {
			char freq[8] = "2600000";
			write(fd[i], freq, strlen(freq));
			}
		}
}

void hurryup_tick() {
    // Please consume the entire queue here otherwise it may get full on the
    // producer side.
    CallTracerItem ct_item;
    while(calltracer_consume(ct_item)) {
	// Check for existence of tid
	auto it = std::find_if(es_threads.begin(), es_threads.end(), [ = ](const std::tuple<int,int,uint64_t, uint64_t, int>& e) {
			return std::get<0>(e) == ct_item.thread_id; });

	// It exists
	if (it != es_threads.end()) {
		// On the same processor, do the processing
			// Check if hotpath is zero
			if(ct_item.is_hotpath == 0) {
				std::get<1>(*it) = ct_item.cpu_id;
			// Don't even bother, change freq to 1.0GHz and update tuple
				changes[ct_item.cpu_id] = 0;
				//std::cout << "Freq change to 1.0ghz: core " << std::get<1>(*it) << std::endl;
				std::get<2>(*it) = ct_item.timestamp;
				std::get<3>(*it) = 0;
				std::get<4>(*it) = 0;
			}
			// It IS on hotpath
			else {	
				// First time?
				if(std::get<4>(*it) == 0) {
					std::get<3>(*it) = 0;
					std::get<4>(*it) = 1;
					std::get<2>(*it) = ct_item.timestamp;
				}

				// Update dif and timestamps
				std::get<1>(*it) = ct_item.cpu_id;
				std::get<3>(*it) += (ct_item.timestamp - std::get<2>(*it));
				std::get<2>(*it) = ct_item.timestamp;
				if(std::get<3>(*it) > 350000000) {
					std::cout << "freq change!" << std::endl;
					changes[ct_item.cpu_id] = 1;
				}

			}
	}

	// Thread doesn't exist in vector of tuples
	else { 
		//Create new tuple into vector
	        es_threads.push_back(std::make_tuple(ct_item.thread_id, ct_item.cpu_id, ct_item.timestamp, 0, 0));
		//Set coreid to 1.0 GHz by default
		changes[ct_item.cpu_id] = 0;
	}

      /*fprintf(stderr, "hurryup_jvmti: timestamp=%lu tid=%d cpu=%d is_hotpath=%d\n",
              ct_item.timestamp, ct_item.thread_id, ct_item.cpu_id,
              ct_item.is_hotpath);*/
    }
    hurryup_freqchange();
}
