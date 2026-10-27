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

        not_full_.wait(lock, [this] {
            return closed_ || queue_.size() < capacity_;
        });

        if (closed_) {
            return false;
        }

        queue_.push_back(std::move(item));

        lock.unlock();
        not_empty_.notify_one();

        return true;
    }

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
