#pragma once

#include <atomic>

namespace colite::port {
    class spin_lock {
    public:
        spin_lock() = default;

        spin_lock(const spin_lock&) = delete;
        spin_lock& operator=(const spin_lock&) = delete;

        void lock() {
            while (locked_.test_and_set(std::memory_order_acquire)) {
                ;
            }
        }

        void unlock() {
            locked_.clear(std::memory_order_release);
        }
    private:
        std::atomic_flag locked_ = ATOMIC_FLAG_INIT;
    };
}
