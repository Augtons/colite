#pragma once

#include <list>
#include <mutex>
#include "colite/port.h"
#include "colite/dispatchers.h"

namespace colite {
    template<typename Alloc = std::allocator<std::byte>>
    class eventloop_dispatcher: public colite::dispatcher<Alloc> {
        using byte_allocator = typename std::allocator_traits<Alloc>::template rebind_alloc<std::byte>;
    public:
        class job {
        public:
            job(
                void *id,
                colite::port::time_duration time,
                colite::callable<void(), byte_allocator> callable
            ): id(id),
               ready_time(colite::port::current_time() + time),
               callable(std::move(callable))
            {
            }

            job(
                void *id,
                colite::port::time_duration time,
                colite::callable<void(), byte_allocator> callable,
                colite::callable<bool(), byte_allocator> predicate
            ): id(id),
               ready_time(colite::port::current_time() + time),
               callable(std::move(callable)),
               predicate(std::move(predicate))
            {
            }

            auto ready() const -> bool {
                if (predicate) {
                    return ready_time <= colite::port::current_time() && predicate.value()();
                } else {
                    return ready_time <= colite::port::current_time();
                }
            }

            auto operator()() const {
                return callable();
            }

            [[nodiscard]]
            auto get_id() const -> void* { return id; }

        private:
            void *id;
            colite::port::time_point ready_time;
            colite::callable<void(), byte_allocator> callable;
            std::optional<colite::callable<bool(), byte_allocator>> predicate = std::nullopt;
        };

        explicit eventloop_dispatcher(const Alloc& alloc = {}): allocator_(alloc) {

        }

        ~eventloop_dispatcher() override = default;

        template<typename Coro>
            requires colite::traits::is_allocator_like_suspend<std::remove_cvref_t<Coro>, byte_allocator>
        auto run(Coro&& coroutine) {
            bool finished;
            auto&& coro = this->launch(std::forward<Coro>(coroutine));
            auto it = this->coroutines_.find(coro.get_coroutine_handle());
            it->second->awaiter_dispatcher_ = this;
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
        std::list<job, typename std::allocator_traits<byte_allocator>::template rebind_alloc<job>>
            jobs_ { allocator_ };

        void dispatch(
            void *id,
            colite::port::time_duration time,
            colite::callable<void(), byte_allocator> callable
        ) override {
            std::lock_guard locker { lock_ };
            jobs_.emplace_back(id, time, std::move(callable));
        }

        void dispatch(
            void *id,
            colite::port::time_duration time,
            colite::callable<void(), byte_allocator> callable,
            colite::callable<bool(), byte_allocator> predicate
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