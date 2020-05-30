#include "calltracer.hpp"
#include <csignal>
#include <cstring>
#include <sys/time.h>
#include "MPMCQueue.h"
#include <dlfcn.h>
#include <unistd.h>
#include <jvmti.h>
#include "vm.hpp" 
#include "tls.hpp"
#include "time.hpp"

static constexpr size_t max_queue_size = 2000;

static std::atomic<bool> has_started;

static rigtorp::MPMCQueue<CallTracerItem> queue(max_queue_size);

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

void calltracer_signal_handler(void* ucontext)
{
    const TlsData& tls = tls_data();

    const auto current_time = get_time();
    const auto cpu_id = tls.cpu_id;
    const auto thread_id = tls.os_thread_id;
    const auto jthread_id = tls.java_thread_id;
    const bool is_hotpath = tls.hotpath_enters > 0;

    if(!queue.try_push(CallTracerItem { current_time, jthread_id, thread_id, cpu_id, is_hotpath }))
    {
        // use write.2 directly since fprintf is not signal safe.
        const char message[] = "hurryup_jvmti: calltracer queue is full!!!!";
        write(2, message, sizeof(message));
    }
}
