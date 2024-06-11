#include "colite/dispatchers.h"
#include "colite/suspend.h"

auto colite::dispatcher::sleep(colite::port::time_duration time) -> colite::suspend<> {
    auto nop_coroutine = []() -> suspend<> {
        co_return;
    };

    return launch(nop_coroutine(), time);
}

void colite::dispatcher::cancel(std::coroutine_handle<> handle) {
    auto it = coroutines_.find(handle);
    if (it != coroutines_.end()) {
        it->second->state_ = coroutine_status::CANCELED;
    }
    coroutines_.erase(handle);
    cancel_jobs(handle.address());
}
