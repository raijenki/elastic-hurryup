#pragma once
#include <jvmti.h>

void vm_init(JavaVM* vm, jvmtiEnv* jvmti);

void vm_shutdown();

auto vm_jni_env() -> JNIEnv*;

auto vm_jvmti_env() -> jvmtiEnv*;

