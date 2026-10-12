#include <iostream>

#include "matching_engine/Order.hpp"

int main() {
    using namespace matching_engine;

    Order order(
        101,
        Side::BUY,
        10250,
        500
    );

    std::cout << "Original: "
              << order.original_quantity() << '\n';

    bool first_trade = order.execute(200);

    std::cout << "Trade 1 accepted: "
              << std::boolalpha << first_trade << '\n';

    std::cout << "Remaining: "
              << order.remaining_quantity() << '\n';

    bool invalid_trade = order.execute(400);

    std::cout << "Trade 2 accepted: "
              << invalid_trade << '\n';

    std::cout << "Remaining: "
              << order.remaining_quantity() << '\n';

    bool final_trade = order.execute(300);

    std::cout << "Trade 3 accepted: "
              << final_trade << '\n';

    std::cout << "Remaining: "
              << order.remaining_quantity() << '\n';

    std::cout << "Filled: "
              << order.is_filled() << '\n';

    return 0;
}
