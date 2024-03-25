#pragma once

#include <chrono>
#include <type_traits>
#include <list>
#include <mutex>
#include "colite_port.h"
#include "colite_callable.h"
#include "utils/colite_unique_ptr.h"

namespace colite {
    namespace dispatcher_detail {
        class task {
        public:
            [[nodiscard]]
            virtual auto get_id() -> void* = 0;

            [[nodiscard]]
            virtual auto ready() -> bool = 0;

            virtual void operator()() = 0;
        };

        template<typename Clock> requires std::chrono::is_clock_v<Clock>
        class task_impl: public task {
        public:
            template<typename R, typename P, typename Func>
            explicit task_impl(void* id, std::chrono::duration<R, P> delay, Func&& func):
                id(id),
                function(std::forward<Func>(func)),
                predicate{}
            {
                using namespace std::chrono_literals;
                start_time = Clock::now() + delay;
            }

            template<typename R, typename P, typename Func, typename Pred>
            explicit task_impl(void* id, std::chrono::duration<R, P> delay, Func&& func, Pred&& pred = {}):
                id(id),
                function(std::forward<Func>(func)),
                predicate(std::forward<Pred>(pred))
            {
                using namespace std::chrono_literals;
                start_time = Clock::now() + delay;
            }

            [[nodiscard]]
            auto get_id() -> void* override { return id; }

            [[nodiscard]]
            auto ready() -> bool override {
                auto time_ready = Clock::now() > start_time;
                auto pred_ready = true;
                if (predicate) {
                    pred_ready = predicate();
                }
                return time_ready && pred_ready;
            }

            void operator()() override {
                function();
            }

        private:
            void *id;
            Clock::time_point start_time;
            colite::callable<void(), colite::port::allocator> function;
            colite::callable<bool(), colite::port::allocator> predicate;
        };
    }

    namespace dispatchers {
        template<typename Clock, typename Alloc = colite::port::allocator>
        class main {
            using byte_allocator = std::allocator_traits<Alloc>::template rebind_alloc<std::byte>;
            using task_unique_ptr = colite::unique_ptr<colite::dispatcher_detail::task, byte_allocator>;

        public:
            main() = default;
            explicit main(const Alloc& allocator): allocator(allocator) {}

            template<typename R, typename P, typename Func>
            void dispatch(void* id, std::chrono::duration<R, P> delay, Func&& func) {
                std::lock_guard locker(lock);
                auto new_task = colite::make_unique<colite::dispatcher_detail::task_impl<Clock>>(
                    allocator, id, delay, std::forward<Func>(func)
                );
                task_list.emplace_back(std::move(new_task));
            }

            template<typename R, typename P, typename Func, typename Pred>
            void dispatch(void* id, std::chrono::duration<R, P> delay, Func&& func, Pred&& pred) {
                std::lock_guard locker(lock);
                auto new_task = colite::make_unique<colite::dispatcher_detail::task_impl<Clock>>(
                    allocator, id, delay, std::forward<Func>(func), std::forward<Pred>(pred)
                );
                task_list.emplace_back(std::move(new_task));
            }

            void cancel(void* coroutine_handle_ptr) {
                std::lock_guard locker(lock);
                task_list.remove_if([=](const auto& it) {
                    return it->get_id() == coroutine_handle_ptr;
                });
            }

            template<typename Func>
            void set_has_done_callback(Func&& func) {
                has_done = std::forward<Func>(func);
            }

            void run() {
                colite_assert(has_done);
                while (!has_done()) {
                    run_once();
                }
            }

        private:
            byte_allocator allocator{};
            std::mutex lock;
            colite::callable<bool(), byte_allocator> has_done;
            std::list<
                task_unique_ptr,
                typename std::allocator_traits<byte_allocator>::template rebind_alloc<task_unique_ptr>
            > task_list{};

            void run_once() {
                colite::unique_ptr<colite::dispatcher_detail::task, byte_allocator> task { nullptr, [](void*){} };
                {
                    std::lock_guard locker(lock);
                    if (!task_list.empty() && task_list.front()->ready()) {
                        task = std::move(task_list.front());
                        task_list.pop_front();
                    } else {
                        auto front = std::move(task_list.front());
                        task_list.pop_front();
                        task_list.emplace_back(std::move(front));
                    }
                }

                if (task) {
                    task->operator()();
                }
            }
        };

        inline dispatchers::main<std::chrono::high_resolution_clock> Main;
    }
}