#pragma once

#include "matching_engine/Types.hpp"

namespace matching_engine {

class Order {
private:
    OrderId order_id_;
    Side side_;
    Price price_ticks_;

    Quantity original_quantity_;
    Quantity remaining_quantity_;

public:
    Order(
        OrderId id,
        Side side,
        Price price_ticks,
        Quantity quantity
    );

    OrderId id() const;
    Side side() const;
    Price price_ticks() const;

    Quantity original_quantity() const;
    Quantity remaining_quantity() const;

    Quantity executed_quantity() const;

    bool execute(Quantity quantity);
    bool is_filled() const;
};

} // namespace matching_engine
