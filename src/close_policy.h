#pragma once

#include <cstddef>

namespace rl {

constexpr bool should_warn_before_exit(std::size_t active_transfers)
{
    return active_transfers > 0;
}

constexpr bool should_defer_update(std::size_t active_transfers)
{
    return active_transfers > 0;
}

} // namespace rl
