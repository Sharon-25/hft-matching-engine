#pragma once

#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "matching_engine/Order.hpp"

#ifdef MATCHING_ENGINE_TESTING
#include "matching_engine/TestingAllocator.hpp"
#endif

namespace matching_engine {

#ifdef MATCHING_ENGINE_TESTING
enum class InsertionFailurePoint {
    NONE,
    AFTER_LIST_INSERT,
    AFTER_INDEX_INSERT,
    AFTER_USED_ID_INSERT
};
#endif

class OrderBook {
private:
#ifdef MATCHING_ENGINE_TESTING
    template <typename T>
    using BookAllocator = testing::TestingAllocator<T>;
#else
    template <typename T>
    using BookAllocator = std::allocator<T>;
#endif

    using OrderList = std::list<
        Order,
        BookAllocator<Order>
    >;

    using PriceLevel = std::pair<const Price, OrderList>;

    using BidBook = std::map<
        Price,
        OrderList,
        std::greater<Price>,
        BookAllocator<PriceLevel>
    >;

    using AskBook = std::map<
        Price,
        OrderList,
        std::less<Price>,
        BookAllocator<PriceLevel>
    >;

    struct OrderLocation {
        Side side;
        Price price;
        OrderList::iterator iterator;
    };

    using OrderIndex = std::unordered_map<
        OrderId,
        OrderLocation,
        std::hash<OrderId>,
        std::equal_to<OrderId>,
        BookAllocator<
            std::pair<const OrderId, OrderLocation>
        >
    >;

    using UsedOrderIds = std::unordered_set<
        OrderId,
        std::hash<OrderId>,
        std::equal_to<OrderId>,
        BookAllocator<OrderId>
    >;

    BidBook bids_;
    AskBook asks_;
    OrderIndex order_index_;
    UsedOrderIds used_order_ids_;

    template <typename BookType>
    void insert_into_side(
        BookType& side_book,
        const Order& order
    ) {
        auto [level, created] =
            side_book.try_emplace(order.price_ticks());

        bool list_inserted = false;
        bool index_inserted = false;
        bool used_id_inserted = false;

        try {
            level->second.push_back(order);
            list_inserted = true;

#ifdef MATCHING_ENGINE_TESTING
            maybe_fail_insertion(
                InsertionFailurePoint::AFTER_LIST_INSERT
            );
#endif

            auto order_it = std::prev(level->second.end());

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

            index_inserted = true;

#ifdef MATCHING_ENGINE_TESTING
            maybe_fail_insertion(
                InsertionFailurePoint::AFTER_INDEX_INSERT
            );
#endif

            auto [used_it, newly_used] =
                used_order_ids_.insert(order.id());

            if (!newly_used) {
                throw std::logic_error(
                    "Previously used order ID"
                );
            }

            used_id_inserted = true;

#ifdef MATCHING_ENGINE_TESTING
            maybe_fail_insertion(
                InsertionFailurePoint::AFTER_USED_ID_INSERT
            );
#endif

        } catch (...) {
            if (used_id_inserted) {
                used_order_ids_.erase(order.id());
            }

            if (index_inserted) {
                order_index_.erase(order.id());
            }

            if (list_inserted) {
                level->second.pop_back();
            }

            if (created && level->second.empty()) {
                side_book.erase(level);
            }

            throw;
        }
    }

    friend class MatchingEngine;

    bool add_active_order(const Order& order);
    bool execute_order(OrderId id, Quantity quantity);
    bool execute_match(
        OrderId incoming_id,
        OrderId resting_id,
        Quantity quantity
    );
    void remove_filled_order(OrderId id);

#ifdef MATCHING_ENGINE_TESTING
    InsertionFailurePoint failure_point_ =
        InsertionFailurePoint::NONE;

    void maybe_fail_insertion(InsertionFailurePoint point);
#endif

public:
    OrderBook() = default;

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    OrderBook(OrderBook&&) = delete;
    OrderBook& operator=(OrderBook&&) = delete;

    bool add_order(const Order& order);
    bool cancel_order(OrderId id);
    bool execute_best_order(Side side, Quantity quantity);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;

    const Order* best_order(Side side) const;
    const Order* find_order(OrderId id) const;

#ifdef MATCHING_ENGINE_TESTING
    void set_insertion_failure_for_test(
        InsertionFailurePoint point
    ) noexcept {
        failure_point_ = point;
    }
#endif

    bool contains(OrderId id) const;
    bool was_order_id_used(OrderId id) const;
    std::size_t order_count() const;

    std::size_t count_matchable_orders(
        Side incoming_side,
        Price limit_price,
        Quantity incoming_quantity
    ) const;

    bool validate_invariants() const;

    std::vector<OrderId> order_ids_at_price(
        Side side,
        Price price
    ) const;

    void print_book() const;
};

} // namespace matching_engine
