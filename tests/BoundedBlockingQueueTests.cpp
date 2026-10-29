#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include "matching_engine/BoundedBlockingQueue.hpp"

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

using matching_engine::BoundedBlockingQueue;

void test_zero_capacity_rejected() {
    bool threw = false;

    try {
        BoundedBlockingQueue<int> queue(0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    CHECK(threw);
}

void test_fifo_order() {
    BoundedBlockingQueue<int> queue(3);

    CHECK(queue.push(10));
    CHECK(queue.push(20));
    CHECK(queue.push(30));

    auto first = queue.pop();
    auto second = queue.pop();
    auto third = queue.pop();

    CHECK(first.has_value());
    CHECK(second.has_value());
    CHECK(third.has_value());

    CHECK(*first == 10);
    CHECK(*second == 20);
    CHECK(*third == 30);
}

void test_close_rejects_new_items() {
    BoundedBlockingQueue<int> queue(2);

    queue.close();

    CHECK(!queue.push(10));
    CHECK(!queue.pop().has_value());
}

void test_close_drains_existing_items() {
    BoundedBlockingQueue<int> queue(3);

    CHECK(queue.push(10));
    CHECK(queue.push(20));

    queue.close();

    CHECK(!queue.push(30));

    auto first = queue.pop();
    auto second = queue.pop();
    auto third = queue.pop();

    CHECK(first.has_value());
    CHECK(second.has_value());
    CHECK(!third.has_value());

    CHECK(*first == 10);
    CHECK(*second == 20);
}

void test_close_is_idempotent() {
    BoundedBlockingQueue<int> queue(1);

    CHECK(queue.push(42));

    queue.close();
    queue.close();

    auto item = queue.pop();

    CHECK(item.has_value());
    CHECK(*item == 42);
    CHECK(!queue.pop().has_value());
}

void test_producer_blocks_when_full() {
    BoundedBlockingQueue<int> queue(1);

    CHECK(queue.push(10));

    std::promise<void> producer_started;
    auto started = producer_started.get_future();

    std::promise<bool> producer_result;
    auto result = producer_result.get_future();

    std::thread producer([&] {
        producer_started.set_value();
        producer_result.set_value(queue.push(20));
    });

    started.wait();

    // The queue is full. The producer cannot complete
    // successfully until the consumer removes an item.
    CHECK(result.wait_for(std::chrono::milliseconds(50)) ==
          std::future_status::timeout);

    auto first = queue.pop();
    CHECK(first.has_value());
    CHECK(*first == 10);

    CHECK(result.wait_for(std::chrono::seconds(5)) ==
          std::future_status::ready);
    CHECK(result.get());

    producer.join();

    auto second = queue.pop();
    CHECK(second.has_value());
    CHECK(*second == 20);
}

void test_consumer_blocks_when_empty() {
    BoundedBlockingQueue<int> queue(1);

    std::promise<void> consumer_started;
    auto started = consumer_started.get_future();

    std::promise<std::optional<int>> consumer_result;
    auto result = consumer_result.get_future();

    std::thread consumer([&] {
        consumer_started.set_value();
        consumer_result.set_value(queue.pop());
    });

    started.wait();

    CHECK(result.wait_for(std::chrono::milliseconds(50)) ==
          std::future_status::timeout);

    CHECK(queue.push(42));

    CHECK(result.wait_for(std::chrono::seconds(5)) ==
          std::future_status::ready);

    auto item = result.get();
    CHECK(item.has_value());
    CHECK(*item == 42);

    consumer.join();
}

void test_close_wakes_blocked_producer() {
    BoundedBlockingQueue<int> queue(1);

    CHECK(queue.push(10));

    std::promise<bool> producer_result;
    auto result = producer_result.get_future();

    std::thread producer([&] {
        producer_result.set_value(queue.push(20));
    });

#ifdef MATCHING_ENGINE_TESTING
    queue.wait_for_blocked_producers_for_test(1);
#endif

    queue.close();

    CHECK(result.wait_for(std::chrono::seconds(5)) ==
          std::future_status::ready);

    CHECK(!result.get());

    producer.join();

    auto remaining = queue.pop();

    CHECK(remaining.has_value());
    CHECK(*remaining == 10);
    CHECK(!queue.pop().has_value());
}

void test_close_wakes_blocked_consumer() {
    BoundedBlockingQueue<int> queue(1);

    std::promise<void> consumer_started;
    auto started = consumer_started.get_future();

    std::promise<std::optional<int>> consumer_result;
    auto result = consumer_result.get_future();

    std::thread consumer([&] {
        consumer_started.set_value();
        consumer_result.set_value(queue.pop());
    });

    started.wait();

    queue.close();

    CHECK(result.wait_for(std::chrono::seconds(5)) ==
          std::future_status::ready);
    CHECK(!result.get().has_value());

    consumer.join();
}

void test_multiple_producers_no_loss_or_duplicates() {
    constexpr int producer_count = 4;
    constexpr int items_per_producer = 250;
    constexpr int total_items =
        producer_count * items_per_producer;

    BoundedBlockingQueue<int> queue(16);

    std::vector<int> received;
    received.reserve(total_items);

    std::thread consumer([&] {
        while (auto item = queue.pop()) {
            received.push_back(*item);
        }
    });

    std::vector<std::thread> producers;

    for (int producer_id = 0;
         producer_id < producer_count;
         ++producer_id) {
        producers.emplace_back([&, producer_id] {
            for (int i = 0; i < items_per_producer; ++i) {
                const int value =
                    producer_id * items_per_producer + i;

                if (!queue.push(value)) {
                    std::abort();
                }
            }
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }

    queue.close();
    consumer.join();

    // Each producer submits its own sequence in order.
    // Interleaving between different producers is allowed.
    std::vector<int> last_seen(producer_count, -1);

    for (int value : received) {
        const int producer_id = value / items_per_producer;
        const int sequence = value % items_per_producer;

        CHECK(producer_id >= 0);
        CHECK(producer_id < producer_count);

        CHECK(sequence == last_seen[producer_id] + 1);

        last_seen[producer_id] = sequence;
    }

    CHECK(received.size() ==
          static_cast<std::size_t>(total_items));

    std::sort(received.begin(), received.end());

    for (int i = 0; i < total_items; ++i) {
        CHECK(received[static_cast<std::size_t>(i)] == i);
    }
}

int main() {
    test_zero_capacity_rejected();
    test_fifo_order();
    test_close_rejects_new_items();
    test_close_drains_existing_items();
    test_close_is_idempotent();
    test_producer_blocks_when_full();
    test_consumer_blocks_when_empty();
    test_close_wakes_blocked_producer();
    test_close_wakes_blocked_consumer();
    test_multiple_producers_no_loss_or_duplicates();

    std::printf("BoundedBlockingQueue tests passed\n");

    return 0;
}
