#pragma once

#include <list>
#include <mutex>
#include "colite/port.h"
#include "colite/dispatchers.h"

namespace colite::port {
    class eventloop_dispatcher: public colite::dispatcher {
        using byte_allocator = colite::allocator::allocator<std::byte>;
    public:
        class job {
        public:
            job(
                void *id,
                colite::port::time_duration time,
                colite::callable<void()> callable
            ): id(id),
               ready_time(colite::port::current_time() + time),
               callable(std::move(callable))
            {
            }

            job(
                void *id,
                colite::port::time_duration time,
                colite::callable<void()> callable,
                colite::callable<bool()> predicate
            ): id(id),
               ready_time(colite::port::current_time() + time),
               callable(std::move(callable)),
               predicate(std::move(predicate))
            {
            }

            [[nodiscard]]
            auto ready() const -> bool {
                if (predicate) {
                    return ready_time <= colite::port::current_time() && predicate.value()();
                } else {
                    return ready_time <= colite::port::current_time();
                }
            }

            void operator()() const {
                callable();
            }

            [[nodiscard]]
            auto get_id() const -> void* { return id; }

        private:
            void *id;
            colite::port::time_point ready_time;
            colite::callable<void()> callable;
            std::optional<colite::callable<bool()>> predicate = std::nullopt;
        };

        eventloop_dispatcher() = default;
        ~eventloop_dispatcher() override = default;

        template<typename Coro>
            requires colite::traits::is_suspend<std::remove_cvref_t<Coro>>
        auto run(Coro&& coroutine) {
            bool finished;
            auto&& coro = this->launch(std::forward<Coro>(coroutine));
            while (true) {
                coro.check_and_throw_exception();
                {
                    std::lock_guard locker { lock_ };
                    finished = jobs_.empty();
                }
                if (finished) {
                    break;
                }
                run_once();
            }
            return coro.await_resume();
        }

    private:
        byte_allocator allocator_;
        std::recursive_mutex lock_ {};
        std::list<job, std::allocator_traits<byte_allocator>::rebind_alloc<job>>
            jobs_ { allocator_ };

        void dispatch(
            void *id,
            colite::port::time_duration time,
            colite::callable<void()> callable
        ) override {
            std::lock_guard locker { lock_ };
            jobs_.emplace_back(id, time, std::move(callable));
        }

        void dispatch(
            void *id,
            colite::port::time_duration time,
            colite::callable<void()> callable,
            colite::callable<bool()> predicate
        ) override {
            std::lock_guard locker { lock_ };
            jobs_.emplace_back(id, time, std::move(callable), std::move(predicate));
        }

        void cancel_jobs(void *id) override {
            std::lock_guard locker { lock_ };
            jobs_.remove_if([=] (const job& it) {
                 return id == it.get_id();
            });
        }

        void run_once() {
            std::optional<job> job = std::nullopt;
            {
                std::lock_guard locker { lock_ };
                if (jobs_.empty()) {
                    return;
                }
                if (jobs_.front().ready()) {
                    job = std::move(jobs_.front());
                    jobs_.pop_front();
                } else {
                    jobs_.splice(jobs_.cend(), jobs_, jobs_.cbegin());
                }
            }

            if (job) {
                job.value()();
            }
        }
    };
}