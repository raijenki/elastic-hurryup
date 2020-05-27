#include <jvmti.h>
#include <cassert>
#include <cstring>
#include <atomic>
#include "time.hpp"
#include "calltracer.hpp"
#include <sys/types.h>
#include <unistd.h>
#include <csignal>
#include "vm.hpp"
#include "tls.hpp"
#include "hurryup.hpp"
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


/// Ensures all methods of a class have a jmethodID.
static void load_method_ids(jvmtiEnv *jvmti, jclass klass)
{
    jint method_count;
    jmethodID* methods = nullptr;
    jvmtiError err;
    err = jvmti->GetClassMethods(klass, &method_count, &methods);
    if(err == JVMTI_ERROR_NONE)
    {
        calltracer_onmethodsload(klass, method_count, methods);
        jvmti->Deallocate(reinterpret_cast<unsigned char*>(methods));
    }
    else if(err == JVMTI_ERROR_CLASS_NOT_PREPARED)
    {
        // Some classes mayn't be prepared during VMInit, so we ignore this error.
    }
    else
    {
        fprintf(stderr, "hurryup_jvmti: load_method_ids failed (%d)\n", err);
    }
}

/// Ensures the methods of all loaded classes have a jmethodID.
static void load_all_method_ids(jvmtiEnv *jvmti)
{
    jint class_count;
    jclass* classes;
    if(jvmti->GetLoadedClasses(&class_count, &classes) == 0)
    {
        for(jint i = 0; i < class_count; ++i)
            load_method_ids(jvmti, classes[i]);

        jvmti->Deallocate(reinterpret_cast<unsigned char*>(classes));
    }
    else
    {
        fprintf(stderr, "hurryup_jvmti: load_all_method_ids failed\n");
    }
}

/// Called when a class file loads.
///
/// Necessary for AsyncGetCallTrace to work.
static void JNICALL
OnClassLoad(jvmtiEnv *jvmti_env,
        JNIEnv *jni_env,
        jthread thread,
        jclass klass)
{
}

/// Called when a class preparation is complete.
static void JNICALL
OnClassPrepare(jvmtiEnv *jvmti_env,
            JNIEnv* jni_env,
            jthread thread,
            jclass klass)
{
    // Make sure method ids are initialized for AsyncGetCallTrace.
    load_method_ids(jvmti_env, klass);
}

/// Called when a method is compiled by the JIT.
///
/// Needed to enable DebugNonSafepoints.
static void JNICALL
OnCompiledMethodLoad(jvmtiEnv *jvmti_env, jmethodID method,
                     jint code_size, const void *code_addr,
                     jint map_length,
                     const jvmtiAddrLocationMap *map,
                     const void *compile_info)
{
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

    //fprintf(stderr, "hurryup_jvmti: started thread %d\n", gettid());
    tls_data_construct(thread, jni_env);

    jvmtiThreadInfo info;
    jvmti_env->GetThreadInfo(thread, &info);
    if(info.name && !strstr(info.name, "[search]")) {
	pthread_sigmask(SIG_BLOCK, &sigprof_mask, nullptr);
	return;
}
    printf("Thread name is %s\n", info.name);
    
    pthread_sigmask(SIG_UNBLOCK, &sigprof_mask, nullptr);

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
    // Ensure the method ids for classes that were already loaded
    // are available for AsyncGetCallTrace.
    load_all_method_ids(jvmti_env);

    hurryup_init();

    calltracer_start(10);

    is_vm_alive.store(true);
}

/// Called once the VM shutdowns.
static void JNICALL
VMDeath(jvmtiEnv *jvmti_env,
            JNIEnv* jni_env)
{
    calltracer_stop();
    hurryup_shutdown();
    is_vm_alive.store(false);
}

/// Called by the virtual machine to configure the agent.
JNIEXPORT jint JNICALL 
Agent_OnLoad(JavaVM *vm, char *options, void *reserved)
{
    jvmtiError err;

    jvmtiEnv *jvmti;
    if(vm->GetEnv((void **)&jvmti, JVMTI_VERSION_1_0) != JNI_OK)
    {
        fprintf(stderr, "hurryup_jvmti: Failed to get environment\n");
        return 1;
    }

    jvmtiCapabilities caps;
    memset(&caps, 0, sizeof(caps));
    caps.can_generate_compiled_method_load_events = true;
    if((err = jvmti->AddCapabilities(&caps)))
    {
        fprintf(stderr, "hurryup_jvmti: Failed to add capabilities (%d)\n", err);
        return 1;
    }

    jvmtiEventCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));

    callbacks.ClassLoad = OnClassLoad;
    jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                    JVMTI_EVENT_CLASS_LOAD,
                                    NULL);

    callbacks.ClassPrepare = OnClassPrepare;
    jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                    JVMTI_EVENT_CLASS_PREPARE,
                                    NULL);

    callbacks.CompiledMethodLoad = OnCompiledMethodLoad;
    jvmti->SetEventNotificationMode(JVMTI_ENABLE,
                                    JVMTI_EVENT_COMPILED_METHOD_LOAD,
                                    NULL);

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

    std::atomic<uint64_t> u64_atomic;
    if(!u64_atomic.is_lock_free())
        fprintf(stderr, "hurryup_jvmti: aligned atomic_uint64_t is not lock free!!!\n");

    tls_init();

    vm_init(vm, jvmti);

    has_seen_signal_dispatcher.store(false);

    calltracer_init();

    fprintf(stderr, "hurryup_jvmti: Agent has been loaded.\n");
    return 0;
}

/// Called by the virtual machine once the agent is about to unload.
JNIEXPORT void JNICALL 
Agent_OnUnload(JavaVM *vm)
{
    calltracer_shutdown();
    vm_shutdown();
    tls_shutdown();
    fprintf(stderr, "hurryup_jvmti: Agent has been unloaded\n");
}
