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

class event_base
{
public:
    struct awaiter_base
    {
        awaiter_base(context& ctx, event_base& ev) : m_ctx(ctx), m_ev(ev) {}
        auto await_ready() noexcept -> bool;
        auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool;
        auto await_resume() noexcept -> void { m_ctx.unregister_wait(); }

        context&                m_ctx;
        event_base&             m_ev;
        awaiter_base*           m_next{nullptr};
        std::coroutine_handle<> m_await_coro{nullptr};
    };

    event_base(bool inital_set = false) noexcept : m_state(inital_set ? this : nullptr) {}
    ~event_base() noexcept = default;

    event_base(const event_base&)            = delete;
    event_base(event_base&&)                 = delete;
    event_base& operator=(const event_base&) = delete;
    event_base& operator=(event_base&&)      = delete;

    void set_state() noexcept;

    auto is_set() const noexcept -> bool;

    auto resume_all_awaiter(detail::awaiter_ptr waiter) noexcept -> void;

    auto register_awaiter(awaiter_base* waiter) noexcept -> bool;

private:
    std::atomic<detail::awaiter_ptr> m_state{nullptr};
};
}; // namespace detail

// TODO[lab4a]: This event is an example to make complie success,
// You should delete it and add your implementation, I don't care what you do,
// but keep the function set() and wait()'s declaration same with example.
template<typename return_type = void>
class event : public detail::event_base, public detail::container<return_type>
{
public:
    using detail::event_base::event_base;
    struct awaiter : public awaiter_base
    {
        using awaiter_base::awaiter_base;
        auto await_resume() noexcept -> return_type
        {
            awaiter_base::await_resume();
            return static_cast<event&>(m_ev).result();
        }
    };

    auto wait() noexcept -> awaiter { return awaiter{local_context(), *this}; } // return awaitable

    template<typename value_type>
    auto set(value_type&& value) noexcept -> void
    {
        this->return_value(std::forward<value_type>(value));
        set_state();
    }
};

template<>
class event<void> : public detail::event_base
{
public:
    using detail::event_base::event_base;
    struct awaiter : public awaiter_base
    {
        using awaiter_base::awaiter_base;
    };

public:
    auto wait() noexcept -> awaiter { return awaiter{local_context(), *this}; } // return awaitable
    auto set() noexcept -> void { set_state(); }
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
