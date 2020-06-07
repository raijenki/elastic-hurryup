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
int changes[24][2];  

struct threadinfo {
	int threadId; // Thread ID 
	jthread jthreadId; // Java Thread ID
	int coreId; // Core ID
	uint64_t timestamp; // Most recent timestamp before actual execution
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
	printf("AGENT TID IS %d\n", gettid());
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
		changes[coreNum][0] = 0; 
		changes[coreNum][1] = 0;
		coreNum += 2;
	}

        while(!should_stop_scheduler.load(std::memory_order_relaxed))
        {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(10ms);
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
	//struct timeval  tv;
	for(auto i = 0; i < 23; i=i+2) {
		// This will ensure that frequency will change only once
		if(changes[i][0] != changes[i][1]) {
			if(changes[i][0] == 0) {
				changes[i][1] = changes[i][0];
				char freq[8] = "1000000";
				write(fd[i], freq, strlen(freq));
			}  else if (changes[i][0] == 4) {
				changes[i][1] = changes[i][0];
				char freq[8] = "2000000";
				write(fd[i], freq, strlen(freq));			
			}  else if (changes[i][0] == 5) {
				changes[i][1] = changes[i][0];
				char freq[8] = "2601000";
				write(fd[i], freq, strlen(freq));
			} 
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
	if(changes[es_thread.coreId][0] != 5) // ignore threads not in 2.6Ghz
	    continue;

	jint thread_state, err;
	if((err = jvmti_env->GetThreadState(es_thread.jthreadId, &thread_state)) != 0)
	{
	    fprintf(stderr, "hurryup_jvmti: failed to GetThreadState (error code %d)\n", err);
	    continue;
	}

	if(thread_state & JVMTI_THREAD_STATE_WAITING)
	{	
	    std::cout << "down to 1.0 for wait - coreId: " << es_thread.coreId << std::endl;
	    es_thread.exec = 0;
	    changes[es_thread.coreId][0] = 0;
	}
	if(thread_state & JVMTI_THREAD_STATE_BLOCKED_ON_MONITOR_ENTER) {
		es_thread.exec = 0;
		changes[es_thread.coreId][0] = 0;
	}
    }
}

void hurryup_tick() {
    // Please consume the entire queue here otherwise it may get full on the
    // producer side.
    CallTracerItem ct_item;
    while(calltracer_consume(ct_item)) {

	// An event just arrived. Does its threadid already exists on our vector?    
	auto it = std::find_if(es_threads.begin(), es_threads.end(), [ = ](const threadinfo& e) { 
			return e.threadId == ct_item.thread_id; });
	
	// It exists
	if (it != es_threads.end()) {
		// Is the event at hot path? (Entry event)
		if(ct_item.is_hotpath == 1) {
			it->exec = 1; // Set the thread as hot pat
			it->timestamp = ct_item.timestamp; // It's a new entry event, this is the comparison time
		}
		// The event is not on hot path (Leave)
		else {
		       it->exec = 0; // Set the thread as not in hot path. We don't care about the other attributes.	
		}
	}
	// The event  doesn't exists in our structure.
	else {
		// This line is redundant, as the first time a search thread will appear is when its on the hot path
		if(ct_item.is_hotpath == 1) {
			// Lets put it into our vector.
			// Doing it this way for readbility purposes
			threadinfo t;
			t.threadId = ct_item.thread_id;
			t.jthreadId = ct_item.java_thread;
			t.coreId = ct_item.cpu_id;
			t.timestamp = ct_item.timestamp;
			t.exec = 1;
			es_threads.push_back(t);
			//std::cout << "Thread does not exists! Registering " << ct_item.thread_id << std::endl;
		}
	}
	
      //fprintf(stderr, "hurryup_jvmti: timestamp=%lu tid=%d cpu=%d is_hotpath=%d\n",
              //ct_item.timestamp, ct_item.thread_id, ct_item.cpu_id,
              //ct_item.is_hotpath);
    }
	
    // We either consumed all the events or there aren't events to be consumed. Iterate over out structure.
    // Iterate on the vector; Get time only once.
    uint64_t actual_time = get_time();
    for (auto& es_thread : es_threads) {
	    // It entered at hot function but didn't change frequency yet
	    if(es_thread.exec == 1) {
		    changes[es_thread.coreId][0] = 4;
		    // Check if dif is higher than 350 ms
		    if(actual_time - es_thread.timestamp >= 300000000) {
		    	    //std::cout << "Hot function event " << es_thread.coreId << std::endl;
			    es_thread.exec = 2; // So we don't have to change frequency again
			    changes[es_thread.coreId][0] = 5;
		    }
	    }
	    // Either not on hot function or left
	    if(es_thread.exec == 0) {
		    //std::cout << "Leave event: " << es_thread.coreId << std::endl;
		    es_thread.exec = -1;
		    changes[es_thread.coreId][0] = 0;
	    }
	}

    //hurryup_restore_waiting_threads(); // This is for threads that might not produce a leave event
    hurryup_freqchange();
}
