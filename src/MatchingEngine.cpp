#include "matching_engine/MatchingEngine.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace matching_engine {

namespace {

class OrderIdReservation {
private:
    std::unordered_set<OrderId>& ids_;
    OrderId id_;
    bool committed_ = false;

public:
    OrderIdReservation(
        std::unordered_set<OrderId>& ids,
        OrderId id
    )
        : ids_(ids), id_(id)
    {
        auto [it, inserted] = ids_.insert(id);

        if (!inserted) {
            throw std::logic_error(
                "Order ID already reserved"
            );
        }
    }

    OrderIdReservation(const OrderIdReservation&) = delete;
    OrderIdReservation& operator=(const OrderIdReservation&) = delete;

    ~OrderIdReservation() {
        if (!committed_) {
            ids_.erase(id_);
        }
    }

    void commit() noexcept {
        committed_ = true;
    }
};

} // unnamed namespace

SubmissionResult MatchingEngine::submit_limit_order(
    const Order& order
) {
    if (order.type() != OrderType::LIMIT) {
        return {
            SubmissionStatus::INVALID_ORDER,
            0,
            order.remaining_quantity()
        };
    }

    if (order.status() != OrderStatus::NEW ||
        (order.side() != Side::BUY &&
         order.side() != Side::SELL)) {
        return {
            SubmissionStatus::INVALID_ORDER,
            0,
            order.remaining_quantity()
        };
    }

    if (accepted_order_ids_.contains(order.id()) ||
        book_.was_order_id_used(order.id())) {
        return {
            SubmissionStatus::DUPLICATE_ID,
            0,
            order.remaining_quantity()
        };
    }

    // Allocate session-ID storage before any execution.
    OrderIdReservation reservation(
        accepted_order_ids_,
        order.id()
    );

    const std::size_t expected_trades =
        book_.count_matchable_orders(
            order.side(),
            order.price_ticks(),
            order.remaining_quantity()
        );

    if (expected_trades > trades_.max_size() - trades_.size()) {
        throw std::length_error(
            "Trade history capacity exceeded"
        );
    }

    trades_.reserve(
        trades_.size() + expected_trades
    );

    // Allocate incoming order storage before any execution.
    if (!book_.add_active_order(order)) {
        return {
            SubmissionStatus::INVALID_ORDER,
            0,
            order.remaining_quantity()
        };
    }

    // Order has been admitted into the book; commit the session ID permanently.
    reservation.commit();

    const Side opposite_side =
        order.side() == Side::BUY
            ? Side::SELL
            : Side::BUY;

    Quantity executed_quantity = 0;
    Quantity remaining_quantity = order.remaining_quantity();

    const std::size_t trades_before = trades_.size();

    while (remaining_quantity > 0) {
        const Order* resting =
            book_.best_order(opposite_side);

        if (resting == nullptr) {
            break;
        }

        const bool crosses =
            order.side() == Side::BUY
                ? order.price_ticks() >= resting->price_ticks()
                : order.price_ticks() <= resting->price_ticks();

        if (!crosses) {
            break;
        }

        const Quantity executable =
            std::min(
                remaining_quantity,
                resting->remaining_quantity()
            );

        const OrderId resting_id = resting->id();
        const Price execution_price =
            resting->price_ticks();

        // Both orders are already stored.
        // These operations do not allocate new book nodes.
        const bool matched =
            book_.execute_match(
                order.id(),
                resting_id,
                executable
            );

        if (!matched) {
            throw std::logic_error(
                "Matching engine invariant violated"
            );
        }

        trades_.push_back(Trade{
            order.id(),
            resting_id,
            execution_price,
            executable
        });

        executed_quantity += executable;
        remaining_quantity -= executable;
    }

    if (trades_.size() - trades_before != expected_trades) {
        throw std::logic_error(
            "Preflight trade count does not match executions"
        );
    }

    return {
        SubmissionStatus::ACCEPTED,
        executed_quantity,
        remaining_quantity
    };
}

SubmissionResult MatchingEngine::submit_market_order(
    const Order& order
) {
    // 1. Validate the incoming market order.
    if (order.type() != OrderType::MARKET ||
        order.status() != OrderStatus::NEW ||
        (order.side() != Side::BUY &&
         order.side() != Side::SELL)) {
        return {
            SubmissionStatus::INVALID_ORDER,
            0,
            order.remaining_quantity()
        };
    }

    // 2. Reject previously accepted or used IDs.
    if (accepted_order_ids_.contains(order.id()) ||
        book_.was_order_id_used(order.id())) {
        return {
            SubmissionStatus::DUPLICATE_ID,
            0,
            order.remaining_quantity()
        };
    }

    // 3. Reserve the incoming ID.
    // This rolls back automatically if preparation fails.
    OrderIdReservation reservation(
        accepted_order_ids_,
        order.id()
    );

    // 4. Count the resting orders this market order
    // could execute against.
    //
    // These values are preflight crossing bounds,
    // NOT execution prices.
    const Price crossing_bound =
        order.side() == Side::BUY
            ? std::numeric_limits<Price>::max()
            : Price{0};

    const std::size_t expected_trades =
        book_.count_matchable_orders(
            order.side(),
            crossing_bound,
            order.remaining_quantity()
        );

    if (expected_trades > book_.order_count()) {
        throw std::logic_error(
            "Market preflight count exceeds resting order count"
        );
    }

    // 5. Reserve all trade-record storage before
    // performing any irreversible executions.
    if (expected_trades >
        trades_.max_size() - trades_.size()) {
        throw std::length_error(
            "Trade history capacity exceeded"
        );
    }

    trades_.reserve(
        trades_.size() + expected_trades
    );

    // 6. Execute directly against the resting book.
    // The incoming market order is never inserted.
    const Side opposite_side =
        order.side() == Side::BUY
            ? Side::SELL
            : Side::BUY;

    Quantity executed_quantity = 0;
    Quantity remaining_quantity =
        order.remaining_quantity();

    const std::size_t trades_before =
        trades_.size();

    while (remaining_quantity > 0) {
        const Order* resting =
            book_.best_order(opposite_side);

        if (resting == nullptr) {
            break;
        }

        const Quantity executable =
            std::min(
                remaining_quantity,
                resting->remaining_quantity()
            );

        // Capture fields before execution.
        // The resting order may be erased afterward.
        const OrderId resting_id = resting->id();
        const Price execution_price =
            resting->price_ticks();

        // Only the resting order exists in the book.
        if (!book_.execute_best_order(
                opposite_side,
                executable)) {
            throw std::logic_error(
                "Market execution invariant violated"
            );
        }

        // The first completed execution makes this ID permanent.
        reservation.commit();

        trades_.push_back(Trade{
            order.id(),
            resting_id,
            execution_price,
            executable
        });

        executed_quantity += executable;
        remaining_quantity -= executable;
    }

    // 7. Verify preflight trade accounting.
    if (trades_.size() - trades_before !=
        expected_trades) {
        throw std::logic_error(
            "Market trade count mismatch"
        );
    }

    if (executed_quantity >
        order.original_quantity() ||
        remaining_quantity !=
            order.original_quantity() - executed_quantity) {
        throw std::logic_error(
            "Market order quantity accounting mismatch"
        );
    }

    // 8. Preserve the accepted ID permanently.
    reservation.commit();

    // The unexecuted remainder is cancelled.
    // It is never inserted into the order book.
    return {
        SubmissionStatus::ACCEPTED,
        executed_quantity,
        remaining_quantity
    };
}

bool MatchingEngine::was_order_id_accepted(
    OrderId id
) const {
    return accepted_order_ids_.contains(id);
}

const OrderBook& MatchingEngine::order_book() const {
    return book_;
}

const std::vector<Trade>& MatchingEngine::trades() const noexcept {
    return trades_;
}

MarketOrderAccounting MatchingEngine::market_order_accounting(
    const SubmissionResult& result
) const noexcept {
    return {
        result.executed_quantity,
        result.remaining_quantity
    };
}

} // namespace matching_engine
