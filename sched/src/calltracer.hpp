#pragma once
#include <cstdint>
#include <sys/types.h>
#include <jvmti.h>

struct CallTracerItem
{
    uint64_t timestamp; // from time.hpp:get_time()
    jthread java_thread;// Associated Java Thread
    pid_t thread_id;    // from gettid()
    int cpu_id;         // from sched_getcpu()
    bool is_hotpath;
};

void calltracer_init();

void calltracer_shutdown();

bool calltracer_start(uint32_t sample_time_ms);

void calltracer_stop();

bool calltracer_consume(CallTracerItem& item);

void calltracer_addpush(bool is_hotpath);
