#pragma once

#include <unordered_map>

namespace colite::allocator {

    template<typename Key, typename Value, typename Alloc>
    using unordered_map = std::unordered_map<
                              Key,
                              Value,
                              std::hash<Key>,
                              std::equal_to<Key>,
                              typename std::allocator_traits<Alloc>::template rebind_alloc<std::pair<const Key, Value>>
                          >;
}