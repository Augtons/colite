#include <iostream>
#include <chrono>
#include <ranges>
#include <thread>

#include "colite/colite.h"

using namespace std::chrono_literals;

colite::eventloop_dispatcher dispatcher;

colite::suspend<int> data(const char* name) {
    printf("[%s]%s\n", name, __PRETTY_FUNCTION__);
    for (auto i : std::views::iota(0) | std::views::take(5)) {
        printf("%s OK\n", name);
        co_await 10ms;
    }
    co_return 123;
}

colite::suspend<int> async_main() {
    printf("Hello\n");

    auto coro = dispatcher.launch(data("c0"));
    co_await 20ms;
    coro.cancel();
    printf("Bye: %d\n", co_await dispatcher.launch(data("c1")));
    co_await 2s;
    printf("Bye: %d\n", co_await dispatcher.launch(data("c2")));
    co_return 123123;
}

int main() {
    return dispatcher.run(async_main());
}