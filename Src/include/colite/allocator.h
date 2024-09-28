#pragma once

#include <unordered_map>
#include "colite/port.h"

namespace colite::allocator {
    template<typename T>
    class allocator {
    public:
        using value_type = T;

        constexpr allocator() = default;

        template<typename U>
        constexpr allocator(const allocator<U>&) noexcept {  }

        [[nodiscard]]
        auto allocate(size_t n) -> value_type* {
            return (value_type*)colite::port::calloc(n, sizeof(value_type));
        }

        void deallocate(value_type* pointer, size_t n) {
            colite::port::free(pointer);
        }
    };

    template<typename T, typename U>
    auto operator == (const allocator<T>&, const allocator<U>&) {
        return true;
    }

    template<typename T, typename U>
    auto operator != (const allocator<T>&, const allocator<U>&) {
        return false;
    }
}
