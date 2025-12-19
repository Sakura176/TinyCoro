#pragma once

#include <atomic>
#include <cstdint>

#include "coro/comp/event.hpp"

namespace coro
{
/**
 * @brief Welcome to tinycoro lab4b, in this part you will build the basic coroutine
 * synchronization component - latch by modifing latch.hpp and latch.cpp. Please ensure
 * you have read the document of lab4b.
 *
 * @warning You should carefully consider whether each implementation should be thread-safe.
 *
 * You should follow the rules below in this part:
 *
 * @note The location marked by todo is where you must add code, but you can also add code anywhere
 * you want, such as function and class definitions, even member variables.
 *
 * @note lab4 and lab5 are free designed lab, leave the interfaces that the test case will use,
 * and then, enjoy yourself!
 */

// TODO[lab4b]: This latch is an example to make complie success,
// You should delete it and add your implementation, I don't care what you do,
// but keep the function count_down() and wait()'s declaration same with example.

/**
 * @breif 倒计数器同步原语，归0时唤醒所有等待的协程
 * */
class latch
{
public:
    using event_t = event<>;
    latch(std::uint64_t count) noexcept : m_count(count) {}
    latch(const latch&)                    = delete;
    latch(latch&&)                         = delete;
    auto operator=(const latch&) -> latch& = delete;
    auto operator=(latch&&) -> latch&      = delete;

    // 计数器减1，为0时恢复
    auto count_down() noexcept -> void
    {
        // 计数减1并获取旧值，判断是否需要恢复
        if (m_count.fetch_sub(1, std::memory_order_acq_rel) <= 1)
        {
            // resume所有awaiter
            m_ev.set();
        }
    }

    // 计数器大于0则挂起
    auto wait() noexcept -> event_t::awaiter { return m_ev.wait(); }

private:
    std::atomic<uint64_t> m_count;
    event_t               m_ev;
};

/**
 * @brief RAII for latch
 *
 */
class latch_guard
{
public:
    latch_guard(latch& l) noexcept : m_l(l) {}
    ~latch_guard() noexcept { m_l.count_down(); }

private:
    latch& m_l;
};

}; // namespace coro
