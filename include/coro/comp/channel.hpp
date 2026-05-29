/**
 * @file channel.hpp
 * @author JiahuiWang
 * @brief lab5c
 * @version 1.0
 * @date 2025-03-24
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <optional>

#include "coro/comp/condition_variable.hpp"
#include "coro/comp/mutex.hpp"
#include "coro/concepts/common.hpp"
#include "coro/task.hpp"

namespace coro
{
/**
 * @brief Welcome to tinycoro lab5c, in this part you will build the basic coroutine
 * synchronization component����channel by modifing channel.hpp and channel.cpp.
 * Please ensure you have read the document of lab5c.
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
namespace detail
{
// TODO[lab5c]: Add code that you don't want to use externally in namespace detail
}; // namespace detail

// TODO[lab5c]: This channel is an example to make complie success,
// You should delete it and add your implementation, I don't care what you do,
// but keep the member function and construct function's declaration same with example.
template<concepts::conventional_type T, size_t capacity = 1>
class channel
{
    using data_type = std::optional<T>;

    mutex                   m_mtx;
    condition_variable      m_send_cv;
    condition_variable      m_recv_cv;
    std::array<T, capacity> m_buffer;
    size_t                  m_head{0};
    size_t                  m_tail{0};
    size_t                  m_count{0};
    bool                    m_closed{false};

public:
    template<typename value_type>
        requires(std::is_constructible_v<T, value_type &&>)
    auto send(value_type&& value) noexcept -> task<bool>
    {
        auto guard = co_await m_mtx.lock_guard();
        while (m_count >= capacity && !m_closed)
        {
            co_await m_send_cv.wait(m_mtx);
        }
        if (m_closed)
        {
            m_send_cv.notify_all();
            co_return false;
        }
        m_buffer[m_tail] = std::move(value);
        m_tail           = (m_tail + 1) % capacity;
        m_count++;
        m_recv_cv.notify_one();
        co_return true;
    }

    auto recv() noexcept -> task<data_type>
    {
        auto guard = co_await m_mtx.lock_guard();
        while (m_count == 0 && !m_closed)
            co_await m_recv_cv.wait(m_mtx);
        if (m_count == 0 && m_closed)
        {
            m_recv_cv.notify_all();
            co_return std::nullopt;
        }
        auto val = std::move(m_buffer[m_head]);
        m_head   = (m_head + 1) % capacity;
        m_count--;
        m_send_cv.notify_one();
        co_return std::optional<T>(std::move(val));
    }

    auto close() noexcept -> void
    {
        // 协作式调度器中，m_closed 赋值在两次 co_await 之间执行
        // 不存在并发打断，无需持锁
        m_closed = true;
        m_send_cv.notify_all();
        m_recv_cv.notify_all();
    }
};

}; // namespace coro
