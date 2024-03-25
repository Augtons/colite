#pragma once

#include <cassert>
#include <memory>

#define colite_assert(...) assert(__VA_ARGS__)

namespace colite::port {
    using allocator = std::allocator<std::byte>;


}