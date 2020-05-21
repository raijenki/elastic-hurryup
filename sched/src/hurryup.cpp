#include "hurryup.hpp"
#include "calltracer.hpp"
#include "time.hpp"
#include "vm.hpp"
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
#include <jvmti.h>

//static std::thread scheduling_thread;
int fd[24];
int changes[24];  
int current_freq[24];

struct threadinfo {
	int threadId; // Thread ID 
	jthread jthreadId; // Java Thread ID
	int coreId; // Core ID
	uint64_t timestamp; // Most recent timestamp before actual execution
	uint64_t dif; // Hotpath: Is the difference bigger than the threshold?
	int exec; //  0 = not hot path, 1 = hot path first time, 2 = hot path again
};
std::vector<threadinfo> es_threads;

static std::atomic<bool> should_stop_scheduler;
static void hurryup_tick();
// using namespace std;

static jthread alloc_thread()
{
    auto env = vm_jni_env();
    if(!env)
    {
	fprintf(stderr, "hurryup_jvmti: Failed to alloc_thread because vm_jni_env failed.\n");
	return nullptr;
    }

    auto thread_class = env->FindClass("java/lang/Thread");
    if(!thread_class)
    {
	fprintf(stderr, "hurryup_jvmti: cannot find java/lang/Thread class\n");
	return nullptr;
    }

    auto init_method_id = env->GetMethodID(thread_class, "<init>", "()V");
    if(!init_method_id)
    {
	fprintf(stderr, "hurryup_jvmti: cannot find java/lang/Thread constructor\n");
	return nullptr;
    }

    auto result = env->NewObject(thread_class, init_method_id);
    if(!result)
    {
	fprintf(stderr, "hurryup_jvmti: cannot create new java/lang/Thread object\n");
	return nullptr;
    }

    return result;
}

void hurryup_init() {
    should_stop_scheduler.store(false);

    auto err = vm_jvmti_env()->RunAgentThread(alloc_thread(), +[](jvmtiEnv*, JNIEnv*, void*) {

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
		current_freq[coreNum] = -1; // unknown frequency
		coreNum += 2;
	}

        while(!should_stop_scheduler.load(std::memory_order_relaxed))
        {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(20ms);
            hurryup_tick();
        }

        pthread_sigmask(SIG_UNBLOCK, &sigprof_mask, nullptr);

    }, nullptr, JVMTI_THREAD_MAX_PRIORITY);

    if(err)
    {
	fprintf(stderr, "hurryup_jvmti: failed to RunAgentThread\n");
	std::abort();
    }
}

void hurryup_shutdown()
{
    should_stop_scheduler.store(true);
    //scheduling_thread.join();
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
			changes[i] = -1; // Don't change again until necessary
			current_freq[i] = 0;
		/*} else if (changes[i] == 1) {
			char freq[8] = "1300000";
			write(fd[i], freq, strlen(freq));
			changes[i] = -1; // Don't change again
			current_freq[i] = 1;
		}  else if (changes[i] == 2) {
			char freq[8] = "1700000";
			write(fd[i], freq, strlen(freq));
			changes[i] = -1; // Don't change again
			current_freq[i] = 1;
		} else if (changes[i] == 3) {
			char freq[8] = "2000000";
			write(fd[i], freq, strlen(freq));
			changes[i] = -1; // Don't change again
			current_freq[i] = 1;
		}  else if (changes[i] == 4) {
			char freq[8] = "2300000";
			write(fd[i], freq, strlen(freq));
			changes[i] = -1; // Don't change again
			current_freq[i] = 1;*/
		}  else if (changes[i] == 5) {
			char freq[8] = "2601000";
			write(fd[i], freq, strlen(freq));
			changes[i] = -1; // Don't change again
			current_freq[i] = 1;
			} 
		}

}

void hurryup_restore_waiting_threads(void)
{
    jvmtiEnv* jvmti_env = vm_jvmti_env();
    //const auto current_time = get_time();

    // Waiting threads do not produce events, as such we must identify
    // and return these threads to the 1.0GHz frequency.
    for(auto& es_thread : es_threads)
    {
	if(current_freq[es_thread.coreId] != 1) // ignore threads not in 2.6Ghz
	    continue;

	jint thread_state, err;
	if((err = jvmti_env->GetThreadState(es_thread.jthreadId, &thread_state)) != 0)
	{
	    fprintf(stderr, "hurryup_jvmti: failed to GetThreadState (error code %d)\n", err);
	    continue;
	}

	if(thread_state & JVMTI_THREAD_STATE_WAITING)
	{	
	    //std::cout << "down to 1.0 " << es_thread.threadId << std::endl;
	    changes[es_thread.coreId] = 0;
	}
	if(thread_state & JVMTI_THREAD_STATE_BLOCKED_ON_MONITOR_ENTER) {
		changes[es_thread.coreId] = 0;
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
		// This is to fix the core id after tasksetting
		if(it->coreId != ct_item.cpu_id) {
		       it->coreId = ct_item.cpu_id;	
		}

		// Hot path?
		if(ct_item.is_hotpath == 1) {
			if(it->exec == 2) { // It ALREADY had the frequency changed to the max
				// Let's just update the timestamp and the dif for future purposes
				it->dif += (it->timestamp - ct_item.timestamp);
				it->timestamp = ct_item.timestamp;
			}
			else { // It is on hotpath but did not change the frequency
				it->dif += (ct_item.timestamp - it->timestamp);
				it->timestamp = ct_item.timestamp;
				it->exec = 1;
				// Should we change frequency for this core?
				if(it->dif > 350000000) {
					it->exec = 2;
					changes[ct_item.cpu_id] = 5;
				}/*
				else if(it->dif > 200000000) { 
					it->exec = 1;
					changes[ct_item.cpu_id] = 4;
				}
				else if(it->dif > 150000000) { 
					it->exec = 1;
					changes[ct_item.cpu_id] = 3;
				}
				else if(it->dif > 100000000) { 
					it->exec = 1;
					changes[ct_item.cpu_id] = 2;
				}
				else if(it->dif > 5000000) { 
					it->exec = 1;
					changes[ct_item.cpu_id] = 1;
				}*/

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
			t.jthreadId = ct_item.java_thread;
			t.coreId = ct_item.cpu_id;
			t.timestamp = ct_item.timestamp;
			t.dif = 0;
			t.exec = 1; // The first time it appears it is already on hot path

			es_threads.push_back(t);
			//std::cout << "Thread does not exists! Registering " << ct_item.thread_id << std::endl;
		}
	}
	
      //fprintf(stderr, "hurryup_jvmti: timestamp=%lu tid=%d cpu=%d is_hotpath=%d\n",
              //ct_item.timestamp, ct_item.thread_id, ct_item.cpu_id,
              //ct_item.is_hotpath);
    }

    hurryup_restore_waiting_threads();
    hurryup_freqchange();
}
