#include "vm.hpp"

static JavaVM* vm;

static jvmtiEnv* jvmti;

void vm_init(JavaVM* vm, jvmtiEnv* jvmti)
{
    ::vm = vm;
    ::jvmti = jvmti;
}

void vm_shutdown()
{
    ::vm = nullptr;
    ::jvmti = nullptr;
}

auto vm_jni_env() -> JNIEnv*
{
    // there's one jni env per thread
    // TODO is this actually signal safe?

    JNIEnv* jni_env;
    return vm->GetEnv(reinterpret_cast<void **>(&jni_env), JNI_VERSION_1_6) == 0
               ? jni_env
               : nullptr;
}

auto vm_jvmti_env() -> jvmtiEnv*
{
    return ::jvmti;
}
