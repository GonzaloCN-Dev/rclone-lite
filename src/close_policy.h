#pragma once

#include <cstddef>

namespace rl {

constexpr bool should_warn_before_exit(std::size_t active_transfers)
{
    return active_transfers > 0;
}

} // namespace rl
