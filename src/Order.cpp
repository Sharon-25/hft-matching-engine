#include "matching_engine/Order.hpp"

#include <stdexcept>

namespace matching_engine {

Order::Order(
    OrderId id,
    Side side,
    Price price_ticks,
    Quantity quantity
)
    : Order(
          id,
          side,
          OrderType::LIMIT,
          price_ticks,
          quantity
      ) {
}

Order::Order(
    OrderId id,
    Side side,
    OrderType type,
    Price price_ticks,
    Quantity quantity
)
    : order_id_(id),
      side_(side),
      type_(type),
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

    if (side != Side::BUY && side != Side::SELL) {
        throw std::invalid_argument(
            "Invalid order side"
        );
    }

    if (quantity == 0) {
        throw std::invalid_argument(
            "Order quantity must be positive"
        );
    }

    if (type == OrderType::LIMIT) {
        if (price_ticks <= 0) {
            throw std::invalid_argument(
                "Limit order price must be positive"
            );
        }
    }
    else if (type == OrderType::MARKET) {
        if (price_ticks != 0) {
            throw std::invalid_argument(
                "Market orders must not specify a price"
            );
        }
    }
    else {
        throw std::invalid_argument(
            "Invalid order type"
        );
    }
}

OrderId Order::id() const noexcept {
    return order_id_;
}

Side Order::side() const noexcept {
    return side_;
}

OrderType Order::type() const noexcept {
    return type_;
}

Price Order::price_ticks() const noexcept {
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
