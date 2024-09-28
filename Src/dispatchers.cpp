#include "colite/dispatchers.h"
#include "colite/suspend.h"

static auto nop_coroutine() -> colite::suspend<> {
    co_return;
}

auto colite::dispatcher::sleep(colite::port::time_duration time) -> colite::suspend<> {
    return launch(nop_coroutine(), time);
}

void colite::dispatcher::cancel(std::coroutine_handle<> handle) {
    cancel_jobs(handle.address());
}
