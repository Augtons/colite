#include <iostream>
#include <memory>
#include <atomic>
#include <chrono>
#include <ranges>
#include <thread>

#include "colite/colite.h"

using namespace std::chrono_literals;

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
        // printf("-- TestAllocator: allocate %zu bytes\n", n * sizeof(value_type));
        return (value_type*)calloc(n, sizeof(value_type));
    }

    void deallocate(value_type* pointer, size_t n) {
        allocated_size_ -= n * sizeof(value_type);
        // printf("-- TestAllocator: free %zu bytes\n", n * sizeof(value_type));
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

colite::eventloop_dispatcher<TestAllocator<std::byte>> dispatcher;

colite::suspend<int, TestAllocator<std::byte>> data(const char* name) {
    printf("[%s]%s\n", name, __PRETTY_FUNCTION__);
    for (auto i : std::views::iota(0) | std::views::take(5)) {
        co_await dispatcher.sleep(125ms);
        printf("%s OK\n", name);
    }
    co_return 123;
}

colite::suspend<int, TestAllocator<std::byte>> async_main() {
    printf("Hello\n");
    printf("Bye: %d\n", co_await dispatcher.launch(data("c1")));
    co_await 2s;
    printf("Bye: %d\n", co_await dispatcher.launch(data("c1")));
    co_return 123123;
}

int main() {
    return dispatcher.run(async_main());
}