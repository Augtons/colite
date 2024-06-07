#include <iostream>
#include <functional>
#include <memory>
#include <atomic>
#include "colite/colite.h"

std::atomic<size_t> allocated_size_ = 0;

struct Leak {
    Leak() = default;
    Leak(const Leak&) = delete;
    Leak& operator=(const Leak&) = delete;
    ~Leak() {
        printf("\nTest Finish! %zu bytes leaked!", (size_t)allocated_size_);
    }
} leak_ {};

template<typename T>
class TestAllocator {
public:
    using value_type = T;

    constexpr TestAllocator() = default;

    template<typename U>
    constexpr TestAllocator(const TestAllocator<U>&) noexcept {  }

    [[nodiscard]]
    auto allocate(size_t n) -> value_type* {
        allocated_size_ += n * sizeof(value_type);
        printf("-- TestAllocator: allocate %zu bytes\n", n * sizeof(value_type));
        return (value_type*)calloc(n, sizeof(value_type));
    }

    void deallocate(value_type* pointer, size_t n) {
        allocated_size_ -= n * sizeof(value_type);
        printf("-- TestAllocator: free %zu bytes\n", n * sizeof(value_type));
        free(pointer);
    }
};

template<typename T, typename U>
auto operator == (const TestAllocator<T>&, const TestAllocator<U>&) {
    return true;
}

template<typename T, typename U>
auto operator != (const TestAllocator<T>&, const TestAllocator<U>&) {
    return false;
}

int main() {
    int d1 = 0;
    int d2 = 0;

    colite::callable<void(), TestAllocator<int>> f = [&] {
    // std::function f = [&] {
        ++d1;
        printf("Hello World (%d, %d)\n", d1, d2);
    };

    auto g = f;

    g = f;
    g = std::move(f);

    g();
    g();

    g = [&] {
        ++d1;
        ++d2;
        printf("Hello World 2\n");
        printf("Hello World 2 %d, %d\n", d1, d2);
    };

    g();

    auto h = std::move(g);

    h();
}