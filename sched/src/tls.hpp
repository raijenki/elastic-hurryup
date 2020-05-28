#pragma once
#include <jvmti.h>
#include <sys/types.h>

struct TlsData
{
    /// JNI/JVMTI reference to the executing Java Thread (jthread).
    /// This reference isn't guaranteed to be live after the thread dies, see
    /// (JNI Local References)[https://docs.oracle.com/javase/7/docs/technotes/guides/jni/spec/functions.html#wp18654].
    jthread java_thread_id;
    /// JNI Environment local to the thread.
    JNIEnv* jni_env;
    /// OS thread id as obtained by `gettid()`.
    pid_t os_thread_id;
    /// The CPU allocated to this thread (or `-1` if unknown).
    int cpu_id = -1;
};

bool tls_init();

void tls_shutdown();

bool tls_has_data();

auto tls_data() -> TlsData&;

void tls_data_construct(jthread jthread_id, JNIEnv* jni_env);

void tls_data_destroy();

