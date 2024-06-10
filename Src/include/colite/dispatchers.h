#pragma once

#include <coroutine>
#include <mutex>
#include <unordered_map>
#include "colite/callable.h"
#include "colite/port.h"
#include "colite/allocator.h"
#include "colite/traits.h"

namespace colite {
    template<typename Alloc>
    class dispatcher;

    // 协程状态
    enum class coroutine_status {
        CREATED,
        STARTED,
        FINISHED,
        CANCELED,
    };

    template<typename Alloc>
    struct coroutine_state {
        // 调度器
        dispatcher<Alloc>* dispatcher_ = nullptr;

        // 当前协程的状态
        coroutine_status state_ = coroutine_status::CREATED;

        // 是否被遗忘
        bool has_detached_ = false;

        // 等待这个协程的人
        std::coroutine_handle<> awaiter_handle_{};
        dispatcher<Alloc> *awaiter_dispatcher_ = nullptr;
    };
}

namespace colite {
    // 调度器基类
    template<typename Alloc = std::allocator<std::byte>>
    class dispatcher {
        using byte_allocator = typename std::allocator_traits<Alloc>::template rebind_alloc<std::byte>;

        template<typename T, typename A>
        friend class colite::suspend;

    public:
        explicit dispatcher(const Alloc& alloc = {}): allocator_(alloc) {}

        virtual ~dispatcher() = default;

        template<typename Coro>
            requires colite::traits::is_allocator_like_suspend<std::remove_cvref_t<Coro>, byte_allocator>
        auto launch(
            Coro&& coroutine,
            colite::port::time_duration duration = colite::port::time_duration(0)
        ) -> decltype(auto) {
            std::coroutine_handle<> handle = coroutine.get_coroutine_handle();
            colite_assert(handle);
            coroutine.state_.value()->dispatcher_ = this;

            auto [it, ok] = coroutines_.try_emplace(handle, coroutine.state_.value());

            coroutine.state_.value()->state_ = colite::coroutine_status::STARTED;
            dispatch(handle.address(), duration, [handle, its_state = coroutine.state_.value(), this] {
                handle.resume();
                dispatch(handle.address(), colite::port::time_duration(0),
                    [handle, its_state, this] {
                        its_state->state_ = colite::coroutine_status::FINISHED;
                        if (its_state->has_detached_) {
                            its_state->dispatcher_->cancel(handle);
                            handle.destroy();
                        } else if (its_state->awaiter_handle_ && its_state->awaiter_dispatcher_) {
                            its_state->awaiter_dispatcher_->dispatch(its_state->awaiter_handle_.address(), colite::port::time_duration(0),
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

        auto sleep(colite::port::time_duration time) {
            return launch([]() -> colite::suspend<void, byte_allocator> {
                co_return;
            }(), time);
        }

    protected:
        byte_allocator allocator_;
        colite::allocator::unordered_map<std::coroutine_handle<>, std::shared_ptr<colite::coroutine_state<byte_allocator>>, byte_allocator> coroutines_ { allocator_ };

        /**
         * @brief 取消所有与当前协程关联的任务，并从协程列表中删除该协程
         * @param handle 协程句柄
         */
        void cancel(std::coroutine_handle<> handle) {
            auto it = coroutines_.find(handle);
            if (it != coroutines_.end()) {
                it->second->state_ = coroutine_status::CANCELED;
            }
            coroutines_.erase(handle);
            cancel_jobs(handle.address());
        }

        virtual void dispatch(void *id, colite::port::time_duration time, colite::callable<void(), byte_allocator> callable) = 0;
        virtual void dispatch(void *id, colite::port::time_duration time, colite::callable<void(), byte_allocator> callable, colite::callable<bool(), byte_allocator> predicate) = 0;
        virtual void cancel_jobs(void *id) = 0;
    };
}