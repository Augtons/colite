#pragma once

#include <utility>
#include <chrono>
#include <coroutine>
#include "colite_port.h"
#include "dispatchers/colite_dispatchers.h"

namespace colite {

    template<typename T, typename Alloc>
    class suspend;

    namespace suspend_detail {
        template<typename Coro, typename R, typename Alloc>
        class promise_type {
            template<typename T, typename A>
            friend class colite::suspend;

            using return_value_type = std::conditional_t<
                std::is_reference_v<R>,
                std::add_pointer_t<std::remove_reference_t<R>>,
                std::optional<R>
            >;

            using byte_allocator = std::allocator_traits<Alloc>::template rebind_alloc<std::byte>;
            using byte_allocator_pointer = std::allocator_traits<byte_allocator>::pointer;

        public:
            auto get_return_object() -> Coro {
                this_handle = std::coroutine_handle<promise_type>::from_promise(*this);
                return Coro { this_handle };
            }

            std::suspend_always initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }

            void return_value(R&& ret) {
                if constexpr (std::is_reference_v<R>) {
                    ret_value = std::addressof(ret);
                } else {
                    ret_value = std::move(ret);
                }
            }

            void return_value(const R& ret) {
                if constexpr (std::is_reference_v<R>) {
                    ret_value = std::addressof(ret);
                } else {
                    ret_value = ret;
                }
            }

            void unhandled_exception() {
                exception_ptr = std::current_exception();
            }

            void* operator new(std::size_t n) {
                auto allocator = byte_allocator{};
                printf("---Allocate: %zu\n", n);
                return (void*)std::allocator_traits<byte_allocator>::allocate(allocator, n);
            }

            void operator delete(void* ptr, size_t n) noexcept {
                auto allocator = byte_allocator{};
                printf("---Deallocate: %zu\n", n);
                std::allocator_traits<byte_allocator>::deallocate(allocator, (byte_allocator_pointer)ptr, n);
            }

        private:
            std::coroutine_handle<promise_type> this_handle;
            return_value_type ret_value{};
            std::exception_ptr exception_ptr;

            colite::callable<void(), colite::port::allocator> on_dispatch;
            colite::callable<void(), colite::port::allocator> on_cancel;
        };


        template<typename Coro, typename Alloc>
        class promise_type<Coro, void, Alloc> {
            template<typename T, typename A>
            friend class colite::suspend;

            using byte_allocator = std::allocator_traits<Alloc>::template rebind_alloc<std::byte>;
            using byte_allocator_pointer = std::allocator_traits<byte_allocator>::pointer;

        public:
            auto get_return_object() -> Coro {
                this_handle = std::coroutine_handle<promise_type>::from_promise(*this);
                return Coro { this_handle };
            }

            std::suspend_always initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }
            void return_void() {  }

            void unhandled_exception() {
                exception_ptr = std::current_exception();
            }

            void* operator new(std::size_t n) {
                auto allocator = byte_allocator{};
                return (void*)std::allocator_traits<byte_allocator>::allocate(allocator, n);
            }

            void operator delete(void* ptr, size_t n) noexcept {
                auto allocator = byte_allocator{};
                std::allocator_traits<byte_allocator>::deallocate(allocator, (byte_allocator_pointer)ptr, n);
            }

        private:
            std::coroutine_handle<promise_type> this_handle;
            std::exception_ptr exception_ptr;

            colite::callable<void(), colite::port::allocator> on_dispatch;
            colite::callable<void(), colite::port::allocator> on_cancel;
        };
    }
}

namespace colite {
    template<typename T = void, typename Alloc = colite::port::allocator>
    class suspend {
    public:
        using promise_type = suspend_detail::promise_type<suspend, T, Alloc>;
        using handle_type = std::coroutine_handle<promise_type>;
        using byte_allocator = std::allocator_traits<Alloc>::template rebind_alloc<std::byte>;

        suspend() = default;

        explicit suspend(const handle_type& handle):
            this_handle { std::allocate_shared<handle_type, byte_allocator>({}, handle) },
            has_forgotten { std::allocate_shared<bool, byte_allocator>({}, false) },
            has_finished { std::allocate_shared<bool, byte_allocator>({}, false) },
            awaiter_handle { std::allocate_shared<std::coroutine_handle<>, byte_allocator>({}) }
        {
            via(dispatchers::Main);
        }

        suspend(const suspend&) = delete;
        suspend& operator = (const suspend&) = delete;
        suspend(suspend&& other) noexcept { swap(other); }
        suspend& operator = (suspend&& other) noexcept { swap(other); return *this; }

        ~suspend() {
            if (!has_forgotten) {  // 隐式代表此协程返回值对象是空对象
                return;
            }

            if (*has_forgotten) { // 被遗忘对象会在执行结束后自动回收资源
                return;
            }

            if (this_handle) {
                cancel();
            }
        }

        void swap(suspend& other) {
            std::swap(this_handle, other.this_handle);
            std::swap(awaiter_handle, other.awaiter_handle);
            std::swap(has_forgotten, other.has_forgotten);
            std::swap(has_finished, other.has_finished);
            std::swap(has_launched, other.has_launched);
        }

        [[nodiscard]]
        auto get_coroutine_handle() const -> handle_type {
            colite_assert(this_handle);
            return *this_handle;
        }

        [[nodiscard]]
        auto await_ready() const -> bool {
            // 断言 has_forgotten 是否合法，即已被初始化，用于判断此协程返回值对象是否不持有任何对象
            colite_assert(has_forgotten);
            if (*has_forgotten) {
                throw std::runtime_error("suspend<T> cannot be `co_await` when it has been forgotten!");
            }
            // 由于下方保证 has_forgotten 智能指针自身一定被初始化，所以一定代表协程被取消，这可能意味着重复 co_await
            if (!this_handle || *this_handle == nullptr) {
                throw std::runtime_error("suspend<T> is being `co_await` twice or it was cancelled.");
            }

            if (*this_handle) {
                return this_handle->done();
            } else {
                return true;
            }
        }

        void await_suspend(std::coroutine_handle<> ext_handle) {
            colite_assert(awaiter_handle);
            *awaiter_handle = ext_handle;
            if (!has_launched) {
                launch_coroutine();
            }
        }

        auto await_resume() -> T {
            if (!this_handle || *this_handle == nullptr) {
                throw std::runtime_error("suspend<T> has been canceled.");
            }
            check_and_throw_exception();
            if constexpr (std::is_same_v<T, void>) {
                cancel();
                return;
            } else if constexpr (std::is_lvalue_reference_v<T>) {
                auto ret_pointer = this_handle->promise().ret_value;
                cancel();
                return *ret_pointer;
            } else if constexpr (std::is_rvalue_reference_v<T>) {
                auto ret_pointer = this_handle->promise().ret_value;
                cancel();
                return std::move(*ret_pointer);
            } else {
                T ret = std::move(this_handle->promise().ret_value).value();
                cancel();
                return ret;
            }
        }

        template<typename D>
        auto launch(D& dispatcher) & -> suspend& {
            assert(this_handle);
            via(dispatcher);
            if (!await_ready()) {
                launch_coroutine();
            }
            return *this;
        }

        template<typename D>
        auto launch(D& dispatcher) && -> suspend&& {
            assert(this_handle);
            via(dispatcher);
            if (!await_ready()) {
                launch_coroutine();
            }
            return std::move(*this);
        }

        template<typename D, typename R, typename P>
        auto launch(D& dispatcher, std::chrono::duration<R, P> delay) & -> suspend& {
            assert(this_handle);
            via(dispatcher, delay);
            if (!await_ready()) {
                launch_coroutine();
            }
            return *this;
        }

        template<typename D, typename R, typename P>
        auto launch(D& dispatcher, std::chrono::duration<R, P> delay) && -> suspend&& {
            assert(this_handle);
            via(dispatcher, delay);
            if (!await_ready()) {
                launch_coroutine();
            }
            return std::move(*this);
        }

        auto check_exception() -> bool {
            assert(this_handle && has_forgotten);
            if (*has_forgotten) {
                throw std::runtime_error("suspend<T> cannot check its exception when it has been forgotten!");
            }
            return bool(this_handle->promise().exception_ptr);
        }

        auto check_and_throw_exception() -> bool {
            if (check_exception()) {
                auto ex = this_handle->promise().exception_ptr;
                this_handle->promise().exception_ptr = nullptr;
                cancel();
                std::rethrow_exception(ex);
            }
            return false;
        }

        void forget() {
            colite_assert(this_handle);
            if (*this_handle != nullptr) {
                *has_forgotten = true;
            }
        }

        void cancel() {
            colite_assert(this_handle);
            if (*this_handle != nullptr) {
                this_handle->promise().on_cancel();
                this_handle->destroy();
                *this_handle = nullptr;
                this_handle.reset();
            }
        }

    private:
        std::shared_ptr<handle_type> this_handle;
        std::shared_ptr<bool> has_forgotten;
        std::shared_ptr<bool> has_finished;
        std::shared_ptr<std::coroutine_handle<>> awaiter_handle;
        bool has_launched = false;

        void launch_coroutine() {
            colite_assert(!has_launched);
            this_handle->promise().on_dispatch();
            has_launched = true;
        }

        template<typename D>
        void via(D& dispatcher) {
            using namespace std::chrono_literals;

            this_handle->promise().on_dispatch = [&dispatcher,
                                                  this_handle{this_handle},
                                                  awaiter_handle{awaiter_handle},
                                                  has_forgotten{has_forgotten},
                                                  has_finished(has_finished)]
            {
                dispatcher.dispatch(this_handle->address(), 0s, [=, &dispatcher] {
                    this_handle->resume();
                    dispatcher.dispatch(
                        this_handle->address(),
                        0s,
                        [=] {
                            *has_finished = true;
                            if (*awaiter_handle != nullptr) {
                                awaiter_handle->resume();
                            } else if (*has_forgotten) {
                                this_handle->destroy();
                                *this_handle = nullptr;
                            }
                        },
                        [=] {
                            return this_handle->done();
                        }
                    );
                });
            };

            this_handle->promise().on_cancel = [&dispatcher, id = this_handle->address()]
            {
                dispatcher.cancel(id);
            };
        }

        template<typename D, typename R, typename P>
        void via(D& dispatcher, std::chrono::duration<R, P> delay) {
            using namespace std::chrono_literals;

            this_handle->promise().on_dispatch = [&dispatcher,
                                                  delay,
                                                  this_handle{this_handle},
                                                  awaiter_handle{awaiter_handle},
                                                  has_forgotten{has_forgotten},
                                                  has_finished{has_finished}]
            {
                dispatcher.dispatch(this_handle->address(), delay, [=, &dispatcher] {
                    this_handle->resume();
                    dispatcher.dispatch(
                        this_handle->address(),
                        0s,
                        [=] {
                            *has_finished = true;
                            if (*awaiter_handle != nullptr) {
                                awaiter_handle->resume();
                            } else if (*has_forgotten) {
                                this_handle->destroy();
                                *this_handle = nullptr;
                            }
                        },
                        [=] {
                            return this_handle->done();
                        }
                    );
                });
            };

            this_handle->promise().on_cancel = [&dispatcher, id = this_handle->address()]
            {
                dispatcher.cancel(id);
            };
        }
    };
}