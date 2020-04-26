#include "hurryup.hpp"
#include "calltracer.hpp"
#include <csignal>
#include <thread>
#include <atomic>
#include <chrono>

static std::thread scheduling_thread;

static std::atomic<bool> should_stop_scheduler;

static void hurryup_tick();

void hurryup_init()
{
    should_stop_scheduler.store(false);
    scheduling_thread = std::thread([] {
        // Avoid sampling this thread.
        sigset_t sigprof_mask;
        sigemptyset(&sigprof_mask);
        sigaddset(&sigprof_mask, SIGPROF);

        pthread_sigmask(SIG_BLOCK, &sigprof_mask, nullptr);

        while(!should_stop_scheduler.load(std::memory_order_relaxed))
        {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(100ms);
            hurryup_tick();
        }

        pthread_sigmask(SIG_UNBLOCK, &sigprof_mask, nullptr);
    });
}

void hurryup_shutdown()
{
    should_stop_scheduler.store(true);
    scheduling_thread.join();
}

void hurryup_tick()
{
    // Please consume the entire queue here otherwise it may get full on the
    // producer side.

    CallTracerItem ct_item;
    while(calltracer_consume(ct_item))
    {
      fprintf(stderr, "hurryup_jvmti: timestamp=%lu tid=%d cpu=%d is_hotpath=%d\n",
              ct_item.timestamp, ct_item.thread_id, ct_item.cpu_id,
              ct_item.is_hotpath);
    }
}
