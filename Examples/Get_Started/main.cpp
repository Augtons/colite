#include <iostream>
#include <chrono>
#include <ranges>
#include <thread>

#include "colite/colite.h"
#include "colite/eventloop_dispatcher.h"
#include "colite/threadpool_dispatcher.h"

using namespace std::chrono_literals;

colite::port::eventloop_dispatcher dispatcher;
colite::port::threadpool_dispatcher io_dispatcher{1, 5};

colite::suspend<int> data(const char* name) {
    printf("[%s]%s\n", name, __PRETTY_FUNCTION__);
    for (auto i : std::views::iota(0) | std::views::take(100)) {
        std::cout << name << "[" << i << "]" << std::this_thread::get_id() << std::endl;
        co_await 50ms;
    }
    co_return 123;
}

colite::suspend<int> async_main() {
    printf("Hello\n");

    std::cout << "This1: " << std::this_thread::get_id() << std::endl;
    {
        auto coro0 = io_dispatcher.launch(data("c0"));
        co_await 2s;
        coro0.detach();
    }
    co_await 2s;
    // auto r = co_await coro0;
    std::cout << "This2: " << std::this_thread::get_id() << std::endl;
    co_return 1;
}

int main() {
    return dispatcher.run(async_main());
}