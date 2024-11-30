#pragma once

#include <tuple>
#include <coroutine>
#include "colite/port.h"

namespace colite {
    class dispatcher;

    template<typename T = void>
    class suspend;

    namespace detail {
        template<typename C, typename R>
        class promise_type;
    }

    // 协程状态
    enum class coroutine_status {
        CREATED,
        STARTED,
        FINISHED,
        CANCELED
    };

    class base_coroutine_state {
        template<typename C, typename R>
        friend class colite::detail::promise_type;

        template<typename T>
        friend class colite::suspend;
    public:
        /**
         * @brief 设置调度器
         * @param dispatcher
         */
        void set_dispatcher(dispatcher *dispatcher) {
            dispatcher_ = dispatcher;
        }

        /**
         * @brief 获取当前协程的调度器
         * @return
         */
        [[nodiscard]]
        auto get_dispatcher() const -> dispatcher* {
            return dispatcher_;
        }

        /**
         * @brief 设置该协程的等待状态
         * @param awaiter_handle
         * @param awaiter_dispatcher
         */
        void await(const std::coroutine_handle<> awaiter_handle, dispatcher *const awaiter_dispatcher) {
            colite_assert(is_awaited() == false);
            awaiter_handle_ = awaiter_handle;
            awaiter_dispatcher_ = awaiter_dispatcher;
        }

        /**
         * @brief 检查当前协程状态是否包含等待数据
         * @return
         */
        [[nodiscard]]
        auto is_awaited() const -> bool {
            return awaiter_handle_ && awaiter_dispatcher_;
        }

        [[nodiscard]]
        auto awaiter() const {
            return std::make_tuple(awaiter_handle_, awaiter_dispatcher_);
        }

        /**
         * @brief 获取协程的状态
         * @return 状态
         */
        [[nodiscard]]
        auto get_status() const -> coroutine_status { return status_; }

        /**
         * @brief 设置协程状态
         */
        void set_status(coroutine_status status) { status_ = status; }

    protected:
        // 当前协程的调度器
        dispatcher* dispatcher_ = nullptr;

        // 当前协程的状态
        volatile coroutine_status status_ = coroutine_status::CREATED;
        std::exception_ptr exception_ptr_{};

        // 等待这个协程的人
        std::coroutine_handle<> awaiter_handle_{};
        dispatcher *awaiter_dispatcher_ = nullptr;
    };

    template<typename R = void>
    class coroutine_state: public base_coroutine_state {
    public:
        void set_return_value(R&& ret) {
            if constexpr (std::is_reference_v<R>) {
                ret_value_ = std::addressof(ret);
            } else {
                ret_value_ = std::move(ret);
            }
        }

        void set_return_value(const R& ret) {
            if constexpr (std::is_reference_v<R>) {
                ret_value_ = std::addressof(ret);
            } else {
                ret_value_ = ret;
            }
        }

        auto get_return_value() -> R {
            if constexpr (std::is_lvalue_reference_v<R>) {
                return *ret_value_;
            } else if constexpr (std::is_rvalue_reference_v<R>) {
                return std::move(*ret_value_);
            } else {
                return std::move(ret_value_).value();
            }
        }
    protected:
        using return_value_type = std::conditional_t<
            std::is_reference_v<R>,
            std::add_pointer_t<std::remove_reference_t<R>>,
            std::optional<R>
        >;
        return_value_type ret_value_{};
    };

    template<>
    class coroutine_state<void>: public base_coroutine_state {

    };
}
