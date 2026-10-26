#pragma once

#ifdef MATCHING_ENGINE_TESTING

#include <cstddef>
#include <limits>
#include <memory>
#include <new>

namespace matching_engine::testing {

class AllocationController {
private:
    inline static std::size_t attempts_ = 0;
    inline static std::size_t fail_at_ =
        std::numeric_limits<std::size_t>::max();
    inline static bool fail_trade_history_ = false;

public:
    static void reset() noexcept {
        attempts_ = 0;
        fail_at_ = std::numeric_limits<std::size_t>::max();
        fail_trade_history_ = false;
    }

    static void fail_on(std::size_t attempt) noexcept {
        attempts_ = 0;
        fail_at_ = attempt;
    }

    static void fail_on_trade_history() noexcept {
        attempts_ = 0;
        fail_at_ = std::numeric_limits<std::size_t>::max();
        fail_trade_history_ = true;
    }

    static std::size_t attempts() noexcept {
        return attempts_;
    }

    static void before_allocate() {
        ++attempts_;

        if (attempts_ == fail_at_) {
            fail_at_ = std::numeric_limits<std::size_t>::max();
            throw std::bad_alloc{};
        }
    }

    static void before_trade_history_reserve() {
        if (fail_trade_history_) {
            fail_trade_history_ = false;
            throw std::bad_alloc{};
        }

        before_allocate();
    }
};

template <typename T>
class TestingAllocator {
public:
    using value_type = T;

    TestingAllocator() noexcept = default;

    template <typename U>
    TestingAllocator(const TestingAllocator<U>&) noexcept {}

    [[nodiscard]]
    T* allocate(std::size_t count) {
        AllocationController::before_allocate();
        return std::allocator<T>{}.allocate(count);
    }

    void deallocate(T* ptr, std::size_t count) noexcept {
        std::allocator<T>{}.deallocate(ptr, count);
    }

    template <typename U>
    bool operator==(const TestingAllocator<U>&) const noexcept {
        return true;
    }
};

} // namespace matching_engine::testing

#endif
