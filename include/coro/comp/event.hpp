/**
 * @file event.hpp
 * @author JiahuiWang
 * @brief lab4a
 * @version 1.0
 * @date 2025-03-24
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once
#include <algorithm>
#include <atomic>
#include <coroutine>
#include <vector>

#include "coro/attribute.hpp"
#include "coro/comp/when_all.hpp"
#include "coro/concepts/awaitable.hpp"
#include "coro/context.hpp"
#include "coro/detail/container.hpp"
#include "coro/detail/types.hpp"

namespace coro
{
/**
 * @brief Welcome to tinycoro lab4a, in this part you will build the basic coroutine
 * synchronization component - event by modifing event.hpp and event.cpp. Please ensure
 * you have read the document of lab4a.
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

namespace detail
{
// TODO[lab4a]: Add code that you don't want to use externally in namespace detail
}; // namespace detail

class event_base
{
public:
    struct awaiter_base
    {
        awaiter_base(context& ctx, event_base& ev) : m_ctx(ctx), m_ev(ev) {}
        auto await_ready() -> bool
        {
            m_ctx.register_wait();
            return m_ev.is_set();
        }
        auto await_suspend(std::coroutine_handle<> handle) -> bool
        {
            m_await_coro = handle;
            return m_ev.register_awaiter(this);
        }
        auto await_resume() -> void { m_ctx.unregister_wait(); }

        context&                m_ctx;
        event_base&             m_ev;
        awaiter_base*           m_next{nullptr};
        std::coroutine_handle<> m_await_coro{nullptr};
    };

    void set_state() noexcept
    {
        auto flag = m_state.exchange(this, std::memory_order_acq_rel);
        if (flag != this)
        {
            auto waiter = static_cast<awaiter_base*>(flag);
            resume_all_awaiter(waiter);
        }
    }
    auto is_set() const noexcept -> bool;

    auto resume_all_awaiter(detail::awaiter_ptr waiter) noexcept -> void;

    auto register_awaiter(awaiter_base* waiter) noexcept -> bool;

private:
    std::atomic<awaiter_ptr> m_state{nullptr};
};

// TODO[lab4a]: This event is an example to make complie success,
// You should delete it and add your implementation, I don't care what you do,
// but keep the function set() and wait()'s declaration same with example.
template<typename return_type = void>
class event : public detail::container<return_type>
{
    // Just make compile success
    struct awaiter
    {
        awaiter(context& ctx, event& ev) : m_ctx(ctx), m_ev(ev) {}
        auto await_ready() -> bool
        {
            m_ctx.register_wait();
            return m_ev.is_set();
        }
        auto await_suspend(std::coroutine_handle<> handle) -> bool { return false; }
        auto await_resume() -> return_type { return {}; }

        context& m_ctx;
        event&   m_ev;
    };

    void set_state() noexcept { auto flag = m_state.exchange(this, std::memory_order_acquire); }

public:
    auto wait() noexcept -> awaiter { return awaiter{local_context(), *this}; } // return awaitable

    template<typename value_type>
    auto set(value_type&& value) noexcept -> void
    {
        this->return_value(std::forward<value_type>(value));
        set_state();
    }

    inline auto is_set() const noexcept -> bool { return m_state.load(std::memory_order_acquire) == this; }

private:
    std::atomic<detail::awaiter_ptr> m_state{nullptr};
};

template<>
class event<>
{
    struct awaiter
    {
        awaiter(context& ctx, event& ev) : m_ctx(ctx), m_ev(ev) {}
        auto await_ready() -> bool
        {
            m_ctx.register_wait();
            return m_ev.is_set();
        }
        auto await_suspend(std::coroutine_handle<> handle) -> bool
        {
            m_await_coro = handle;
            return m_ev.register_awaiter(this);
        }
        auto await_resume() -> void { m_ctx.unregister_wait(); }

        context&                m_ctx;
        event&                  m_ev;
        awaiter*                m_next{nullptr};
        std::coroutine_handle<> m_await_coro{nullptr};
    };

    void set_state() noexcept
    {
        auto flag = m_state.exchange(this, std::memory_order_acq_rel);
        if (flag != this)
        {
            auto waiter = static_cast<awaiter*>(flag);
            resume_all_awaiter(waiter);
        }
    }

public:
    auto        wait() noexcept -> awaiter { return awaiter{local_context(), *this}; } // return awaitable
    auto        set() noexcept -> void { set_state(); }
    inline auto is_set() const noexcept -> bool { return m_state.load(std::memory_order_acquire) == this; }

    auto resume_all_awaiter(detail::awaiter_ptr waiter) noexcept -> void
    {
        while (waiter != nullptr)
        {
            auto cur = static_cast<awaiter*>(waiter);
            cur->m_ctx.submit_task(cur->m_await_coro);
            waiter = cur->m_next;
        }
    }

    auto register_awaiter(awaiter* waiter) noexcept -> bool
    {
        const auto          set_state = this;
        detail::awaiter_ptr old_value = nullptr;

        do
        {
            old_value = m_state.load(std::memory_order_acquire);
            if (old_value == this)
            {
                waiter->m_next = nullptr;
                return false;
            }
            waiter->m_next = static_cast<awaiter*>(old_value);
        } while (!m_state.compare_exchange_weak(old_value, waiter, std::memory_order_acquire));

        return true; // 成功注册，需要挂起协程
    }

private:
    std::atomic<detail::awaiter_ptr> m_state{nullptr};
};

/**
 * @brief RAII for event
 *
 */
class event_guard
{
    using guard_type = event<>;

public:
    event_guard(guard_type& ev) noexcept : m_ev(ev) {}
    ~event_guard() noexcept
    {
        m_ev.set();
        log::debug("event_guard::~event_guard");
    }

private:
    guard_type& m_ev;
};

}; // namespace coro
