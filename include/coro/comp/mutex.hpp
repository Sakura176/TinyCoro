#pragma once

#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstdint>
#include <type_traits>

#include "coro/comp/mutex_guard.hpp"
#include "coro/context.hpp"
#include "coro/detail/types.hpp"
#include "coro/log.hpp"

namespace coro
{
/**
 * @brief Welcome to tinycoro lab4d, in this part you will build the basic coroutine
 * synchronization component----mutex by modifing mutex.hpp and mutex.cpp.
 * Please ensure you have read the document of lab4d.
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

// TODO[lab4d]: This mutex is an example to make complie success,
// You should delete it and add your implementation, I don't care what you do,
// but keep the member function and construct function's declaration same with example.
class mutex
{
public:
    struct mutex_awaiter
    {
        mutex_awaiter(context& ctx, mutex& mtx) noexcept : m_ctx(ctx), m_mtx(mtx) {}

        auto await_ready() noexcept -> bool;
        auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool;
        auto await_resume() noexcept -> void;

        // QUESTION: Why resume the coroutine need to be submitted to the context?
        virtual auto resume() noexcept -> void
        {
            log::debug("mutex_awaiter::resume: ctx_id={}, coro={}", m_ctx.get_ctx_id(), reinterpret_cast<uintptr_t>(m_await_coro.address()));
            m_ctx.submit_task(m_await_coro);
        }

        context&                m_ctx;
        mutex&                  m_mtx;
        mutex_awaiter*          m_next{nullptr};
        std::atomic<bool>       should_suspend{true};
        std::coroutine_handle<> m_await_coro;
    };
    // Just make lock_guard() compile success
    struct guard_awaiter : public mutex_awaiter
    {
        using guard_type = detail::lock_guard<mutex>;
        using mutex_awaiter::mutex_awaiter;

        auto await_resume() -> guard_type
        {
            mutex_awaiter::await_resume();
            return guard_type(m_mtx);
        }
    };

public:
    mutex() noexcept {}
    ~mutex() noexcept {}

    auto try_lock() noexcept -> bool;
    auto lock() noexcept -> mutex_awaiter;

    auto unlock() noexcept -> void;

    auto lock_guard() noexcept -> guard_awaiter;

public:
    bool register_waiter(mutex_awaiter* waiter) noexcept;

private:
    std::atomic<uintptr_t>           m_state{0}; // 0=空闲，1=锁定无等待，其他=等待队列头
};

}; // namespace coro
