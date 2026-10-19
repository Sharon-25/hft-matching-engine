#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "matching_engine/Order.hpp"

namespace matching_engine {

class OrderBook {
private:
    using OrderList = std::list<Order>;

    using BidBook = std::map<
        Price,
        OrderList,
        std::greater<Price>
    >;

    using AskBook = std::map<
        Price,
        OrderList,
        std::less<Price>
    >;

    struct OrderLocation {
        Side side;
        Price price;
        OrderList::iterator iterator;
    };

    BidBook bids_;
    AskBook asks_;
    std::unordered_map<OrderId, OrderLocation> order_index_;
    std::unordered_set<OrderId> used_order_ids_;

    template <typename BookType>
    void insert_into_side(
        BookType& side_book,
        const Order& order
    );

public:
    OrderBook() = default;

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    OrderBook(OrderBook&&) = delete;
    OrderBook& operator=(OrderBook&&) = delete;

    bool add_order(const Order& order);
    bool cancel_order(OrderId id);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;

    bool contains(OrderId id) const;
    bool was_order_id_used(OrderId id) const;
    std::size_t order_count() const;

    std::vector<OrderId> order_ids_at_price(
        Side side,
        Price price
    ) const;

    void print_book() const;
};

} // namespace matching_engine
