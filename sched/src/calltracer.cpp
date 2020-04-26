#include "calltracer.hpp"
#include <csignal>
#include <cstring>
#include <sys/time.h>
#include "MPMCQueue.h"
#include <dlfcn.h>
#include <unistd.h>
#include <jvmti.h>
#include "vm.hpp" 
#include "time.hpp"

struct JVMPI_CallFrame
{
    jint lineno;
    jmethodID method_id;
};

struct JVMPI_CallTrace 
{
    JNIEnv *env_id;
    jint num_frames;
    JVMPI_CallFrame *frames;
};

enum
{
    ticks_no_Java_frame = 0,
    ticks_no_class_load = -1,
    ticks_GC_active = -2,
    ticks_unknown_not_Java = -3,
    ticks_not_walkable_not_Java = -4,
    ticks_unknown_Java = -5,
    ticks_not_walkable_Java = -6,
    ticks_unknown_state = -7,
    ticks_thread_exit = -8,
    ticks_deopt = -9,
    ticks_safepoint = -10
};

static constexpr size_t max_queue_size = 2000;

static constexpr size_t max_frames = 300;

static std::atomic<bool> has_started;

static constexpr jint max_hotpath_methods = 4;
static jmethodID hotpath_methods[max_hotpath_methods] = {};
static jint num_hotpath_methods = 0;

static rigtorp::MPMCQueue<CallTracerItem> queue(max_queue_size);

static void (*AsyncGetCallTrace)(JVMPI_CallTrace *trace, jint depth, void* ucontext);


static void calltracer_signal_handler(void* ucontext);

static void sigprof_action(int signum, siginfo_t*, void* ucontext)
{
    const auto old_errno = errno;
    calltracer_signal_handler(ucontext);
    errno = old_errno;
}

void calltracer_init()
{
    has_started.store(false);

    AsyncGetCallTrace = reinterpret_cast<decltype(AsyncGetCallTrace)>(
        dlsym(RTLD_DEFAULT, "AsyncGetCallTrace"));

    if(!AsyncGetCallTrace)
    {
        fprintf(stderr, "hurryup_jvmti: failed to find symbol for AsyncGetCallTrace\n");
        std::abort();
    }

    // Clear the queue if not empty.
    CallTracerItem item;
    while(queue.try_pop(item)) { /* loop */ }
}

void calltracer_shutdown()
{
    if(has_started.load())
        calltracer_stop();

    // Clear the queue if not empty.
    CallTracerItem item;
    while(queue.try_pop(item)) { /* loop */ }
}

bool calltracer_start(uint32_t sample_time_ms)
{
    assert(!has_started.load());

    struct sigaction action, old_action;

    std::memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = sigprof_action;
    action.sa_flags = SA_RESTART | SA_SIGINFO;
    if(sigaction(SIGPROF, &action, &old_action) < 0)
        return false;

    struct itimerval timeout;
    std::memset(&timeout, 0, sizeof(timeout));
    timeout.it_interval.tv_sec = sample_time_ms / 1000;
    timeout.it_interval.tv_usec = (sample_time_ms * 1000) % 1000000;
    timeout.it_value = timeout.it_interval;
    if(setitimer(ITIMER_PROF, &timeout, nullptr) < 0)
    {
        sigaction(SIGPROF, &old_action, nullptr);
        return false;
    }

    has_started.store(true);
    return true;
}

void calltracer_stop()
{
    assert(has_started.load());

    struct itimerval timeout;
    std::memset(&timeout, 0, sizeof(timeout));
    setitimer(ITIMER_PROF, &timeout, nullptr);

    signal(SIGPROF, SIG_DFL);

    has_started.store(false);
}

bool calltracer_consume(CallTracerItem& item)
{
    return queue.try_pop(item);
}

void calltracer_onmethodsload(jclass klass, jint method_count, jmethodID* methods)
{
    jvmtiEnv* jvmti = vm_jvmti_env();
    char *class_signature, *method_name;

    if(jvmti->GetClassSignature(klass, &class_signature, nullptr) == 0)
    {
        if(!strcmp(class_signature, "Lorg/elasticsearch/search/SearchService;"))
        {
            for(jint i = 0; i < method_count; ++i)
            {
                if(jvmti->GetMethodName(methods[i], &method_name, nullptr, nullptr) == 0)
                {
                    if(!strcmp(method_name, "executeQueryPhase"))
                    {
                      fprintf(stderr, "hurryup_jvmti: found method id for %s::%s (%p::%p)\n",
                              class_signature, method_name, (void*)klass, (void*)methods[i]);

		      assert(::num_hotpath_methods < ::max_hotpath_methods);
                      ::hotpath_methods[::num_hotpath_methods++] = methods[i];
                    }
                    jvmti->Deallocate(reinterpret_cast<unsigned char*>(method_name));
                }
            }
        }
        jvmti->Deallocate(reinterpret_cast<unsigned char*>(class_signature));
    }
}

void calltracer_signal_handler(void* ucontext)
{
    JNIEnv* jni_env = vm_jni_env();
    if(!jni_env)
        return; // not a java thread

    jvmtiEnv* jvmti = vm_jvmti_env();
    assert(jvmti != nullptr);

    JVMPI_CallFrame frames[max_frames];

    JVMPI_CallTrace trace;
    trace.env_id = jni_env;
    trace.frames = frames;

    AsyncGetCallTrace(&trace, max_frames, ucontext);
    if(trace.num_frames <= 0)
    {
        // some kind of error (see ticks_ enum)
        return;
    }
    
    // TODO explain why these calls are (or aren't) signal safe
    const auto current_time = get_time();
    const auto thread_id = gettid();
    const auto cpu_id = sched_getcpu();
    bool is_hotpath = false;

    for(jint i = 0; i < trace.num_frames && !is_hotpath; ++i)
    {
        for(jint j = 0; j < num_hotpath_methods; ++j)
	{
	    if(frames[i].method_id == hotpath_methods[j])
	    {
	        is_hotpath = true;
	        break;
	    }
	}
    }

    if(!queue.try_push(CallTracerItem { current_time, thread_id, cpu_id, is_hotpath }))
    {
        // use write.2 directly since fprintf is not signal safe.
        const char message[] = "hurryup_jvmti: calltracer queue is full!!!!";
        write(2, message, sizeof(message));
    }
}
