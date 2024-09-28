#pragma once

#include <windows.h>
#include "threadpoolapiset.h"
#include "colite/port.h"
#include "colite/dispatchers.h"

namespace colite::port {
    class threadpool_dispatcher: public colite::dispatcher {
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

            [[nodiscard]]
            auto get_id() const -> void* { return id; }

            [[nodiscard]]
            auto get_callable() const& -> colite::callable<void()> { return callable; }
            auto get_callable() & -> colite::callable<void()> { return callable; }
            auto get_callable() && -> colite::callable<void()> { return std::move(callable); }

        private:
            void *id;
            colite::port::time_point ready_time;
            colite::callable<void()> callable;
            std::optional<colite::callable<bool()>> predicate = std::nullopt;
        };

        struct job_task_args;

        class threadpool_job {
        public:
            enum class Type { None, Work, Timer };
            explicit threadpool_job(void* id, job_task_args* args, PTP_WORK work):
                id_(id), args_(args), type_(Type::Work), handle_{.work_ = work} { }
            explicit threadpool_job(void* id, job_task_args* args, PTP_TIMER timer):
                id_(id), args_(args), type_(Type::Timer), handle_{.timer_ = timer} { }

            [[nodiscard]]
            auto get_id() const -> void* { return id_; }

            [[nodiscard]]
            auto get_args() -> job_task_args* { return args_; }

            [[nodiscard]]
            auto get_args() const -> job_task_args* { return args_; }

            ~threadpool_job() {
                switch (type_) {
                    case Type::Work: {
                        CloseThreadpoolWork(handle_.work_);
                        break;
                    }
                    case Type::Timer: {
                        CloseThreadpoolTimer(handle_.timer_);
                        break;
                    }
                    default:
                        break;
                }
            }
        private:
            void* const id_ = nullptr;
            job_task_args* const args_ = nullptr;
            const Type type_ = Type::None;
            const union {
                PTP_WORK work_ = nullptr;
                PTP_TIMER timer_;
            } handle_;
        };

        struct job_task_args {
            threadpool_dispatcher& dispatcher_;
            threadpool_job job_;
            colite::callable<void()> callable_;
        };

        explicit threadpool_dispatcher(DWORD minimum_thread_count = 5, DWORD maximun_thread_count = 10)
        {
            // colite_assert(maximun_thread_count >= minimum_thread_count, "The maximum number of threads must be greater than the minimum");
            colite_assert(maximun_thread_count >= minimum_thread_count);
            InitializeThreadpoolEnvironment(&callback_environ_);

            thread_pool_ = CreateThreadpool(nullptr);
            if (!thread_pool_) {
                char error_message[48];
                snprintf(error_message, sizeof(error_message), "CreateThreadpool failed. LastError: %lu", GetLastError());
                cleanup();
                throw std::runtime_error(error_message);
            }
            SetThreadpoolThreadMaximum(thread_pool_, maximun_thread_count);
            SetThreadpoolThreadMinimum(thread_pool_, minimum_thread_count);
            SetThreadpoolCallbackPool(&callback_environ_, thread_pool_);

            cleanup_group_ = CreateThreadpoolCleanupGroup();
            if (!cleanup_group_) {
                char error_message[48];
                snprintf(error_message, sizeof(error_message), "CreateThreadpoolCleanupGroup failed. LastError: %lu", GetLastError());
                cleanup();
                throw std::runtime_error(error_message);
            }
            SetThreadpoolCallbackCleanupGroup(&callback_environ_, cleanup_group_, nullptr);

            auto operator_work = CreateThreadpoolWork(dispatcher_operator, this, &callback_environ_);
            if (!operator_work) {
                char error_message[48];
                snprintf(error_message, sizeof(error_message), "Create Operator task failed. LastError: %lu", GetLastError());
                cleanup();
                throw std::runtime_error(error_message);
            }
            SubmitThreadpoolWork(operator_work);
        }

        ~threadpool_dispatcher() override {
            cleanup();
        };

        threadpool_dispatcher(const threadpool_dispatcher&) = delete;
        threadpool_dispatcher& operator=(const threadpool_dispatcher&) = delete;
        threadpool_dispatcher(threadpool_dispatcher&& other) noexcept { swap(other); }
        threadpool_dispatcher& operator=(threadpool_dispatcher&& other) noexcept { swap(other); return *this; }

        void swap(threadpool_dispatcher& other) noexcept {
            std::swap(thread_pool_, other.thread_pool_);
        }

        void close() noexcept {
            this->~threadpool_dispatcher();
        }

    protected:
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

    private:
        TP_CALLBACK_ENVIRON callback_environ_ {};
        PTP_CLEANUP_GROUP cleanup_group_ = nullptr;
        PTP_POOL thread_pool_ = nullptr;

        std::atomic<bool> stop_request_ = false;

        std::mutex lock_ {};
        std::list<job, colite::allocator::allocator<job>> jobs_ {  };

        void cleanup() {
            stop_request_ = true;
            if (cleanup_group_) {
                CloseThreadpoolCleanupGroupMembers(cleanup_group_, false, nullptr);
                CloseThreadpoolCleanupGroup(cleanup_group_);
            }
            if (thread_pool_) {
                CloseThreadpool(thread_pool_);
            }
        }

        static VOID CALLBACK dispatcher_operator(PTP_CALLBACK_INSTANCE Instance, PVOID Parameter, PTP_WORK Work) {
            auto* self = static_cast<threadpool_dispatcher*>(Parameter);

            auto& jobs_ = self->jobs_;
            auto& lock_ = self->lock_;
            auto& stop_request = self->stop_request_;

            while (!stop_request) {
                std::optional<job> job = std::nullopt;
                {
                    std::lock_guard locker { lock_ };
                    if (jobs_.empty()) {
                        continue;
                    }
                    if (jobs_.front().ready()) {
                        job = std::move(jobs_.front());
                        jobs_.pop_front();
                    } else {
                        jobs_.splice(jobs_.cend(), jobs_, jobs_.cbegin());
                    }
                }
                if (job) {
                    self->start_dispatch(job->get_id(), std::move(job).value().get_callable());
                }
            }
        }

        void start_dispatch(
            void *id,
            colite::callable<void()> callable
        ) {
            auto* args = (job_task_args*)colite::port::calloc(1, sizeof(job_task_args));

            auto work = CreateThreadpoolWork(job_callback, args, &callback_environ_);
            colite_assert(work);

            ::new (args) job_task_args {
                .dispatcher_ = *this,
                .job_ = threadpool_job(id, args, work),
                .callable_ = std::move(callable)
            };
            SubmitThreadpoolWork(work);
        }

        static VOID CALLBACK job_callback(PTP_CALLBACK_INSTANCE Instance, PVOID Parameter, PTP_WORK Work) {
            auto* args = static_cast<job_task_args*>(Parameter);
            args->callable_();
            args->~job_task_args();
            colite::port::free(args);
        }
    };
}