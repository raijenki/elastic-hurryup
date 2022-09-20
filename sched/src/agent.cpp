#include <jvmti.h>
#include <jvmtiprof/jvmtiprof.h>
#include <cassert>
#include <cstring>
#include <atomic>
#include "MPMCQueue.h"
#include "time.hpp"
#include "calltracer.hpp"
#include <sys/types.h>
#include <unistd.h>
#include <csignal>
#include "vm.hpp"
#include "tls.hpp"
#include "hurryup.hpp"
#include <sched.h>
using std::memory_order_relaxed;

/// We need to watch for when the VM is alive.
/// 
/// Details on the reason are given on NativeMethodBind below.
/// 
/// Due to multiple events being concurrent with VMInit this flag is atomic.
static std::atomic<bool> is_vm_alive {false};

/// We need to ignore the creation of the signal dispatcher thread, as we are 
/// not interested in it. Thus we store a flag on whether we have seen it.
static std::atomic<bool> has_seen_signal_dispatcher {false};

/// A sigset_t containing only SIGPROF.
static const sigset_t sigprof_mask = [] {
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, SIGPROF);
    return s;
}();

/// The CPU the next thread should be allocated into.
static int next_cpu_id = 0;

static jvmtiProfEnv* jvmtiprof;
static jint hook_id;
constexpr auto interesting_class_name = "org/elasticsearch/search/SearchService";
constexpr auto interesting_method_name = "executeQueryPhase";
constexpr auto interesting_method_signature = "(Lorg/elasticsearch/search/internal/ShardSearchRequest;Lorg/elasticsearch/action/search/SearchTask;)Lorg/elasticsearch/search/SearchPhaseResult;";

void onEnterExecuteQueryPhase(
    jvmtiProfEnv* jvmtiprof_env, jvmtiEnv* jvmti_env,
    JNIEnv* jni_env, jthread thread, jint hook_id)

{
    //puts("ENTER2");
    //fprintf(stderr, "onEnterExecuteQueryPhase %d\n", tls_data().hotpath_enters);
    calltracer_addpush(true);
    //++tls_data().hotpath_enters;
}

void onLeaveExecuteQueryPhase(
	jvmtiProfEnv* jvmtiprof_env, jvmtiEnv* jvmti_env,
	JNIEnv* jni_env, jthread thread, jint hook_id)
{

    //puts("LEAVE2");
    calltracer_addpush(false);
    //--tls_data().hotpath_enters;
    //fprintf(stderr, "onLeaveExecuteQueryPhase %d\n", tls_data().hotpath_enters);
}

/// Called when a Java Thread starts.
static void JNICALL
ThreadStart(jvmtiEnv *jvmti_env,
            JNIEnv* jni_env,
            jthread thread)
{
    // We do not want to count the internal VM threads. These are launched
    // before boot. Note that they do not have a corresponding ThreadEnd,
    // so we do not have to worry about it there.
    if(!is_vm_alive)
    {
        pthread_sigmask(SIG_BLOCK, &sigprof_mask, nullptr);
        return;
    }

    /// Another internal thread that we do not want to count is the signal
    /// dispatcher thread. This one also do not have a corresponding ThreadEnd.
    if(!has_seen_signal_dispatcher)
    {
        jvmtiError err;
        jvmtiThreadInfo info;

        if((err = jvmti_env->GetThreadInfo(thread, &info)))
        {
            fprintf(stderr, "hurryup_jvmti: Failed to GetThreadInfo\n");
            pthread_sigmask(SIG_BLOCK, &sigprof_mask, nullptr);
            return;
        }

        if(info.name && !strcmp(info.name, "Signal Dispatcher"))
        {
            has_seen_signal_dispatcher.store(true);
            pthread_sigmask(SIG_BLOCK, &sigprof_mask, nullptr);
            return;
        }
    }

    tls_data_construct(thread, jni_env);

    {
	TlsData& tls = tls_data();

	jvmtiThreadInfo info;
	jvmti_env->GetThreadInfo(thread, &info);
        //fprintf(stderr, "hurryup_jvmti: started thread (name %s) (tid %d)\n", info.name, gettid());
	if(!info.name || !strstr(info.name, "[search]")) {
	    pthread_sigmask(SIG_BLOCK, &sigprof_mask, nullptr);
	    return;
	}

	const auto this_cpu_id = next_cpu_id;
	next_cpu_id += 2;
	tls.cpu_id = this_cpu_id;

	cpu_set_t cpu_set;
	CPU_ZERO(&cpu_set);
	CPU_SET(this_cpu_id, &cpu_set);
	if(sched_setaffinity(tls.os_thread_id, sizeof(cpu_set), &cpu_set) == -1)
	{
	    fprintf(
		stderr,
		"hurryup_jvmti: Failed to sched_setaffinity thread %d at core %d",
		tls.os_thread_id, this_cpu_id);
        }

	//printf("Created thread %s at core %d\n", info.name, this_cpu_id);

	pthread_sigmask(SIG_UNBLOCK, &sigprof_mask, nullptr);
    }
}

/// Called when a Java Thread ends.
static void JNICALL
ThreadEnd(jvmtiEnv *jvmti_env,
            JNIEnv* jni_env,
            jthread thread)
{
    //fprintf(stderr, "hurryup_jvmti: ending thread %d\n", gettid());

    pthread_sigmask(SIG_BLOCK, &sigprof_mask, nullptr);

    tls_data_destroy();
}


/// Called once the VM is ready.
static void JNICALL
VMInit(jvmtiEnv *jvmti_env,
            JNIEnv* jni_env,
            jthread thread)
{
    hurryup_init();

    //calltracer_start(5);

    is_vm_alive.store(true);
}

/// Called once the VM shutdowns.
static void JNICALL
VMDeath(jvmtiEnv *jvmti_env,
            JNIEnv* jni_env)
{
    //calltracer_stop();
    hurryup_shutdown();
    is_vm_alive.store(false);
}

/// Called by the virtual machine to configure the agent.
JNIEXPORT jint JNICALL 
Agent_OnLoad(JavaVM *vm, char *options, void *reserved)
{
    jvmtiError err;
    jvmtiProfError jvmtiprof_err;

    jvmtiEnv *jvmti;
    if(vm->GetEnv((void **)&jvmti, JVMTI_VERSION_1_0) != JNI_OK)
    {
        fprintf(stderr, "hurryup_jvmti: Failed to get environment\n");
        return 1;
    }

    jvmtiprof_err = jvmtiProf_Create(vm, jvmti, &jvmtiprof, JVMTIPROF_VERSION);
    if(jvmtiprof_err != JVMTIPROF_ERROR_NONE)
    {
        fprintf(stderr, "failed to create JVMTIPROF environment (error %d)\n",
                jvmtiprof_err);
        return 1;
    }

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));

    callbacks.ThreadStart = ThreadStart;
    jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                    JVMTI_EVENT_THREAD_START,
                                    NULL);

    callbacks.ThreadEnd = ThreadEnd;
    jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                    JVMTI_EVENT_THREAD_END,
                                    NULL);

    callbacks.VMInit = VMInit;
    jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                    JVMTI_EVENT_VM_INIT,
                                    NULL);

    callbacks.VMDeath = VMDeath;
    jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                    JVMTI_EVENT_VM_DEATH,
                                    NULL);

    if((err = jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks))))
    {
        fprintf(stderr, "hurryup_jvmti: Failed to set callbacks (%d)\n", err);
        return 1;
    }

    jvmtiProfCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_generate_specific_method_entry_events = true;
    caps.can_generate_specific_method_exit_events = true;
    jvmtiprof_err = jvmtiprof->AddCapabilities(&caps);
    if(jvmtiprof_err != JVMTIPROF_ERROR_NONE)
    {
        fprintf(stderr, "failed to add hooking capability (%d)\n", jvmtiprof_err);
	return 1; // TODO dispose
    }

    jvmtiProfEventCallbacks callbacks2;
    memset(&callbacks2, 0, sizeof(callbacks2));
    callbacks2.SpecificMethodEntry = onEnterExecuteQueryPhase;
    callbacks2.SpecificMethodExit = onLeaveExecuteQueryPhase;

    jvmtiprof_err = jvmtiprof->SetEventNotificationMode(
            JVMTI_ENABLE, JVMTIPROF_EVENT_SPECIFIC_METHOD_ENTRY, NULL);
    if(jvmtiprof_err != JVMTIPROF_ERROR_NONE)
    {
        fprintf(stderr, "failed to enable method entry event notifications (%d)\n", jvmtiprof_err);
	return 1; // TODO dispose
    }

    jvmtiprof_err = jvmtiprof->SetEventNotificationMode(
            JVMTI_ENABLE, JVMTIPROF_EVENT_SPECIFIC_METHOD_EXIT, NULL);
    if(jvmtiprof_err != JVMTIPROF_ERROR_NONE)
    {
        fprintf(stderr, "failed to enable method exit event notifications (%d)\n", jvmtiprof_err);
	return 1; // TODO dispose
    }

    jvmtiprof_err = jvmtiprof->SetEventCallbacks(&callbacks2, sizeof(callbacks2));
    if(jvmtiprof_err != JVMTIPROF_ERROR_NONE)
    {
        fprintf(stderr, "failed to set event callbacks for method hook (%d)\n", jvmtiprof_err);
	return 1; // TODO dispose
    }

    jvmtiprof_err = jvmtiprof->SetMethodEventFlag(
	    interesting_class_name, interesting_method_name, interesting_method_signature,
            (jvmtiProfMethodEventFlag)((int)JVMTIPROF_METHOD_EVENT_ENTRY | (int)JVMTIPROF_METHOD_EVENT_EXIT),
            JVMTI_ENABLE,
            &hook_id);
    if(jvmtiprof_err != JVMTIPROF_ERROR_NONE)
    {
        fprintf(stderr, "failed to set method hook flags (%d)\n", jvmtiprof_err);
	return 1; // TODO dispose
    }

    std::atomic<uint64_t> u64_atomic;
    if(!u64_atomic.is_lock_free())
        fprintf(stderr, "hurryup_jvmti: aligned atomic_uint64_t is not lock free!!!\n");

    tls_init();

    vm_init(vm, jvmti);

    has_seen_signal_dispatcher.store(false);

    //calltracer_init();

    fprintf(stderr, "hurryup_jvmti: Agent has been loaded.\n");
    return 0;
}

/// Called by the virtual machine once the agent is about to unload.
JNIEXPORT void JNICALL 
Agent_OnUnload(JavaVM *vm)
{
    //calltracer_shutdown();
    vm_shutdown();
    tls_shutdown();
    jvmtiprof->DisposeEnvironment();
    fprintf(stderr, "hurryup_jvmti: Agent has been unloaded\n");
}
