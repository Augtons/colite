#pragma once

#include <cassert>
#include <chrono>

#define colite_assert(...) assert(__VA_ARGS__)

namespace colite {
    namespace port {
        using time_duration = std::chrono::steady_clock::duration;
        using time_point = std::chrono::steady_clock::time_point;

        inline auto current_time() -> time_point {
            return std::chrono::steady_clock::now();
        }
    }
}