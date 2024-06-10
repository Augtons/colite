#pragma once

#include <bitset>
#include <cstddef>
#include <utility>
#include <functional>
#include <memory>
#include "colite/traits.h"

namespace colite::detail {
    // 可调用目标包装器的基类
    template<typename R, typename... Args>
    class target_wrapper {
    public:
        virtual ~target_wrapper() = default;
        virtual void copy_to(target_wrapper *ptr) const = 0;
        virtual void move_to(target_wrapper *ptr) = 0;
        virtual auto operator()(Args&&... args) const -> R = 0;
    };

    // 可调用目标包装器的实现类
    template<typename C, typename R, typename... Args>
    class target_wrapper_impl: public target_wrapper<R, Args...> {
    public:
        explicit target_wrapper_impl(C target): target(std::move(target)) { }
        ~target_wrapper_impl() override = default;

        void copy_to(target_wrapper<R, Args...> *ptr) const override {
            ::new (ptr) target_wrapper_impl(target);
        }
        void move_to(target_wrapper<R, Args...> *ptr) override {
            ::new (ptr) target_wrapper_impl(std::move(target));
        }
        auto operator()(Args&&... args) const -> R override {
            return std::invoke(target, std::forward<Args>(args)...);
        }
    private:
        C target;
    };
}

namespace colite {
    template<typename Fn, typename Alloc = std::allocator<std::byte>>
    class callable {
    public:
        using byte_allocator = typename std::allocator_traits<Alloc>::template rebind_alloc<std::byte>;
        using target_wrapper_type = typename traits::target_traits<Fn>::target_wrapper_type;

        /**
         * 判断当前 callable 是否发生了小对象优化 (SSO)
         * @return
         */
        [[nodiscard]]
        auto is_sso() const -> bool { return target_size_ <= sizeof(target_wrapper_data_); }

        /**
         * @brief 默认构造
         * @param alloc 分配器
         */
        explicit callable(const Alloc& alloc = {}): allocator_(alloc) {  }

        /**
         * @brief 自可调用对象 C 的构造
         * @tparam C 可调用对象类型
         * @param fn 可调用对象
         * @param alloc 分配器
         */
        template<typename C>
            requires (!traits::is_callable_v<std::remove_cvref_t<C>>)
                && traits::target_traits<Fn>::template is_callable_target_of_it<std::remove_cvref_t<C>>
        callable(C&& fn, const Alloc& alloc = {}):
            allocator_(alloc)
        {
            using impl_type = typename traits::target_traits<Fn>::template target_wrapper_impl_type<C>;
            target_size_ = sizeof(impl_type);
            if (is_sso()) {
                auto& sso = target_wrapper_data_.sso;
                ::new (sso.target_buf_.data()) impl_type(std::forward<C>(fn));
            } else {
                auto& no_sso = target_wrapper_data_.no_sso;
                no_sso.target_ptr_ = reinterpret_cast<target_wrapper_type*>(std::allocator_traits<byte_allocator>::allocate(allocator_, target_size_));
                ::new (no_sso.target_ptr_) impl_type(std::forward<C>(fn));
            }
        }

        /**
         * @brief 析构
         */
        ~callable() {
            if (!*this) {
                return;
            }
            if (is_sso()) {
                auto& sso = target_wrapper_data_.sso;
                reinterpret_cast<target_wrapper_type*>(sso.target_buf_.data())->~target_wrapper_type();
            } else {
                auto& no_sso = target_wrapper_data_.no_sso;
                no_sso.target_ptr_->~target_wrapper_type();

                using allocator_pointer_type = typename std::allocator_traits<byte_allocator>::pointer;
                std::allocator_traits<byte_allocator>::deallocate(allocator_, reinterpret_cast<allocator_pointer_type>(no_sso.target_ptr_), target_size_);
            }
        }

        /**
         * @brief 调用表达式
         * @param args 参数
         * @return 返回值
         */
        template<typename... Args>
        auto operator()(Args&&... args) const {
            if (is_sso()) {
                return reinterpret_cast<const target_wrapper_type*>(target_wrapper_data_.sso.target_buf_.data())->operator()(std::forward<Args>(args)...);
            } else {
                return target_wrapper_data_.no_sso.target_ptr_->operator()(std::forward<Args>(args)...);
            }
        }

        /**
         * @brief 判断是否包含内容
         */
        explicit operator bool() const {
            return target_size_ > 0;
        }

        /**
         * @brief 复制构造
         * @param other
         */
        callable(const callable& other):
            allocator_(other.allocator_),
            target_size_(other.target_size_)
        {
            if (is_sso()) {
                auto& sso = target_wrapper_data_.sso;
                auto& other_sso = other.target_wrapper_data_.sso;
                reinterpret_cast<const target_wrapper_type*>(other_sso.target_buf_.data())->copy_to(reinterpret_cast<target_wrapper_type*>(sso.target_buf_.data()));
            } else {
                auto& no_sso = target_wrapper_data_.no_sso;
                auto& other_no_sso = other.target_wrapper_data_.no_sso;
                no_sso.target_ptr_ = reinterpret_cast<target_wrapper_type*>(std::allocator_traits<byte_allocator>::allocate(allocator_, target_size_));
                reinterpret_cast<const target_wrapper_type*>(other_no_sso.target_ptr_)->copy_to(no_sso.target_ptr_);
            }
        }

        /**
         * 移动构造
         * @param other
         */
        callable(callable&& other) noexcept:
            allocator_(std::exchange(other.allocator_, {})),
            target_size_(std::exchange(other.target_size_, 0))
        {
            if (is_sso()) {
                auto& sso = target_wrapper_data_.sso;
                auto& other_sso = other.target_wrapper_data_.sso;
                reinterpret_cast<target_wrapper_type*>(other_sso.target_buf_.data())->move_to(reinterpret_cast<target_wrapper_type*>(sso.target_buf_.data()));
            } else {
                auto& no_sso = target_wrapper_data_.no_sso;
                auto& other_no_sso = other.target_wrapper_data_.no_sso;
                no_sso.target_ptr_ = std::move(other_no_sso.target_ptr_);
            }
        }

        /**
         * @brief 复制赋值
         * @param other
         * @return 当前对象
         */
        callable& operator=(const callable& other) {
            this->~callable();
            allocator_ = other.allocator_;
            target_size_ = other.target_size_;
            if (is_sso()) {
                auto& sso = target_wrapper_data_.sso;
                auto& other_sso = other.target_wrapper_data_.sso;
                reinterpret_cast<const target_wrapper_type*>(other_sso.target_buf_.data())->copy_to(reinterpret_cast<target_wrapper_type*>(sso.target_buf_.data()));
            } else {
                auto& no_sso = target_wrapper_data_.no_sso;
                auto& other_no_sso = other.target_wrapper_data_.no_sso;
                no_sso.target_ptr_ = reinterpret_cast<target_wrapper_type*>(std::allocator_traits<byte_allocator>::allocate(allocator_, target_size_));
                reinterpret_cast<const target_wrapper_type*>(other_no_sso.target_ptr_)->copy_to(no_sso.target_ptr_);
            }
            return *this;
        }

        /**
         * @brief 移动赋值
         * @param other
         * @return 当前对象
         */
        callable& operator=(callable&& other) noexcept {
            swap(other);
            return *this;
        }

        /**
         * @brief 交换对象
         * @param other
         */
        void swap(callable& other) noexcept {
            auto& sso = target_wrapper_data_.sso;
            auto& no_sso = target_wrapper_data_.no_sso;
            auto& other_sso = other.target_wrapper_data_.sso;
            auto& other_no_sso = other.target_wrapper_data_.no_sso;

            std::swap(allocator_, other.allocator_);

            const auto this_is_sso = is_sso();
            const auto other_is_sso = other.is_sso();
            if (this_is_sso && other_is_sso) {
                auto temp = std::move(sso.target_buf_);
                reinterpret_cast<target_wrapper_type*>(other_sso.target_buf_.data())->move_to(reinterpret_cast<target_wrapper_type*>(sso.target_buf_.data()));
                reinterpret_cast<target_wrapper_type*>(temp.data())->move_to(reinterpret_cast<target_wrapper_type*>(other_sso.target_buf_.data()));
            } else if (!this_is_sso && !other_is_sso) {
                std::swap(no_sso.target_ptr_, other_no_sso.target_ptr_);
            } else if (this_is_sso && !other_is_sso) {
                auto temp = other_no_sso.target_ptr_;
                reinterpret_cast<target_wrapper_type*>(sso.target_buf_.data())->move_to(reinterpret_cast<target_wrapper_type*>(other_sso.target_buf_.data()));
                no_sso.target_ptr_ = temp;
            } else if (!this_is_sso && other_is_sso) {
                auto temp = no_sso.target_ptr_;
                reinterpret_cast<target_wrapper_type*>(other_sso.target_buf_.data())->move_to(reinterpret_cast<target_wrapper_type*>(sso.target_buf_.data()));
                other_no_sso.target_ptr_ = temp;
            }
            std::swap(target_size_, other.target_size_);
        }

    private:
        // 分配器
        byte_allocator allocator_;

        // target 对象大小
        size_t target_size_ = 0;

        // target 包装器，考虑小对象优化
        union {
            // 未发生小对象优化
            struct no_sso {
                target_wrapper_type *target_ptr_;
            } no_sso;
            // 发生小对象优化
            struct sso {
                std::array<std::byte, std::max(sizeof(struct no_sso), 2 * sizeof(void*))> target_buf_;
            } sso;
        } target_wrapper_data_{};
    };
}