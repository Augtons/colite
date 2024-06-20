#pragma once

#include <coroutine>
#include <optional>
#include <memory>
#include "colite/traits.h"
#include "colite/dispatchers.h"

namespace colite::detail {
    template<typename Promise>
    class base_promise {
        template<typename T>
        friend class colite::suspend;
    public:
        std::suspend_always initial_suspend() noexcept { return {}; }

        void* operator new(std::size_t n) {
            return colite::port::calloc(n, sizeof(std::byte));
        }

        void operator delete(void* ptr, size_t n) noexcept {
            colite::port::free(ptr);
        }

    protected:
        std::coroutine_handle<Promise> this_handle_;
    };


    template<typename Coro, typename R>
    class promise_type: public base_promise<promise_type<Coro, R>> {
        template<typename T>
        friend class colite::suspend;

        using base_promise_t = base_promise<promise_type>;
    public:
        using base_promise_t::initial_suspend;
        using base_promise_t::operator new;
        using base_promise_t::operator delete;

        std::suspend_never final_suspend() noexcept {
            colite_assert(state_->get_status() != coroutine_status::CANCELED);
            state_->set_status(coroutine_status::FINISHED);
            return {};
        }

        auto get_return_object() -> Coro {
            this_handle_ = std::coroutine_handle<promise_type>::from_promise(*this);
            state_ = std::allocate_shared<colite::coroutine_state<R>, colite::allocator::allocator<std::byte>>({});
            return Coro { this_handle_, state_ };
        }

        template<typename Any>
        auto await_transform(Any&& any) -> decltype(auto) {
            if constexpr (colite::traits::is_std_chrono_duration<std::remove_cvref_t<Any>>) {
                return state_->get_dispatcher()->sleep(std::forward<Any>(any));
            } else if constexpr (colite::traits::is_suspend<std::remove_cvref_t<Any>>) {
                if (any && any.state_->get_status() == coroutine_status::CREATED) {
                    return state_->get_dispatcher()->launch(std::forward<Any>(any));
                } else {
                    return std::forward<Any>(any);
                }
            } else {
                return std::forward<Any>(any);
            }
        }

        void return_value(R&& ret) {
            state_->set_return_value(std::move(ret));
        }

        void return_value(const R& ret) {
            state_->set_return_value(ret);
        }

        void unhandled_exception() {
            state_->exception_ptr_ = std::current_exception();
        }

    protected:
        using return_value_type = std::conditional_t<
            std::is_reference_v<R>,
            std::add_pointer_t<std::remove_reference_t<R>>,
            std::optional<R>
        >;
        using base_promise_t::this_handle_;
        std::shared_ptr<colite::coroutine_state<R>> state_;
    };

    template<typename Coro>
    class promise_type<Coro, void> : base_promise<promise_type<Coro, void>> {
        template<typename T>
        friend class colite::suspend;

        using base_promise_t = base_promise<promise_type>;
    public:
        using base_promise_t::initial_suspend;
        using base_promise_t::operator new;
        using base_promise_t::operator delete;

        std::suspend_never final_suspend() noexcept {
            colite_assert(state_->get_status() != coroutine_status::CANCELED);
            state_->set_status(coroutine_status::FINISHED);
            return {};
        }

        auto get_return_object() -> Coro {
            this_handle_ = std::coroutine_handle<promise_type>::from_promise(*this);
            state_ = std::allocate_shared<colite::coroutine_state<>, colite::allocator::allocator<std::byte>>({});
            return Coro { this_handle_, state_ };
        }

        template<typename Any>
        auto await_transform(Any&& any) -> decltype(auto) {
            if constexpr (colite::traits::is_std_chrono_duration<std::remove_cvref_t<Any>>) {
                return state_->dispatcher_->sleep(std::forward<Any>(any));
            } else if constexpr (colite::traits::is_suspend<std::remove_cvref_t<Any>>) {
                if (any && any.state_->status_ == coroutine_status::CREATED) {
                    return state_->dispatcher_->launch(std::forward<Any>(any));
                } else {
                    return std::forward<Any>(any);
                }
            } else {
                return std::forward<Any>(any);
            }
        }

        void return_void() {

        }

        void unhandled_exception() {
            state_->exception_ptr_ = std::current_exception();
        }

    protected:
        using base_promise_t::this_handle_;
        std::shared_ptr<colite::coroutine_state<>> state_;
    };
}


namespace colite {
    template<typename T>
    class suspend {
    public:
        using promise_type = colite::detail::promise_type<suspend, T>;

        friend class colite::dispatcher;

        template<typename Coro, typename R>
        friend class colite::detail::promise_type;

        suspend() = default;
        suspend(const suspend&) = delete;
        suspend& operator=(const suspend&) = delete;
        suspend(suspend&& other) noexcept { swap(other); }
        suspend& operator=(suspend&& other) noexcept { swap(other); return *this; }

        explicit suspend(
            const std::coroutine_handle<promise_type>& handle,
            std::shared_ptr<colite::coroutine_state<T>> state
        ): this_handle_(handle), state_(std::move(state)) {

        }

        ~suspend() {
            if (!*this) {
                return;
            }
            if (!has_detached_) {
                cancel();
            }
        }

        [[nodiscard]]
        explicit operator bool() const { return this_handle_ != nullptr; }

        void swap(suspend& other) noexcept {
            std::swap(this_handle_, other.this_handle_);
            std::swap(state_, other.state_);
            std::swap(has_detached_, other.has_detached_);
        }

        [[nodiscard]]
        auto get_coroutine_handle() const -> std::coroutine_handle<promise_type> {
            return this_handle_;
        }

        [[nodiscard]]
        auto await_ready() const -> bool {
            if (!*this) {
                throw std::runtime_error("suspend<T> is null.");
            }
            if (!state_->get_dispatcher()) {
                throw std::runtime_error("suspend<T> is not associated with any dispatcher.");
            }
            if (state_->get_status() == coroutine_status::CANCELED) {
                throw std::runtime_error("suspend<T> is being `co_await` when it was cancelled.");
            }
            if (state_->is_awaited()) {
                throw std::runtime_error("suspend<T> is being `co_await` twice or it was cancelled.");
            }
            return this_handle_.done();
        }

        template<typename Promise>
        void await_suspend(std::coroutine_handle<Promise> ext_handle) {
            colite_assert(*this);
            if (state_->get_status() == coroutine_status::CANCELED) {
                throw std::runtime_error("suspend<T> is being `co_await` when it was cancelled.");
            }
            auto dispatcher = ext_handle.promise().state_->get_dispatcher();
            state_->await(ext_handle, dispatcher);
        }

        auto await_resume() -> T {
            colite_assert(*this);
            if (state_->get_status() == colite::coroutine_status::CANCELED) {
                throw std::runtime_error("suspend<T> has been canceled.");
            }
            check_and_throw_exception();
            if constexpr (!std::is_same_v<T, void>) {
                return state_->get_return_value();
            } else {
                return;
            }
        }

        void check_and_throw_exception() {
            if (!*this) {
                return;
            }
            auto ex = state_->exception_ptr_;
            if (ex) {
                state_->exception_ptr_ = nullptr;
                cancel();
                std::rethrow_exception(ex);
            }
        }

        void detach() {
            if (*this) {
                has_detached_ = true;
            }
        }

        /**
         * @brief 取消协程，若协程已被取消或正常执行完毕，则无操作
         */
        void cancel() {
            if (!*this) {
                return;
            }
            auto status = state_->get_status();
            if (status == coroutine_status::FINISHED || status == coroutine_status::CANCELED) {
                return;
            }
            state_->set_status(coroutine_status::CANCELED);
            auto dispatcher = state_->get_dispatcher();
            if (dispatcher) {
                dispatcher->cancel(this_handle_);
            }
        }
    protected:
        std::coroutine_handle<promise_type> this_handle_ {};
        std::shared_ptr<colite::coroutine_state<T>> state_ {};
        bool has_detached_ = false;
    };
}
