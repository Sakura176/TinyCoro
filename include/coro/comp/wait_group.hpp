/**
 * @file wait_group.hpp
 * @author JiahuiWang
 * @brief lab4c
 * @version 1.0
 * @date 2025-03-24
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <atomic>
#include <coroutine>

#include "coro/comp/event.hpp"
#include "coro/detail/types.hpp"

namespace coro
{
/**
 * @brief Welcome to tinycoro lab4c, in this part you will build the basic coroutine
 * synchronization component——wait_group by modifing wait_group.hpp and wait_group.cpp.
 * Please ensure you have read the document of lab4c.
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

class context;

// TODO[lab4c]: This wait_group is an example to make complie success,
// You should delete it and add your implementation, I don't care what you do,
// but keep the member function and construct function's declaration same with example.
class wait_group
{
public:
    using event_t = event<>;
    explicit wait_group(uint64_t count = 0) noexcept : m_count(count) {}

    auto add(int count) noexcept -> void { m_count.fetch_add(count, std::memory_order_acquire); };

    auto done() noexcept -> void
    {
        if (m_count.fetch_sub(1, std::memory_order_acq_rel) <= 1)
        {
            m_ev.set();
        }
    };

    auto wait() noexcept -> event_t::awaiter { return m_ev.wait(); };

private:
    std::atomic<uint64_t> m_count;
    event_t               m_ev;
};

}; // namespace coro
