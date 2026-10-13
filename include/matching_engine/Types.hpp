#pragma once

#include <cstdint>

namespace matching_engine {

using OrderId = std::uint64_t;
using Quantity = std::uint64_t;
using Price = std::int64_t;

enum class Side {
    BUY,
    SELL
};

enum class OrderType {
    LIMIT,
    MARKET
};

enum class OrderStatus {
    NEW,
    PARTIALLY_FILLED,
    FILLED,
    CANCELLED
};

} // namespace matching_engine
