#pragma once

#include <coroutine>
#include <mutex>
#include <unordered_map>
#include "colite/callable.h"
#include "colite/port.h"
#include "colite/allocator.h"
#include "colite/traits.h"
#include "colite/state.h"

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
            coroutine.state_->set_dispatcher(this);
            coroutine.state_->set_status(coroutine_status::STARTED);

            // 前往目标调度器上回复该协程
            dispatch(handle.address(), duration, [handle, state = coroutine.state_, this] {
                handle.resume();
                // 当当前协程执行完毕之后，判断后续任务（是否要恢复等待者的协程），并销毁当前协程
                dispatch(handle.address(), colite::port::time_duration(0),
                    [handle, state, this] {
                        if (state->is_awaited()) {
                            auto [awaiter_handle, awaiter_dispatcher] = state->awaiter();
                            awaiter_dispatcher->dispatch(handle.address(), colite::port::time_duration(0),
                                [handle, awaiter_handle, this] {
                                    awaiter_handle.resume();
                                }
                            );
                        }
                    },
                    [=] {
                        auto status = state->get_status();
                        return status == coroutine_status::CANCELED || status == coroutine_status::FINISHED;
                    }
                );
            });

            return std::forward<Coro>(coroutine);
        }

    protected:
        std::mutex lock_{};

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
