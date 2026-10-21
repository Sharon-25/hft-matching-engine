#pragma once

#include "matching_engine/Types.hpp"

namespace matching_engine {

class Order {
private:
    OrderId order_id_;
    Side side_;
    OrderType type_;
    Price price_ticks_;

    Quantity original_quantity_;
    Quantity remaining_quantity_;
    OrderStatus status_;

public:
    Order(
        OrderId id,
        Side side,
        Price price_ticks,
        Quantity quantity
    );

    Order(
        OrderId id,
        Side side,
        OrderType type,
        Price price_ticks,
        Quantity quantity
    );

    OrderId id() const noexcept;
    Side side() const noexcept;
    OrderType type() const noexcept;
    Price price_ticks() const noexcept;

    Quantity original_quantity() const;
    Quantity remaining_quantity() const;

    Quantity executed_quantity() const;

    bool execute(Quantity quantity);
    bool is_filled() const;

    OrderStatus status() const;
    bool cancel();
    bool is_active() const;
};

} // namespace matching_engine
