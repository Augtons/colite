#pragma once

#include <memory>
#include <type_traits>

template<typename T>
class TestAllocator {
public:
    using value_type = T;

    constexpr TestAllocator() = default;

    template<typename U>
    constexpr TestAllocator(TestAllocator<U>) noexcept {  }

    [[nodiscard]]
    auto allocate(size_t n) -> T* {
        printf("TestAllocator: allocate %zu bytes\n", n);
        return (T*)calloc(n, sizeof(T));
    }

    void deallocate(T* pointer, size_t n) {
        printf("TestAllocator: free %zu bytes\n", n);
        free(pointer);
    }
};

template<typename T, typename U>
inline auto operator == (const TestAllocator<T>&, const TestAllocator<U>&) {
    return true;
}

template<typename T, typename U>
inline auto operator != (const TestAllocator<T>&, const TestAllocator<U>&) {
    return false;
}