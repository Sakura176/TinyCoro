/**
 * @file task.hpp
 * @author JiahuiWang
 * @brief lab1
 * @version 1.0
 * @date 2025-03-26
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <cassert>
#include <coroutine>
#include <stdexcept>
#include <utility>

#include "coro/attribute.hpp"
#include "coro/detail/container.hpp"
#include "coro/log.hpp"

namespace coro
{
/**
 * @brief Welcome to tinycoro lab1, in this part you will add codes for task.hpp to make task can be
 * detached and after the execution is completed, the executive power will be transferred to the
 * parent level.
 *
 * You should follow the rules below in this part:
 *
 * @note Do not modify existing functions and class declaration, which may cause the test to not run
 * correctly, but you can change the implementation logic if you need.
 *
 * @note The location marked by todo is where you must add code, but you can also add code anywhere
 * you want, such as function and class definitions, even member variables.
 */

template<typename return_type = void>
class task;

enum class CoroType
{
    Normal,
    Detach,
    Clean,
};

namespace detail
{
struct promise_base
{
    struct awaiter_base
    {
    public:
        // return false to suspend coroutine
        auto await_ready() const noexcept -> bool { return false; }

        template<typename promise_type>
        auto await_suspend(std::coroutine_handle<promise_type> coroutine) noexcept -> std::coroutine_handle<>
        {
            auto& promise = coroutine.promise();
            return promise.m_continuation == nullptr ? std::noop_coroutine() : promise.m_continuation;
        }

        void await_resume() noexcept { log::debug("promise_base awaiter_base await_resume"); }
    };
    promise_base() noexcept = default;
    ~promise_base()         = default;

    constexpr auto initial_suspend() noexcept { return std::suspend_always{}; }

    [[CORO_TEST_USED(lab1)]] auto final_suspend() noexcept
    {
        // TODO[lab1]: Add you codes
        // Return suspend_always is incorrect,
        // so you should modify the return type and define new awaiter to return
        return awaiter_base{};
    }

    void continuation(std::coroutine_handle<> continuation) noexcept { m_continuation = continuation; }

    void     set_state(CoroType state) { m_state = state; }
    CoroType get_state() const { return m_state; }

    std::coroutine_handle<> m_continuation{nullptr};
    CoroType                m_state{CoroType::Normal};

#ifdef DEBUG
public:
    int promise_id{0};
#endif // DEBUG
};

template<typename return_type>
struct promise final : public promise_base, public container<return_type>
{
public:
    using task_type        = task<return_type>;
    using coroutine_handle = std::coroutine_handle<promise<return_type>>;

#ifdef DEBUG
    template<typename... Args>
    promise(int id, Args&&... args) noexcept
    {
        promise_id = id;
    }
#endif // DEBUG
    promise() noexcept {}
    promise(const promise&)             = delete;
    promise(promise&& other)            = delete;
    promise& operator=(const promise&)  = delete;
    promise& operator=(promise&& other) = delete;
    ~promise()                          = default;

    auto get_return_object() noexcept -> task_type;

    auto unhandled_exception() noexcept -> void { this->set_exception(); }
};

template<>
struct promise<void> : public promise_base
{
    using task_type        = task<void>;
    using coroutine_handle = std::coroutine_handle<promise<void>>;

#ifdef DEBUG
    template<typename... Args>
    promise(int id, Args&&... args) noexcept
    {
        promise_id = id;
    }
#endif // DEBUG
    promise() noexcept                  = default;
    promise(const promise&)             = delete;
    promise(promise&& other)            = delete;
    promise& operator=(const promise&)  = delete;
    promise& operator=(promise&& other) = delete;
    ~promise()                          = default;

    auto get_return_object() noexcept -> task_type;

    constexpr auto return_void() noexcept -> void {}

    auto unhandled_exception() noexcept -> void { m_exception_ptr = std::current_exception(); }

    auto result() -> void
    {
        if (m_exception_ptr)
        {
            std::rethrow_exception(m_exception_ptr);
        }
    }

private:
    std::exception_ptr m_exception_ptr{nullptr};
};

} // namespace detail

template<typename return_type>
class [[CORO_AWAIT_HINT]] task
{
public:
    using task_type        = task<return_type>;
    using promise_type     = detail::promise<return_type>;
    using coroutine_handle = std::coroutine_handle<promise_type>;

    struct awaitable_base
    {
        awaitable_base(coroutine_handle coroutine) noexcept : m_coroutine(coroutine) {}

        auto await_ready() const noexcept -> bool { return !m_coroutine || m_coroutine.done(); }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> std::coroutine_handle<>
        {
            // TODO[lab1]: Add you codes
            log::debug("task await_suspend!");
            // m_coroutine = awaiting_coroutine.promise().m_continuation;
            m_coroutine.promise().continuation(awaiting_coroutine);
            return m_coroutine;
        }

        std::coroutine_handle<promise_type> m_coroutine{nullptr};
    };

    task() noexcept : m_coroutine(nullptr) {}

    explicit task(coroutine_handle handle) : m_coroutine(handle) {}
    task(const task&) = delete;
    task(task&& other) noexcept : m_coroutine(std::exchange(other.m_coroutine, nullptr)) {}

    ~task()
    {
        if (m_coroutine != nullptr)
        {
            m_coroutine.destroy();
        }
    }

    auto operator=(const task&) -> task& = delete;

    auto operator=(task&& other) noexcept -> task&
    {
        if (std::addressof(other) != this)
        {
            if (m_coroutine != nullptr)
            {
                m_coroutine.destroy();
            }

            m_coroutine = std::exchange(other.m_coroutine, nullptr);
        }

        return *this;
    }

    /**
     * @return True if the task is in its final suspend or if the task has been destroyed.
     */
    auto is_ready() const noexcept -> bool { return m_coroutine == nullptr || m_coroutine.done(); }

    auto resume() -> bool
    {
        log::debug("task resume");
        if (!m_coroutine.done())
        {
            m_coroutine.resume();
        }
        return !m_coroutine.done();
    }

    auto destroy() -> bool
    {
        if (m_coroutine != nullptr)
        {
            m_coroutine.destroy();
            m_coroutine = nullptr;
            return true;
        }

        return false;
    }

    [[CORO_TEST_USED(lab1)]] auto detach() -> void
    {
        // TODO[lab1]: Add you codes
        m_coroutine.promise().set_state(CoroType::Detach);
        m_coroutine = nullptr;
    }

    auto operator co_await() const& noexcept
    {
        struct awaitable : public awaitable_base
        {
            auto await_resume() -> decltype(auto) { return this->m_coroutine.promise().result(); }
        };

        return awaitable{m_coroutine};
    }

    auto operator co_await() const&& noexcept
    {
        struct awaitable : public awaitable_base
        {
            auto await_resume() -> decltype(auto) { return std::move(this->m_coroutine.promise()).result(); }
        };

        return awaitable{m_coroutine};
    }

    auto promise() & -> promise_type& { return m_coroutine.promise(); }
    auto promise() const& -> const promise_type& { return m_coroutine.promise(); }
    auto promise() && -> promise_type&& { return std::move(m_coroutine.promise()); }

    auto handle() & -> coroutine_handle { return m_coroutine; }
    auto handle() && -> coroutine_handle { return std::exchange(m_coroutine, nullptr); }

private:
    coroutine_handle m_coroutine{nullptr};
};

using coroutine_handle = std::coroutine_handle<detail::promise_base>;

/**
 * @brief do clean work when handle is done
 *
 * @param handle
 */
[[CORO_TEST_USED(lab1)]] inline auto clean(std::coroutine_handle<> handle) noexcept -> void
{
    // TODO[lab1]: Add you codes
    auto coroutine = coroutine_handle::from_address(handle.address());
    switch (coroutine.promise().get_state())
    {
        case CoroType::Detach:
            coroutine.destroy();
            break;
        default:
            break;
    }
}

namespace detail
{
template<typename return_type>
inline auto promise<return_type>::get_return_object() noexcept -> task<return_type>
{
    return task<return_type>{coroutine_handle::from_promise(*this)};
}

inline auto promise<void>::get_return_object() noexcept -> task<>
{
    return task<>{coroutine_handle::from_promise(*this)};
}

#ifdef DEBUG
template<typename T = void>
inline auto get_promise(std::coroutine_handle<> handle) -> promise<T>&
{
    auto  specific_handle = std::coroutine_handle<detail::promise<T>>::from_address(handle.address());
    auto& promise         = specific_handle.promise();
    return promise;
}
#endif // DEBUG

} // namespace detail

} // namespace coro
