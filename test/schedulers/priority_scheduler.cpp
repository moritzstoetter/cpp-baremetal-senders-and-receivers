#include "detail/common.hpp"

#include <async/concepts.hpp>
#include <async/connect.hpp>
#include <async/continue_on.hpp>
#include <async/debug.hpp>
#include <async/just_result_of.hpp>
#include <async/schedulers/priority_scheduler.hpp>
#include <async/schedulers/task_manager.hpp>
#include <async/start_detached.hpp>
#include <async/start_on.hpp>
#include <async/then.hpp>

#include <stdx/concepts.hpp>
#include <stdx/ct_format.hpp>

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <concepts>
#include <string>
#include <vector>

namespace {
struct hal {
    static auto schedule(async::priority_t) {}
};

using task_manager_t = async::priority_task_manager<hal, 8>;
} // namespace

template <> inline auto async::injected_task_manager<> = task_manager_t{};

TEST_CASE("fixed_priority_scheduler fulfils concept", "[priority_scheduler]") {
    static_assert(async::scheduler<async::fixed_priority_scheduler<0>>);
}

TEST_CASE("fixed_priority_scheduler sender advertises nothing",
          "[priority_scheduler]") {
    static_assert(async::sender_of<
                  decltype(async::fixed_priority_scheduler<0>::schedule()),
                  async::set_value_t()>);
}

TEST_CASE("sender has the fixed_priority_scheduler as its completion scheduler",
          "[priority_scheduler]") {
    using S = async::fixed_priority_scheduler<0>;
    auto s = S::schedule();
    auto cs =
        async::get_completion_scheduler<async::set_value_t>(async::get_env(s));
    static_assert(std::same_as<decltype(cs), S>);
}

TEST_CASE("fixed_priority_scheduler schedules tasks", "[priority_scheduler]") {
    auto s = async::fixed_priority_scheduler<0>{};
    int var{};
    async::sender auto sndr =
        async::start_on(s, async::just_result_of([&] { var = 42; }));
    auto op = async::connect(sndr, universal_receiver{});

    async::task_mgr::service_tasks<0>();
    CHECK(var == 0);

    async::start(op);
    async::task_mgr::service_tasks<0>();
    CHECK(var == 42);
    CHECK(async::task_mgr::is_idle());
}

TEST_CASE("fixed_priority_scheduler is cancellable before start",
          "[priority_scheduler]") {
    auto s = async::fixed_priority_scheduler<0>{};
    int var{};
    async::sender auto sndr =
        async::start_on(s, async::just_result_of([&] { var = 42; }));
    auto r = stoppable_receiver{[&] { var = 17; }};
    auto op = async::connect(sndr, r);

    r.request_stop();
    async::start(op);
    async::task_mgr::service_tasks<0>();
    CHECK(var == 17);
    CHECK(async::task_mgr::is_idle());
}

TEST_CASE("fixed_priority_scheduler is cancellable after start",
          "[priority_scheduler]") {
    auto s = async::fixed_priority_scheduler<0>{};
    int var{};
    async::sender auto sndr =
        async::start_on(s, async::just_result_of([&] { var = 42; }));
    auto r = stoppable_receiver{[&] { var = 17; }};
    auto op = async::connect(sndr, r);

    async::start(op);
    r.request_stop();
    async::task_mgr::service_tasks<0>();
    CHECK(var == 17);
    CHECK(async::task_mgr::is_idle());
}

TEST_CASE("request and response", "[priority_scheduler]") {
    int var{};

    using client_context = async::fixed_priority_scheduler<1>;
    using server_context = async::fixed_priority_scheduler<2>;

    auto s = client_context::schedule() //
             | async::then([&] {
                   ++var;
                   return 42;
               })                                   //
             | async::continue_on(server_context{}) //
             | async::then([&](auto i) {
                   ++var;
                   return i * 2;
               })                                   //
             | async::continue_on(client_context{}) //
             | async::then([&](auto i) { var += i; });
    CHECK(async::start_detached(s));

    CHECK(var == 0);
    async::task_mgr::service_tasks<1>();
    CHECK(var == 1);
    async::task_mgr::service_tasks<2>();
    CHECK(var == 2);
    async::task_mgr::service_tasks<1>();
    CHECK(var == 86);
    CHECK(async::task_mgr::is_idle());
}

namespace {
std::vector<std::string> debug_events{};

struct debug_handler {
    template <stdx::ct_string C, stdx::ct_string L, stdx::ct_string S,
              typename Ctx>
    constexpr auto signal(auto &&...) {
        debug_events.push_back(fmt::format("{} {} {}", C, L, S));
    }
};
} // namespace

template <> inline auto async::injected_debug_handler<> = debug_handler{};

TEST_CASE("fixed_priority_scheduler can be debugged", "[priority_scheduler]") {
    using namespace std::string_literals;
    debug_events.clear();

    auto s = async::fixed_priority_scheduler<0, "sched">::schedule();
    auto op = async::connect(
        s, with_env{universal_receiver{},
                    async::prop{async::get_debug_interface_t{},
                                async::debug::named_interface<"op">{}}});

    async::start(op);
    CHECK(debug_events == std::vector{"op sched start"s});
    async::task_mgr::service_tasks<0>();
    CHECK(debug_events ==
          std::vector{"op sched start"s, "op sched set_value"s});
}

TEST_CASE("fixed_priority_scheduler produces set_stopped debug signal",
          "[priority_scheduler]") {
    using namespace std::string_literals;
    debug_events.clear();

    auto stop = async::inplace_stop_source{};
    auto s = async::fixed_priority_scheduler<0, "sched">::schedule();
    auto r = with_env{
        universal_receiver{},
        async::env{async::prop{async::get_debug_interface_t{},
                               async::debug::named_interface<"op">{}},
                   async::prop{async::get_stop_token_t{}, stop.get_token()}}};
    auto op = async::connect(s, r);

    async::start(op);
    CHECK(debug_events == std::vector{"op sched start"s});
    stop.request_stop();
    async::task_mgr::service_tasks<0>();
    CHECK(debug_events ==
          std::vector{"op sched start"s, "op sched set_stopped"s});
}
