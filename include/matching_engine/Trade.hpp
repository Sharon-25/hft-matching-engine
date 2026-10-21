#pragma once

#include "matching_engine/Types.hpp"

namespace matching_engine {

struct Trade {
    OrderId incoming_order_id;
    OrderId resting_order_id;

    Price execution_price_ticks;
    Quantity executed_quantity;
};

} // namespace matching_engine
