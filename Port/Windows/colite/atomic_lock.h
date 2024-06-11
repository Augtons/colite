#pragma once

#include <atomic>

namespace colite::port {
    class atomic_lock {
    public:
        atomic_lock() = default;

        atomic_lock(const atomic_lock&) = delete;
        atomic_lock& operator=(const atomic_lock&) = delete;

        void lock() {
            while (locked_.test_and_set(std::memory_order_acquire)) {
                printf("Pending\n");
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        void unlock() {
            locked_.clear(std::memory_order_release);
        }
    private:
        std::atomic_flag locked_ = ATOMIC_FLAG_INIT;
    };
}
