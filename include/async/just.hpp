#pragma once

#include <async/completes_synchronously.hpp>
#include <async/completion_tags.hpp>
#include <async/concepts.hpp>
#include <async/connect.hpp>
#include <async/debug.hpp>
#include <async/env.hpp>
#include <async/type_traits.hpp>

#include <stdx/concepts.hpp>
#include <stdx/ct_string.hpp>
#include <stdx/tuple.hpp>

#include <concepts>
#include <type_traits>
#include <utility>

namespace async {
namespace _just {
template <stdx::ct_string Name, typename Tag, typename R, typename... Vs>
struct op_state {
    [[no_unique_address]] R receiver;
    [[no_unique_address]] stdx::tuple<Vs...> values;

    constexpr auto start() & -> void {
        debug_signal<"start", Name, op_state>(get_env(receiver));
        std::move(values).apply([&]<typename... Ts>(Ts &&...ts) {
            debug_signal<Tag::name, Name, op_state>(get_env(receiver));
            Tag{}(std::move(receiver), std::forward<Ts>(ts)...);
        });
    }

    [[nodiscard]] constexpr auto query(get_env_t) const noexcept {
        return prop{completes_synchronously_t{}, std::true_type{}};
    }
};

template <stdx::ct_string Name, typename Tag, typename... Vs> struct sender {
    using is_sender = void;
    using completion_signatures = async::completion_signatures<Tag(Vs...)>;
    [[no_unique_address]] stdx::tuple<Vs...> values;

    template <receiver R>
    [[nodiscard]] constexpr auto
    connect(R &&r) && -> op_state<Name, Tag, std::remove_cvref_t<R>, Vs...> {
        check_connect<sender &&, R>();
        return {std::forward<R>(r), std::move(values)};
    }

    template <receiver R>
        requires std::copy_constructible<decltype(values)>
    [[nodiscard]] constexpr auto connect(
        R &&r) const & -> op_state<Name, Tag, std::remove_cvref_t<R>, Vs...> {
        check_connect<sender const &, R>();
        return {std::forward<R>(r), values};
    }

    [[nodiscard]] constexpr auto query(get_env_t) const noexcept {
        return prop{completes_synchronously_t{}, std::true_type{}};
    }
};
} // namespace _just

template <stdx::ct_string Name = "just", typename... Vs>
[[nodiscard]] constexpr auto just(Vs &&...vs) -> sender auto {
    return _just::sender<Name, set_value_t, std::remove_cvref_t<Vs>...>{
        {std::forward<Vs>(vs)...}};
}

template <stdx::ct_string Name = "just_error", typename V>
[[nodiscard]] constexpr auto just_error(V &&v) -> sender auto {
    return _just::sender<Name, set_error_t, std::remove_cvref_t<V>>{
        {std::forward<V>(v)}};
}

template <stdx::ct_string Name = "just_stopped">
[[nodiscard]] constexpr auto just_stopped() -> sender auto {
    return _just::sender<Name, set_stopped_t>{};
}
} // namespace async
