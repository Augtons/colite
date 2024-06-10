#pragma once

#include <coroutine>
#include <optional>
#include <memory>
#include "colite/traits.h"
#include "colite/dispatchers.h"

namespace colite {
    namespace detail {
        template<typename Coro, typename R>
        class promise_type {
            template<typename T>
            friend class colite::suspend;

            using return_value_type = std::conditional_t<
                std::is_reference_v<R>,
                std::add_pointer_t<std::remove_reference_t<R>>,
                std::optional<R>
            >;

        public:
            auto get_return_object() -> Coro {
                this_handle_ = std::coroutine_handle<promise_type>::from_promise(*this);
                state_ = std::allocate_shared<colite::coroutine_state, colite::allocator::allocator<std::byte>>({});
                return Coro { this_handle_, state_ };
            }

            std::suspend_always initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }

            template<typename Any>
            auto await_transform(Any&& any) -> decltype(auto) {
                if constexpr (colite::traits::is_std_chrono_duration<std::remove_cvref_t<Any>>) {
                    return state_->dispatcher_->sleep(std::forward<Any>(any));
                } else {
                    return std::forward<Any>(any);
                }
            }

            void return_value(R&& ret) {
                if constexpr (std::is_reference_v<R>) {
                    ret_value_ = std::addressof(ret);
                } else {
                    ret_value_ = std::move(ret);
                }
            }

            void return_value(const R& ret) {
                if constexpr (std::is_reference_v<R>) {
                    ret_value_ = std::addressof(ret);
                } else {
                    ret_value_ = ret;
                }
            }

            void unhandled_exception() {
                exception_ptr_ = std::current_exception();
            }

            void* operator new(std::size_t n) {
                return colite::port::calloc(n, sizeof(std::byte));
            }

            void operator delete(void* ptr, size_t n) noexcept {
                colite::port::free(ptr);
            }

        private:
            std::coroutine_handle<promise_type> this_handle_;
            return_value_type ret_value_{};
            std::exception_ptr exception_ptr_;
            std::shared_ptr<colite::coroutine_state> state_;
        };

        template<typename Coro>
        class promise_type<Coro, void> {
            template<typename T>
            friend class colite::suspend;

        public:
            auto get_return_object() -> Coro {
                this_handle_ = std::coroutine_handle<promise_type>::from_promise(*this);
                state_ = std::allocate_shared<colite::coroutine_state, colite::allocator::allocator<std::byte>>({});
                return Coro { this_handle_, state_ };
            }

            std::suspend_always initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }
            void return_void() { }

            template<typename Any>
            auto await_transform(Any&& any) -> decltype(auto) {
                if constexpr (colite::traits::is_std_chrono_duration<std::remove_cvref_t<Any>>) {
                    return state_->dispatcher_->sleep(std::forward<Any>(any));
                } else {
                    return std::forward<Any>(any);
                }
            }

            void unhandled_exception() {
                exception_ptr_ = std::current_exception();
            }

            void* operator new(std::size_t n) {
                return colite::port::calloc(n, sizeof(std::byte));
            }

            void operator delete(void* ptr, size_t n) noexcept {
                colite::port::free(ptr);
            }

        private:
            std::coroutine_handle<promise_type> this_handle_;
            std::exception_ptr exception_ptr_;
            std::shared_ptr<colite::coroutine_state> state_;
        };
    }
}

namespace colite {
    template<typename T>
    class suspend {
    public:
        using promise_type = colite::detail::promise_type<suspend, T>;

        friend class colite::dispatcher;

        suspend() = default;
        suspend(const suspend&) = delete;
        suspend& operator=(const suspend&) = delete;
        suspend(suspend&& other) noexcept { swap(other); }
        suspend& operator=(suspend&& other) noexcept { swap(other); return *this; }

        explicit suspend(
            const std::coroutine_handle<promise_type>& handle,
            std::shared_ptr<colite::coroutine_state> state
        ): this_handle_(handle), state_(std::move(state)) {

        }

        ~suspend() {
            if (!this_handle_) {
                return;
            }
            if (!state_.value()->dispatcher_) {
                // 协程已创建但未与调度器关联，由协程自身负责释放内存
                cancel();
                return;
            }
            if (!state_.value()->has_detached_) {
                cancel();
            }
        }

        void swap(suspend& other) noexcept {
            std::swap(this_handle_, other.this_handle_);
            std::swap(state_, other.state_);
        }

        [[nodiscard]]
        auto get_coroutine_handle() const -> std::coroutine_handle<promise_type> {
            return this_handle_;
        }

        [[nodiscard]]
        auto await_ready() const -> bool {
            if (!this_handle_) {
                throw std::runtime_error("suspend<T> is null.");
            }
            if (!state_.value()->dispatcher_) {
                throw std::runtime_error("suspend<T> is not associated with any dispatcher.");
            }
            if (state_.value()->state_ == coroutine_status::CANCELED) {
                throw std::runtime_error("suspend<T> is being `co_await` when it was cancelled.");
            }
            if (state_.value()->has_detached_) {
                throw std::runtime_error("suspend<T> cannot be `co_await` when it has been detached!");
            }
            if (state_.value()->awaiter_handle_ != nullptr) {
                throw std::runtime_error("suspend<T> is being `co_await` twice or it was cancelled.");
            }
            return this_handle_.done();
        }

        template<typename Promise>
        void await_suspend(std::coroutine_handle<Promise> ext_handle) {
            colite_assert(this_handle_ && state_.value()->dispatcher_);
            if (state_.value()->state_ == coroutine_status::CANCELED) {
                throw std::runtime_error("suspend<T> is being `co_await` when it was cancelled.");
            }
            state_.value()->awaiter_handle_ = ext_handle;
            if (state_.value()->state_ == colite::coroutine_status::CREATED) {
                state_.value()->dispatcher_->launch(*this);
            }
        }

        auto await_resume() -> T {
            colite_assert(this_handle_ && state_.value()->dispatcher_);
            if (!state_.value()->dispatcher_->coroutines_.contains(this_handle_)) {
                throw std::runtime_error("suspend<T> has been canceled.");
            }
            check_and_throw_exception();
            if constexpr (std::is_same_v<T, void>) {
                cancel();
                return;
            } else if constexpr (std::is_lvalue_reference_v<T>) {
                auto ret_pointer = this_handle_->promise().ret_value_;
                cancel();
                return *ret_pointer;
            } else if constexpr (std::is_rvalue_reference_v<T>) {
                auto ret_pointer = this_handle_.promise().ret_value_;
                cancel();
                return std::move(*ret_pointer);
            } else {
                T ret = std::move(this_handle_.promise().ret_value_).value();
                cancel();
                return ret;
            }
        }

        void check_and_throw_exception() {
            if (!this_handle_ || !state_.value()->dispatcher_) {
                return;
            }
            if (!state_.value()->dispatcher_->coroutines_.contains(this_handle_)) {
                return;
            }
            auto ex = this_handle_.promise().exception_ptr_;
            if (ex) {
                this_handle_.promise().exception_ptr_ = nullptr;
                cancel();
                std::rethrow_exception(ex);
            }
        }

        void detach() {
            if (!this_handle_ || !state_.value()->dispatcher_) {
                return;
            }
            state_.value()->has_detached_ = true;
            if (state_.value()->state_ == coroutine_status::FINISHED) {
                cancel();
            }
        }

        void cancel() {
            if (!this_handle_) {
                return;
            }
            if (state_.value()->dispatcher_) {
                state_.value()->dispatcher_->cancel(this_handle_);
                state_.value()->dispatcher_ = nullptr;
            }
            this_handle_.destroy();
            this_handle_ = nullptr;
        }
    private:
        std::coroutine_handle<promise_type> this_handle_ {};
        std::optional<std::shared_ptr<colite::coroutine_state>> state_ = std::nullopt;
    };

    inline auto nop_coroutine() -> suspend<> {
        co_return;
    }
}
