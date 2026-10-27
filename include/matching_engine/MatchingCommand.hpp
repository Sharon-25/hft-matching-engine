#pragma once

#include <future>
#include <optional>
#include <type_traits>
#include <utility>

#include "matching_engine/MatchingEngine.hpp"
#include "matching_engine/Order.hpp"

namespace matching_engine {

struct MatchingCommand {
    Order order;

    std::promise<SubmissionResult> result_promise;

#ifdef MATCHING_ENGINE_TESTING
    std::optional<InsertionFailurePoint> failure_point;
#endif

    MatchingCommand(
        Order incoming_order,
        std::promise<SubmissionResult> promise
    )
        : order(std::move(incoming_order)),
          result_promise(std::move(promise)) {}

    MatchingCommand(const MatchingCommand&) = delete;
    MatchingCommand& operator=(const MatchingCommand&) = delete;

    MatchingCommand(MatchingCommand&&) noexcept = default;
    MatchingCommand& operator=(MatchingCommand&&) = delete;
};

static_assert(
    std::is_nothrow_move_constructible_v<MatchingCommand>,
    "MatchingCommand must support nonthrowing moves"
);

} // namespace matching_engine
