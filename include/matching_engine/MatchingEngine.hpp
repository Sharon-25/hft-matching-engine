#pragma once

#include <unordered_set>
#include <vector>

#include "matching_engine/OrderBook.hpp"
#include "matching_engine/Trade.hpp"

namespace matching_engine {

enum class SubmissionStatus {
    ACCEPTED,
    DUPLICATE_ID,
    INVALID_ORDER
};

struct SubmissionResult {
    SubmissionStatus status;
    Quantity executed_quantity;
    Quantity remaining_quantity;
};

struct MarketOrderAccounting {
    Quantity executed_quantity;
    Quantity cancelled_quantity;
};

class MatchingEngine {
private:
    OrderBook book_;
#ifdef MATCHING_ENGINE_TESTING
    using AcceptedOrderIds = std::unordered_set<
        OrderId,
        std::hash<OrderId>,
        std::equal_to<OrderId>,
        testing::TestingAllocator<OrderId>
    >;
#else
    using AcceptedOrderIds = std::unordered_set<OrderId>;
#endif

    AcceptedOrderIds accepted_order_ids_;
    std::vector<Trade> trades_;

public:
    using MarketOrderAccounting = matching_engine::MarketOrderAccounting;

    MatchingEngine() = default;

    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    MatchingEngine(MatchingEngine&&) = delete;
    MatchingEngine& operator=(MatchingEngine&&) = delete;

    SubmissionResult submit_limit_order(const Order& order);
    SubmissionResult submit_market_order(const Order& order);

    MarketOrderAccounting market_order_accounting(
        const SubmissionResult& result
    ) const noexcept;
    bool was_order_id_accepted(OrderId id) const;

    const OrderBook& order_book() const;
    const std::vector<Trade>& trades() const noexcept;

#ifdef MATCHING_ENGINE_TESTING
    void set_insertion_failure_for_test(
        InsertionFailurePoint point
    ) noexcept {
        book_.set_insertion_failure_for_test(point);
    }
#endif
};

} // namespace matching_engine
