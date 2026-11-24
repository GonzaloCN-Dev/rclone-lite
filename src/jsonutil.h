#pragma once

#include <string>
#include <type_traits>

#include <nlohmann/json.hpp>

namespace rl {

// nlohmann's json::value() throws when the key exists but holds null or another type.
// rclone emits null for several fields, and an exception escaping a worker thread kills
// the whole process, so every read of external JSON goes through this tolerant accessor.
template<class D>
auto jv(const nlohmann::json &j, const char *key, const D &def)
{
    using Dd = std::decay_t<D>;
    using T = std::conditional_t<std::is_same_v<Dd, nlohmann::json>, nlohmann::json,
                                 std::conditional_t<std::is_convertible_v<Dd, std::string>, std::string, Dd>>;
    if (!j.is_object())
        return T(def);
    auto it = j.find(key);
    if (it == j.end() || it->is_null())
        return T(def);
    try {
        return it->template get<T>();
    } catch (...) {
        return T(def);
    }
}

// dump() throws on invalid UTF-8 by default; replace instead so saving never fails.
inline std::string dump_safe(const nlohmann::json &j, int indent = -1)
{
    return j.dump(indent, ' ', false, nlohmann::json::error_handler_t::replace);
}

} // namespace rl
