#pragma once

#include <type_traits>
#include <chrono>

namespace colite {
    template<typename Fn, typename Alloc>
    class callable;

    template<typename T, typename Alloc>
    class suspend;

    namespace detail {
        template<typename R, typename... Args>
        class target_wrapper;

        template<typename C, typename R, typename... Args>
        class target_wrapper_impl;
    }

    namespace traits {
        /**
         * @brief 用于判断是否为类模板 `colite::callable` 的实例化类型
         * @tparam C 待判断的类
         */
        template<typename C>
        constexpr bool is_callable_v = false;

        template<typename Fn, typename Alloc>
        constexpr bool is_callable_v<colite::callable<Fn, Alloc>> = true;

        /**
         * @brief 用于判断指定的函数类型 F 的特性
         * @tparam F 待判断的函数类型
         */
        template<typename F>
        struct target_traits;

        template<typename R, typename... Args>
        struct target_traits<R(Args...)> {
            // 获取该函数类型 F 对应的类模板 colite::detail::target 实例化
            using target_wrapper_type = detail::target_wrapper<R, Args...>;

            // 获取该函数类型 F 对应的，对可调用对象 C 进行包装的 colite::detail::target_impl 实例化
            template<typename C>
            using target_wrapper_impl_type = detail::target_wrapper_impl<C, R, Args...>;

            // 判断可调用对象 C 是否满足该函数的调用签名
            template<typename C>
            static constexpr bool is_callable_target_of_it = std::is_invocable_r_v<R, C, Args...>;
        };

        /**
         * @brief 判断类型 T 是否为协程 suspend
         * @tparam T
         */
        template<typename T>
        constexpr bool is_suspend = false;

        template<typename T, typename Alloc>
        constexpr bool is_suspend<colite::suspend<T, Alloc>> = true;

        /**
         * @brief 满足分配器是否为 Alloc 的 rebind_alloc 的 suspend 类
         * @tparam S 待判断的 suspend 对象
         * @tparam Alloc 待判断的分配器
         */
        template<typename S, typename Alloc>
        concept is_allocator_like_suspend = is_suspend<S> && std::same_as<
            typename S::byte_allocator,
            typename std::allocator_traits<Alloc>::template rebind_alloc<std::byte>
        >;

        template<typename C>
        constexpr bool is_std_chrono_duration = false;

        template<typename R, typename P>
        constexpr bool is_std_chrono_duration<std::chrono::duration<R, P>> = true;
    }
}