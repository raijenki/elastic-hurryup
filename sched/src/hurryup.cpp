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
#include <sys/time.h>

static std::thread scheduling_thread;
int fd[24];
int changes[24];  

struct threadinfo {
	int threadId; // Thread ID 
	int coreId; // Core ID
	uint64_t timestamp; // Most recent timestamp before actual execution
	uint64_t dif; // Hotpath: Is the difference bigger than the threshold?
	int exec; //  0 = not hot path, 1 = hot path first time, 2 = hot path again
};
std::vector<threadinfo> es_threads;

static std::atomic<bool> should_stop_scheduler;
static void hurryup_tick();
// using namespace std;

void hurryup_init() {
    should_stop_scheduler.store(false);
    scheduling_thread = std::thread([] {
        // Avoid sampling this thread.
        sigset_t sigprof_mask;
        sigemptyset(&sigprof_mask);
        sigaddset(&sigprof_mask, SIGPROF);

        pthread_sigmask(SIG_BLOCK, &sigprof_mask, nullptr);
	
	// Variables for file opening
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
            std::this_thread::sleep_for(50ms);
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
	struct timeval  tv;
	
	for(auto i = 0; i < 23; i=i+2) { 
		if(changes[i] == 0) {
			char freq[8] = "1000000";
			//gettimeofday(&tv, NULL);
			//double time_in_mill = (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000 ; 
			//std::cout << "change to 1.0 at " << time_in_mill << std::endl;
			write(fd[i], freq, strlen(freq));
			changes[i] = 2; // Don't change again until necessary
		} else if (changes[i] == 1) {
			char freq[8] = "2600000";
			//gettimeofday(&tv, NULL);
			//double time_in_mill = (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000 ; 
			//printf("%lf\n", time_in_mill);
			//std::cout << "change to 2.6 at " << time_in_mill << std::endl;
			write(fd[i], freq, strlen(freq));
			changes[i] = 2; // Don't change again
			}
		}
}

void hurryup_tick() {
    // Please consume the entire queue here otherwise it may get full on the
    // producer side.
    CallTracerItem ct_item;
    while(calltracer_consume(ct_item)) {

	// An event just arrived. Does it already exists on our vector?    
	auto it = std::find_if(es_threads.begin(), es_threads.end(), [ = ](const threadinfo& e) { 
			return e.threadId == ct_item.thread_id; });
	
	// It exists
	if (it != es_threads.end()) {
      	fprintf(stderr, "hurryup_jvmti: timestamp=%lu tid=%d cpu=%d is_hotpath=%d\n",
              ct_item.timestamp, ct_item.thread_id, ct_item.cpu_id,
              ct_item.is_hotpath);
		// This is to fix the core id after tasksetting
		if(it->coreId != ct_item.cpu_id) {
		       it->coreId = ct_item.cpu_id;	
		}

		// Hot path?
		if(ct_item.is_hotpath == 1) {
			if(it->exec == 2) { // It ALREADY had the frequency changed
				// Let's just update the timestamp and the dif for future purposes
				it->dif += (it->timestamp - ct_item.timestamp);
				it->timestamp = ct_item.timestamp;
			}
			else { // It is on hotpath but did not change the frequency
				it->dif += (ct_item.timestamp - it->timestamp);
				it->timestamp = ct_item.timestamp;
				it->exec = 1;

				// Should we change frequency for this core?
				if(it->dif > 300000000) {
					it->exec = 2;
					changes[ct_item.cpu_id] = 1;
					//std::cout << "freq change 2.6! dif: " << it->dif << "tid: " << it->threadId << std::endl;
				}
			}
		
		}
		// Not on hot path
		else {
			// Was it on hot path at previous execution?
			if(it->exec == 1 || it->exec == 2) {
				// Let's update parameters
				it->timestamp = ct_item.timestamp;
				it->dif = 0;
				it->exec = 0; // This change wont happen again

				// Set it to transition to 1.0 GHz
				changes[ct_item.cpu_id] = 0;
				//std::cout << "freq change 1.0! tid: " << it->threadId << std::endl;

			}

		}
	}

	// It doesn't exists.
	else {
		// Is it a search thread? If it isn't, we don't care
		if(ct_item.is_hotpath == 1) {
			// It is because only search threads access the hotpath and are the ones to
			// change the processor frequency.
			// Lets put it into our vector.
			// Doing it this way for readbility purposes
			threadinfo t;
			t.threadId = ct_item.thread_id;
			t.coreId = ct_item.cpu_id;
			t.timestamp = ct_item.timestamp;
			t.dif = 0;
			t.exec = 1; // The first time it appears it is already on hot path

			es_threads.push_back(t);
			std::cout << "Thread does not exists! Registering " << ct_item.thread_id << std::endl;
		}
	}
	
      //fprintf(stderr, "hurryup_jvmti: timestamp=%lu tid=%d cpu=%d is_hotpath=%d\n",
              //ct_item.timestamp, ct_item.thread_id, ct_item.cpu_id,
              //ct_item.is_hotpath);
    }
    hurryup_freqchange();
}
