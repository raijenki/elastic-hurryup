#pragma once
#include <ctime>

/// Gets the current time in an arbitrary but precise time unit.
///
/// For portability, assume the time is in an arbitrary time unit.
/// Use `to_millis(get_time())` to convert it to a known unit.
///
/// Please also assume that this time may have small shifts between CPU cores.
inline uint64_t get_time()
{
    struct timespec ts = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

/// Converts our arbitrary time unit into milliseconds.
inline uint64_t to_millis(uint64_t time)
{
    return time / 1000000;
}
