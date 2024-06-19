#pragma once

#include <cassert>
#include <chrono>
#include <unordered_map>

#define colite_assert(...) assert(__VA_ARGS__)

inline class Leak {
public:
    Leak() = default;
    Leak(const Leak&) = delete;
    Leak& operator=(const Leak&) = delete;
    ~Leak() {
        printf("\n-- Test Finish!\n");
        for (auto& [ptr, size] : allocated_memory_) {
            printf("%p: %zu\n", ptr, size);
        }
    }

    void allocate(void* ptr, size_t size) {
        std::lock_guard locker { lock_ };
        allocated_memory_.try_emplace(ptr, size);
    }

    void deallocate(void* ptr) {
        std::lock_guard locker { lock_ };
        allocated_memory_.erase(ptr);
    }
private:
    std::mutex lock_{};
    std::unordered_map<void*, size_t> allocated_memory_ {};
} leak_ {};

namespace colite {
    namespace port {
        using time_duration = std::chrono::steady_clock::duration;
        using time_point = std::chrono::steady_clock::time_point;

        inline auto current_time() -> time_point {
            return std::chrono::steady_clock::now();
        }

        inline void* calloc(size_t n,size_t size) {
            auto ptr = ::calloc(n, size);
            leak_.allocate(ptr, n * size);
            return ptr;
        }

        inline void free(void *ptr) {
            ::free(ptr);
            leak_.deallocate(ptr);
        }
    }
}