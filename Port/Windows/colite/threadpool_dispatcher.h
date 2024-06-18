#pragma once

#include <windows.h>
#include "threadpoolapiset.h"
#include "colite/port.h"
#include "colite/dispatchers.h"

namespace colite::port {
    class threadpool_dispatcher: public colite::dispatcher {
    public:
        struct job_task_args;

        class job {
        public:
            enum class Type { None, Work, Timer };
            explicit job(void* id, job_task_args* args, PTP_WORK work): id_(id), args_(args), type_(Type::Work), work_(work) {}
            explicit job(void* id, job_task_args* args, PTP_TIMER timer): id_(id), args_(args), type_(Type::Timer), timer_(timer) {}

            [[nodiscard]]
            auto get_id() const -> void* { return id_; }

            [[nodiscard]]
            auto get_args() -> job_task_args* { return args_; }

            [[nodiscard]]
            auto get_args() const -> job_task_args* { return args_; }

            // 一定不要写成 ~job() 因为只有外部主动取消 job 的时候才需要等待并删除
            void close() {
                switch (type_) {
                    case Type::Work: {
                        CloseThreadpoolWork(work_);
                        break;
                    }
                    case Type::Timer: {
                        CloseThreadpoolTimer(timer_);
                        break;
                    }
                    default:
                        break;
                }
            }
        private:
            void* id_ = nullptr;
            job_task_args* args_ = nullptr;
            Type type_ = Type::None;
            union {
                PTP_WORK work_ = nullptr;
                PTP_TIMER timer_;
            };
        };

        struct job_task_args {
            threadpool_dispatcher& dispatcher_;
            colite::callable<void()> callable_;
            std::optional<colite::callable<bool()>> predicate_ = std::nullopt;
        };

        explicit threadpool_dispatcher(DWORD minimum_thread_count = 5, DWORD maximun_thread_count = 10)
        {
            // colite_assert(maximun_thread_count >= minimum_thread_count, "The maximum number of threads must be greater than the minimum");
            colite_assert(maximun_thread_count >= minimum_thread_count);
            InitializeThreadpoolEnvironment(&callback_environ_);

            thread_pool_ = CreateThreadpool(nullptr);
            if (!thread_pool_) {
                cleanup();
                char error_message[48];
                snprintf(error_message, sizeof(error_message), "CreateThreadpool failed. LastError: %lu", GetLastError());
                throw std::runtime_error(error_message);
            }
            SetThreadpoolThreadMaximum(thread_pool_, maximun_thread_count);
            SetThreadpoolThreadMinimum(thread_pool_, minimum_thread_count);
            SetThreadpoolCallbackPool(&callback_environ_, thread_pool_);

            cleanup_group_ = CreateThreadpoolCleanupGroup();
            if (!cleanup_group_) {
                cleanup();
                char error_message[48];
                snprintf(error_message, sizeof(error_message), "CreateThreadpoolCleanupGroup failed. LastError: %lu", GetLastError());
                throw std::runtime_error(error_message);
            }
            SetThreadpoolCallbackCleanupGroup(&callback_environ_, cleanup_group_, nullptr);
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

    private:
        TP_CALLBACK_ENVIRON callback_environ_ {};
        PTP_CLEANUP_GROUP cleanup_group_ = nullptr;
        PTP_POOL thread_pool_ = nullptr;

        std::mutex lock_ {};
        std::list<job, colite::allocator::allocator<job>> jobs_ {  };

        void cleanup() {
            if (cleanup_group_) {
                CloseThreadpoolCleanupGroupMembers(cleanup_group_, false, nullptr);
                CloseThreadpoolCleanupGroup(cleanup_group_);
            }
            if (thread_pool_) {
                CloseThreadpool(thread_pool_);
            }
        }

        void dispatch(
            void *id,
            colite::port::time_duration time,
            colite::callable<void()> callable
        ) override {
            auto* args = (job_task_args*)colite::port::calloc(1, sizeof(job_task_args));
            ::new (args) job_task_args {
                .dispatcher_ = *this,
                .callable_ = std::move(callable)
            };

            if (time.count() <= 0) {
                auto work = CreateThreadpoolWork(job_callback, args, &callback_environ_);
                colite_assert(work);
                {
                    std::lock_guard locker { lock_ };
                    jobs_.emplace_back(id, args, work);
                }
                SubmitThreadpoolWork(work);
            } else {
                auto timer = CreateThreadpoolTimer(job_scheduled_callback, args, &callback_environ_);
                colite_assert(timer);
                {
                    std::lock_guard locker { lock_ };
                    jobs_.emplace_back(id, args, timer);
                }

                auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(time).count();
                ULARGE_INTEGER ulDueTime {
                    // 1. 下方 SetThreadpoolTimer 要求单位为 100ns
                    // 2. QuadPart 为负数表示相对时长，QuadPart 为正数表示绝对时刻
                    .QuadPart = (ULONGLONG)-(microseconds * 10)
                };
                FILETIME file_due_time {
                    .dwLowDateTime = ulDueTime.LowPart,
                    .dwHighDateTime = ulDueTime.HighPart,
                };
                SetThreadpoolTimer(timer, &file_due_time, 0, 0);
            }
        }

        void dispatch(
            void *id,
            colite::port::time_duration time,
            colite::callable<void()> callable,
            colite::callable<bool()> predicate
        ) override {
            auto* args = (job_task_args*)colite::port::calloc(1, sizeof(job_task_args));
            ::new (args) job_task_args {
                .dispatcher_ = *this,
                .callable_ = std::move(callable),
                .predicate_ = std::move(predicate)
            };

            if (time.count() <= 0) {
                auto work = CreateThreadpoolWork(job_callback, args, &callback_environ_);
                colite_assert(work);
                {
                    std::lock_guard locker { lock_ };
                    jobs_.emplace_back(id, args, work);
                }
                SubmitThreadpoolWork(work);
            } else {
                auto timer = CreateThreadpoolTimer(job_scheduled_callback, args, &callback_environ_);
                colite_assert(timer);
                {
                    std::lock_guard locker { lock_ };
                    jobs_.emplace_back(id, args, timer);
                }

                auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(time).count();
                ULARGE_INTEGER ulDueTime {
                    // 1. 下方 SetThreadpoolTimer 要求单位为 100ns
                    // 2. QuadPart 为负数表示相对时长，QuadPart 为正数表示绝对时刻
                    .QuadPart = (ULONGLONG)-(microseconds * 10)
                };
                FILETIME file_due_time {
                    .dwLowDateTime = ulDueTime.LowPart,
                    .dwHighDateTime = ulDueTime.HighPart,
                };
                SetThreadpoolTimer(timer, &file_due_time, 0, 0);
            }
        }

        void cancel_jobs(void *id) override {
            decltype(jobs_) temp{};
            {
                std::lock_guard locker { lock_ };

                jobs_.remove_if([&](job& it) {
                    if (id == it.get_id()) {
                        temp.emplace_back(std::move(it));
                        return true;
                    }
                    return false;
                });
            }

            for (job& it : temp) {
                it.close();
            }
            // std::scoped_lock locker { lock_ };
            // jobs_.remove_if([=](job& it) {
            //     if (id == it.get_id()) {
            //         it.close();
            //         return true;
            //     }
            //     return false;
            // });
        }

        static VOID CALLBACK job_callback(PTP_CALLBACK_INSTANCE Instance, PVOID Parameter, PTP_WORK Work) {
            auto* args = static_cast<job_task_args*>(Parameter);
            while(args->predicate_ && !args->predicate_.value()()) {

            }
            args->callable_();
            args->~job_task_args();
            colite::port::free(args);
        }

        static VOID CALLBACK job_scheduled_callback(PTP_CALLBACK_INSTANCE Instance, PVOID Parameter, PTP_TIMER Timer) {
            auto* args = static_cast<job_task_args*>(Parameter);
            while(args->predicate_ && !args->predicate_.value()()) {

            }
            args->callable_();
            args->~job_task_args();
            colite::port::free(args);
        }
    };
}