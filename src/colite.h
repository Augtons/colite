#pragma once

#include <memory>
#include <iostream>
#include <type_traits>

#include "colite_port.h"
#include "colite_suspend.h"
#include "dispatchers/colite_dispatchers.h"

namespace colite {
    template<typename Coro, typename... Args>
    inline auto run_main(Coro &&coroutine, Args&&... args) -> int {
        using namespace std::chrono_literals;

        auto awaiter = coroutine(std::forward<Args>(args)...);
        auto handle = awaiter.get_coroutine_handle();

        auto& main_dispatcher = colite::dispatchers::Main;

        main_dispatcher.set_has_done_callback([&] {
            awaiter.check_and_throw_exception();
            return handle.done();
        });

        awaiter.launch(main_dispatcher);
        main_dispatcher.run();
        return 0;
    }

    template<typename Dispacher, typename Coro, typename... Args>
    auto launch(Dispacher& dispacher, Coro&& coro, Args&&... args) {
        return coro(std::forward<Args>(args)...).launch(dispacher);
    }

    template<typename R, typename P>
    suspend<> sleep(std::chrono::duration<R, P> delay) {
        auto sleep_impl = []() -> suspend<> {
            co_return;
        };
        co_await sleep_impl().launch(dispatchers::Main, delay);
    }
}