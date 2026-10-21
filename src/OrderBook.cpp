#include "matching_engine/OrderBook.hpp"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <new>
#include <stdexcept>

namespace matching_engine {

#ifdef MATCHING_ENGINE_TESTING
void OrderBook::maybe_fail_insertion(
    InsertionFailurePoint point
) {
    if (failure_point_ == point) {
        failure_point_ = InsertionFailurePoint::NONE;
        throw std::bad_alloc{};
    }
}
#endif

bool OrderBook::add_order(const Order& order) {
    if (order.status() != OrderStatus::NEW ||
        (order.side() != Side::BUY &&
         order.side() != Side::SELL) ||
        used_order_ids_.contains(order.id())) {
        return false;
    }

    if (order.side() == Side::BUY) {
        insert_into_side(bids_, order);
    } else {
        insert_into_side(asks_, order);
    }

    return true;
}

bool OrderBook::add_active_order(const Order& order) {
    if (!order.is_active() ||
        (order.side() != Side::BUY &&
         order.side() != Side::SELL) ||
        order_index_.contains(order.id())) {
        return false;
    }

    if (order.side() == Side::BUY) {
        insert_into_side(bids_, order);
    } else {
        insert_into_side(asks_, order);
    }

    return true;
}

bool OrderBook::cancel_order(OrderId id) {
    auto found = order_index_.find(id);

    if (found == order_index_.end()) {
        return false;
    }

    const auto& location = found->second;

    if (location.side == Side::BUY) {
        auto level = bids_.find(location.price);

        // The index invariant guarantees this level exists.
        level->second.erase(location.iterator);

        if (level->second.empty()) {
            bids_.erase(level);
        }
    } else {
        auto level = asks_.find(location.price);

        level->second.erase(location.iterator);

        if (level->second.empty()) {
            asks_.erase(level);
        }
    }

    order_index_.erase(found);

    return true;
}

bool OrderBook::execute_best_order(
    Side side,
    Quantity quantity
) {
    if (quantity == 0) {
        return false;
    }

    if (side == Side::BUY) {
        if (bids_.empty()) {
            return false;
        }

        auto level = bids_.begin();
        Order& order = level->second.front();

        if (quantity > order.remaining_quantity()) {
            return false;
        }

        const OrderId id = order.id();

        if (!order.execute(quantity)) {
            return false;
        }

        if (order.is_filled()) {
            order_index_.erase(id);
            level->second.pop_front();

            if (level->second.empty()) {
                bids_.erase(level);
            }
        }

        return true;
    }

    if (side == Side::SELL) {
        if (asks_.empty()) {
            return false;
        }

        auto level = asks_.begin();
        Order& order = level->second.front();

        if (quantity > order.remaining_quantity()) {
            return false;
        }

        const OrderId id = order.id();

        if (!order.execute(quantity)) {
            return false;
        }

        if (order.is_filled()) {
            order_index_.erase(id);
            level->second.pop_front();

            if (level->second.empty()) {
                asks_.erase(level);
            }
        }

        return true;
    }

    return false;
}

void OrderBook::remove_filled_order(OrderId id) {
    auto index_it = order_index_.find(id);

    if (index_it == order_index_.end()) {
        throw std::logic_error(
            "Filled order missing from index"
        );
    }

    const OrderLocation location = index_it->second;

    if (location.side == Side::BUY) {
        auto level = bids_.find(location.price);

        level->second.erase(location.iterator);

        if (level->second.empty()) {
            bids_.erase(level);
        }
    } else {
        auto level = asks_.find(location.price);

        level->second.erase(location.iterator);

        if (level->second.empty()) {
            asks_.erase(level);
        }
    }

    order_index_.erase(index_it);
}

bool OrderBook::execute_order(
    OrderId id,
    Quantity quantity
) {
    auto index_it = order_index_.find(id);

    if (index_it == order_index_.end()) {
        return false;
    }

    Order& order = *index_it->second.iterator;

    if (!order.execute(quantity)) {
        return false;
    }

    if (order.is_filled()) {
        remove_filled_order(id);
    }

    return true;
}

bool OrderBook::execute_match(
    OrderId incoming_id,
    OrderId resting_id,
    Quantity quantity
) {
    if (incoming_id == resting_id || quantity == 0) {
        return false;
    }

    auto incoming_it = order_index_.find(incoming_id);
    auto resting_it = order_index_.find(resting_id);

    if (incoming_it == order_index_.end() ||
        resting_it == order_index_.end()) {
        return false;
    }

    Order& incoming = *incoming_it->second.iterator;
    Order& resting = *resting_it->second.iterator;

    const bool incoming_side_valid =
        incoming.side() == Side::BUY ||
        incoming.side() == Side::SELL;

    const bool resting_side_valid =
        resting.side() == Side::BUY ||
        resting.side() == Side::SELL;

    if (!incoming.is_active() ||
        !resting.is_active() ||
        !incoming_side_valid ||
        !resting_side_valid ||
        incoming.side() == resting.side()) {
        return false;
    }

    const Order* expected_resting =
        best_order(resting.side());

    if (expected_resting == nullptr ||
        expected_resting->id() != resting_id) {
        return false;
    }

    // Validate price compatibility.
    const bool crosses =
        incoming.side() == Side::BUY
            ? incoming.price_ticks() >= resting.price_ticks()
            : incoming.price_ticks() <= resting.price_ticks();

    if (!crosses) {
        return false;
    }

    // Validate both quantities before changing either order.
    if (quantity > incoming.remaining_quantity() ||
        quantity > resting.remaining_quantity()) {
        return false;
    }

    // Both executions are now valid.
    const bool incoming_ok = incoming.execute(quantity);
    const bool resting_ok = resting.execute(quantity);

    if (!incoming_ok || !resting_ok) {
        throw std::logic_error(
            "Validated paired execution failed"
        );
    }

    // Remove fully filled orders using the existing
    // indexed removal logic.
    if (incoming.is_filled()) {
        remove_filled_order(incoming_id);
    }

    if (resting.is_filled()) {
        remove_filled_order(resting_id);
    }

    return true;
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }

    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) {
        return std::nullopt;
    }

    return asks_.begin()->first;
}

const Order* OrderBook::best_order(Side side) const {
    if (side == Side::BUY) {
        if (bids_.empty()) {
            return nullptr;
        }

        return &bids_.begin()->second.front();
    }

    if (side == Side::SELL) {
        if (asks_.empty()) {
            return nullptr;
        }

        return &asks_.begin()->second.front();
    }

    return nullptr;
}

const Order* OrderBook::find_order(OrderId id) const {
    auto it = order_index_.find(id);

    if (it == order_index_.end()) {
        return nullptr;
    }

    return &(*it->second.iterator);
}

bool OrderBook::contains(OrderId id) const {
    return order_index_.contains(id);
}

bool OrderBook::was_order_id_used(OrderId id) const {
    return used_order_ids_.contains(id);
}

std::size_t OrderBook::order_count() const {
    return order_index_.size();
}

std::vector<OrderId> OrderBook::order_ids_at_price(
    Side side,
    Price price
) const {
    std::vector<OrderId> result;

    if (side == Side::BUY) {
        auto level = bids_.find(price);

        if (level == bids_.end()) {
            return result;
        }

        for (const auto& order : level->second) {
            result.push_back(order.id());
        }
    } else if (side == Side::SELL) {
        auto level = asks_.find(price);

        if (level == asks_.end()) {
            return result;
        }

        for (const auto& order : level->second) {
            result.push_back(order.id());
        }
    }

    return result;
}

std::size_t OrderBook::count_matchable_orders(
    Side incoming_side,
    Price limit_price,
    Quantity incoming_quantity
) const {
    if (incoming_quantity == 0) {
        return 0;
    }

    std::size_t count = 0;
    Quantity remaining = incoming_quantity;

    if (incoming_side == Side::BUY) {
        for (const auto& [price, orders] : asks_) {
            if (price > limit_price) {
                break;
            }

            for (const Order& resting : orders) {
                ++count;

                const Quantity executable =
                    std::min(
                        remaining,
                        resting.remaining_quantity()
                    );

                remaining -= executable;

                if (remaining == 0) {
                    return count;
                }
            }
        }
    } else if (incoming_side == Side::SELL) {
        for (const auto& [price, orders] : bids_) {
            if (price < limit_price) {
                break;
            }

            for (const Order& resting : orders) {
                ++count;

                const Quantity executable =
                    std::min(
                        remaining,
                        resting.remaining_quantity()
                    );

                remaining -= executable;

                if (remaining == 0) {
                    return count;
                }
            }
        }
    }

    return count;
}

bool OrderBook::validate_invariants() const {
    std::size_t counted_orders = 0;

    const auto validate_side =
        [&](const auto& side_book, Side expected_side) {
            for (const auto& [price, orders] : side_book) {
                if (orders.empty()) {
                    return false;
                }

                for (auto it = orders.begin();
                     it != orders.end(); ++it) {

                    const Order& order = *it;

                    if (order.side() != expected_side ||
                        order.price_ticks() != price ||
                        !order.is_active() ||
                        order.remaining_quantity() == 0) {
                        return false;
                    }

                    auto index_it =
                        order_index_.find(order.id());

                    if (index_it == order_index_.end()) {
                        return false;
                    }

                    const OrderLocation& location =
                        index_it->second;

                    if (location.side != expected_side ||
                        location.price != price ||
                        location.iterator != it) {
                        return false;
                    }

                    if (!used_order_ids_.contains(order.id())) {
                        return false;
                    }

                    ++counted_orders;
                }
            }

            return true;
        };

    if (!validate_side(bids_, Side::BUY) ||
        !validate_side(asks_, Side::SELL)) {
        return false;
    }

    if (counted_orders != order_index_.size()) {
        return false;
    }

    const auto bid = best_bid();
    const auto ask = best_ask();

    if (bid.has_value() &&
        ask.has_value() &&
        *bid >= *ask) {
        return false;
    }

    return true;
}

void OrderBook::print_book() const {
    std::cout << "\n===== BUY BOOK =====\n";

    for (const auto& [price, orders] : bids_) {
        std::cout << "Price: " << price << " | Orders: ";

        for (const auto& order : orders) {
            std::cout << order.id() << " ";
        }

        std::cout << '\n';
    }

    std::cout << "\n===== SELL BOOK =====\n";

    for (const auto& [price, orders] : asks_) {
        std::cout << "Price: " << price << " | Orders: ";

        for (const auto& order : orders) {
            std::cout << order.id() << " ";
        }

        std::cout << '\n';
    }
}

} // namespace matching_engine
