#include "matching_engine/OrderBook.hpp"

#include <iostream>
#include <iterator>
#include <stdexcept>

namespace matching_engine {

template <typename BookType>
void OrderBook::insert_into_side(
    BookType& side_book,
    const Order& order
) {
    auto [level, created] =
        side_book.try_emplace(order.price_ticks());

    try {
        level->second.push_back(order);

        auto order_it = std::prev(level->second.end());

        try {
            auto [index_it, inserted] =
                order_index_.emplace(
                    order.id(),
                    OrderLocation{
                        order.side(),
                        order.price_ticks(),
                        order_it
                    }
                );

            if (!inserted) {
                throw std::logic_error(
                    "Duplicate active order ID"
                );
            }

            try {
                auto [used_it, newly_used] =
                    used_order_ids_.insert(order.id());

                if (!newly_used) {
                    throw std::logic_error(
                        "Previously used order ID"
                    );
                }
            } catch (...) {
                order_index_.erase(index_it);
                throw;
            }
        } catch (...) {
            level->second.pop_back();
            throw;
        }
    } catch (...) {
        if (created && level->second.empty()) {
            side_book.erase(level);
        }
        throw;
    }
}

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
