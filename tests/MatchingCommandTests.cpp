#include <cstdio>
#include <cstdlib>
#include <future>
#include <type_traits>
#include <utility>

#include "matching_engine/BoundedBlockingQueue.hpp"
#include "matching_engine/MatchingCommand.hpp"

using namespace matching_engine;

#define CHECK(condition)                                      \
    do {                                                      \
        if (!(condition)) {                                   \
            std::fprintf(                                     \
                stderr,                                       \
                "CHECK failed: %s (%s:%d)\n",                  \
                #condition, __FILE__, __LINE__                \
            );                                                \
            std::abort();                                     \
        }                                                     \
    } while (false)

void test_command_moves_through_queue() {
    BoundedBlockingQueue<MatchingCommand> queue(2);

    Order order(
        1001,
        Side::BUY,
        10500,
        10
    );

    std::promise<SubmissionResult> promise;
    auto future = promise.get_future();

    MatchingCommand command(
        std::move(order),
        std::move(promise)
    );

    CHECK(queue.push(std::move(command)));

    auto received = queue.pop();

    CHECK(received.has_value());
    CHECK(received->order.id() == 1001);
    CHECK(received->order.side() == Side::BUY);

    received->result_promise.set_value({
        SubmissionStatus::ACCEPTED,
        0,
        10
    });

    const auto result = future.get();

    CHECK(result.status == SubmissionStatus::ACCEPTED);
    CHECK(result.executed_quantity == 0);
    CHECK(result.remaining_quantity == 10);
}

void test_command_is_move_only() {
    static_assert(
        !std::is_copy_constructible_v<MatchingCommand>
    );

    static_assert(
        std::is_nothrow_move_constructible_v<MatchingCommand>
    );
}

int main() {
    test_command_is_move_only();
    test_command_moves_through_queue();

    std::printf("MatchingCommand tests passed\n");

    return 0;
}
