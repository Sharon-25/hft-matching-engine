#include "matching_engine/Order.hpp"

#include <stdexcept>

namespace matching_engine {

Order::Order(
    OrderId id,
    Side side,
    Price price_ticks,
    Quantity quantity
)
    : order_id_(id),
      side_(side),
      price_ticks_(price_ticks),
      original_quantity_(quantity),
      remaining_quantity_(quantity),
      status_(OrderStatus::NEW)
{
    if (id == 0) {
        throw std::invalid_argument(
            "Order ID must be positive"
        );
    }

    if (price_ticks <= 0) {
        throw std::invalid_argument(
            "Limit price must be positive"
        );
    }

    if (quantity == 0) {
        throw std::invalid_argument(
            "Order quantity must be positive"
        );
    }
}

OrderId Order::id() const {
    return order_id_;
}

Side Order::side() const {
    return side_;
}

Price Order::price_ticks() const {
    return price_ticks_;
}

Quantity Order::original_quantity() const {
    return original_quantity_;
}

Quantity Order::remaining_quantity() const {
    return remaining_quantity_;
}

Quantity Order::executed_quantity() const {
    return original_quantity_ - remaining_quantity_;
}

bool Order::execute(Quantity quantity) {
    if (!is_active() ||
        quantity == 0 ||
        quantity > remaining_quantity_) {
        return false;
    }

    remaining_quantity_ -= quantity;

    if (remaining_quantity_ == 0) {
        status_ = OrderStatus::FILLED;
    } else {
        status_ = OrderStatus::PARTIALLY_FILLED;
    }

    return true;
}

bool Order::is_filled() const {
    return status_ == OrderStatus::FILLED;
}

OrderStatus Order::status() const {
    return status_;
}

bool Order::is_active() const {
    return status_ == OrderStatus::NEW ||
           status_ == OrderStatus::PARTIALLY_FILLED;
}

bool Order::cancel() {
    if (!is_active()) {
        return false;
    }

    status_ = OrderStatus::CANCELLED;
    return true;
}

} // namespace matching_engine
