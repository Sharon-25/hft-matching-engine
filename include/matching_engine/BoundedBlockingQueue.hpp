#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace matching_engine {

template <typename T>
class BoundedBlockingQueue {
    static_assert(
        std::is_nothrow_move_constructible_v<T>,
        "Queue payload must be nothrow move constructible"
    );

private:
    const std::size_t capacity_;

    std::deque<T> queue_;

    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;

    bool closed_ = false;

#ifdef MATCHING_ENGINE_TESTING
    std::condition_variable test_waiting_cv_;
    std::size_t test_waiting_producers_ = 0;
#endif

public:
    explicit BoundedBlockingQueue(std::size_t capacity)
        : capacity_(capacity) {
        if (capacity == 0) {
            throw std::invalid_argument(
                "Queue capacity must be positive"
            );
        }
    }

    BoundedBlockingQueue(const BoundedBlockingQueue&) = delete;
    BoundedBlockingQueue& operator=(
        const BoundedBlockingQueue&
    ) = delete;

    bool push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);

#ifdef MATCHING_ENGINE_TESTING
        if (!closed_ && queue_.size() >= capacity_) {
            ++test_waiting_producers_;
            test_waiting_cv_.notify_all();

            not_full_.wait(lock, [this] {
                return closed_ || queue_.size() < capacity_;
            });

            --test_waiting_producers_;
        } else {
            not_full_.wait(lock, [this] {
                return closed_ || queue_.size() < capacity_;
            });
        }
#else
        not_full_.wait(lock, [this] {
            return closed_ || queue_.size() < capacity_;
        });
#endif

        if (closed_) {
            return false;
        }

        queue_.push_back(std::move(item));

        lock.unlock();
        not_empty_.notify_one();

        return true;
    }

#ifdef MATCHING_ENGINE_TESTING
    void wait_for_blocked_producers_for_test(
        std::size_t expected_count
    ) {
        std::unique_lock<std::mutex> lock(mutex_);

        test_waiting_cv_.wait(lock, [this, expected_count] {
            return test_waiting_producers_ >= expected_count;
        });
    }
#endif

    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);

        not_empty_.wait(lock, [this] {
            return closed_ || !queue_.empty();
        });

        if (queue_.empty()) {
            return std::nullopt;
        }

        std::optional<T> result(
            std::in_place,
            std::move(queue_.front())
        );

        queue_.pop_front();

        lock.unlock();
        not_full_.notify_one();

        return result;
    }

    void close() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }

        not_empty_.notify_all();
        not_full_.notify_all();
    }
};

} // namespace matching_engine
