#pragma once

#include <memory>
#include <type_traits>
#include "colite_port.h"

namespace colite {

    namespace unique_ptr_detail {
        template<typename T, typename Alloc, typename... Args>
            requires std::is_constructible_v<T, Args...>
        inline auto create_by_alloc(const Alloc& allocator, Args&&... args) {
            using allocator_type = std::allocator_traits<Alloc>::template rebind_alloc<T>;
            allocator_type alloc { allocator };
            auto ptr = std::allocator_traits<allocator_type>::allocate(alloc, 1);
            if constexpr (!std::is_trivially_constructible_v<T, Args...>) {
                ::new(ptr) T(std::forward<Args>(args)...);
            }
            return ptr;
        }

        template<typename T, typename Alloc>
        inline auto delete_by_alloc(const Alloc& allocator, T* ptr) {
            using allocator_type = std::allocator_traits<Alloc>::template rebind_alloc<T>;
            allocator_type alloc { allocator };
            if constexpr (!std::is_trivially_destructible_v<T>) {
                ptr->~T();
            }
            std::allocator_traits<allocator_type>::deallocate(alloc, ptr, 1);
        }
    }

    template<typename T, typename Alloc = colite::port::allocator>
    using unique_ptr = std::unique_ptr<T, colite::callable<void(void*), Alloc>>;

    template<typename T, typename Alloc = colite::port::allocator, typename... Args>
    inline auto make_unique(const Alloc& allocator, Args&&... args) {
        return colite::unique_ptr<T>(
            unique_ptr_detail::create_by_alloc<T>(allocator, std::forward<Args>(args)...),
            [=](void* ptr) {
                unique_ptr_detail::delete_by_alloc(allocator, (T*)ptr);
            }
        );
    }
}