#include "tls.hpp"
#include <unistd.h>
#include <pthread.h>
#include <cassert>
#include <jni.h>

static pthread_key_t tls_key;

bool tls_init()
{
    if(pthread_key_create(&tls_key, nullptr) != 0)
    {
        fprintf(stderr, "hurryup_jvmti: failed to pthread_key_create\n");
        return false;
    }
    return true;
}

void tls_shutdown()
{
    pthread_key_delete(tls_key);
}

bool tls_has_data()
{
    return pthread_getspecific(tls_key) != nullptr;
}

auto tls_data() -> TlsData&
{
    assert(tls_has_data());
    return *static_cast<TlsData*>(pthread_getspecific(tls_key));
}

void tls_data_construct(jthread jthread_id, JNIEnv* jni_env)
{
    assert(!tls_has_data());

    auto strong_jthread_id = jni_env->NewGlobalRef(jthread_id);
    pthread_setspecific(tls_key, new TlsData{
            strong_jthread_id,
            jni_env,
            gettid()
    });
    assert(tls_has_data());
}

void tls_data_destroy()
{
    assert(tls_has_data());

    void* p = pthread_getspecific(tls_key);
    pthread_setspecific(tls_key, nullptr);

    auto* tls = static_cast<TlsData*>(p);
    //tls->jni_env->DeleteGlobalRef(tls->java_thread_id); -- hack for hurryup
    delete tls;

    assert(!tls_has_data());
}
