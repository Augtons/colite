#pragma once

#include <coroutine>
#include <mutex>
#include <unordered_map>
#include "colite/callable.h"
#include "colite/port.h"
#include "colite/allocator.h"
#include "colite/traits.h"

namespace colite {
    class dispatcher;

    template<typename T = void>
    class suspend;

    // 协程状态
    enum class coroutine_status {
        CREATED,
        STARTED,
        FINISHED,
        CANCELED,
    };

    struct coroutine_state {
        // 调度器
        dispatcher* dispatcher_ = nullptr;

        // 当前协程的状态
        coroutine_status state_ = coroutine_status::CREATED;

        // 是否被遗忘
        bool has_detached_ = false;

        // 等待这个协程的人
        std::coroutine_handle<> awaiter_handle_{};
    };
}

namespace colite {
    // 调度器基类
    class dispatcher {
        using byte_allocator = colite::allocator::allocator<std::byte>;

        template<typename T = void>
        friend class colite::suspend;

    public:
        explicit dispatcher() = default;
        virtual ~dispatcher() = default;

        auto sleep(colite::port::time_duration time) -> colite::suspend<>;

        template<typename Coro>
            requires colite::traits::is_suspend<std::remove_cvref_t<Coro>>
        auto launch(
            Coro&& coroutine,
            colite::port::time_duration duration = colite::port::time_duration(0)
        ) -> decltype(auto) {
            std::coroutine_handle<> handle = coroutine.get_coroutine_handle();
            colite_assert(handle);
            coroutine.state_->dispatcher_ = this;

            auto [it, ok] = coroutines_.try_emplace(handle, coroutine.state_);

            coroutine.state_->state_ = colite::coroutine_status::STARTED;
            dispatch(handle.address(), duration, [handle, its_state = coroutine.state_, this] {
                handle.resume();
                dispatch(handle.address(), colite::port::time_duration(0),
                    [handle, its_state, this] {
                        its_state->state_ = colite::coroutine_status::FINISHED;
                        if (its_state->has_detached_) {
                            its_state->dispatcher_->cancel(handle);
                            handle.destroy();
                        } else if (its_state->awaiter_handle_ && its_state->dispatcher_) {
                            its_state->dispatcher_->dispatch(its_state->awaiter_handle_.address(), colite::port::time_duration(0),
                                [handle, its_state, this] {
                                    its_state->awaiter_handle_.resume();
                                },
                                [=] {
                                    return handle.done();
                                }
                            );
                        }
                    },
                    [=] {
                        return handle.done();
                    }
                );
            });

            return std::forward<Coro>(coroutine);
        }

    protected:
        std::unordered_map<
            std::coroutine_handle<>,
            std::shared_ptr<colite::coroutine_state>,
            std::hash<std::coroutine_handle<>>,
            std::equal_to<std::coroutine_handle<>>,
            colite::allocator::allocator<std::pair<const std::coroutine_handle<>, std::shared_ptr<colite::coroutine_state>>>
        > coroutines_ {};

        /**
         * @brief 取消所有与当前协程关联的任务，并从协程列表中删除该协程
         * @param handle 协程句柄
         */
        void cancel(std::coroutine_handle<> handle);

        virtual void dispatch(void *id, colite::port::time_duration time, colite::callable<void()> callable) = 0;
        virtual void dispatch(void *id, colite::port::time_duration time, colite::callable<void()> callable, colite::callable<bool()> predicate) = 0;
        virtual void cancel_jobs(void *id) = 0;
    };
}
