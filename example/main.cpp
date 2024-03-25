#include <iostream>
#include <functional>
#include "colite.h"
#include "TestAllocator.h"

using namespace colite;
using namespace std::chrono_literals;

suspend<size_t> func(const char* str) {
    for (int i = 0; i < 5; ++i) {
        printf("func: %d\n", i);
        co_await sleep(1s);
    }
    co_return strlen(str);
}

suspend<size_t> j;

suspend<size_t> middle(const char *str) {
    auto l = launch(dispatchers::Main, func, str);
    l.forget();

    suspend<size_t> h;

    co_await sleep(2s);

    h = std::move(l);
    j = std::move(h);

    co_return 0;
}

suspend<int> async_main() {
    printf("Hello\n");

    auto len = co_await middle("Hello");

    printf("LEN = %zu\n", len);

    co_await sleep(4s);

    co_return 0;
}

int main() {
    try {
        colite::run_main(async_main);
    }
    catch (const std::runtime_error& e) {
        fprintf(stderr, "std::runtime_error: %s\n", e.what());
    }

    printf("async_main() returned\n");
    std::this_thread::sleep_for(1s);
}