#include "coro/comp/mutex.hpp"
#include "coro/context.hpp"
#include "coro/log.hpp"
#include "coro/scheduler.hpp"
#include <atomic>
#include <cassert>
#include <cstdint>

namespace coro
{
// TODO[lab4d] : Add codes if you need
// 直接挂起协程
auto mutex::mutex_awaiter::await_ready() noexcept -> bool
{
    // log::info("await_ready");
    return false;
}
auto mutex::mutex_awaiter::await_suspend(std::coroutine_handle<> handle) noexcept -> bool
{
    // log::info("await_suspend");
    m_await_coro = handle;
    m_ctx.register_wait();
    return m_mtx.register_waiter(this);
}
auto mutex::mutex_awaiter::await_resume() noexcept -> void
{
    // log::info("await_resume");
    m_ctx.unregister_wait();
}

auto mutex::try_lock() noexcept -> bool
{
    uintptr_t expected = 0;
    return m_state.compare_exchange_strong(expected, 1, std::memory_order_acquire, std::memory_order_relaxed);
}
auto mutex::lock() noexcept -> mutex_awaiter
{
    // 直接返回等待器，内部判断是否挂起
    return mutex_awaiter(local_context(), *this);
};

auto mutex::unlock() noexcept -> void
{
    uintptr_t old_state = m_state.load(std::memory_order_acquire);

    while (true)
    {
        if (old_state == 1)
        {
            // 尝试直接获取锁
            if (m_state.compare_exchange_weak(old_state, 0, std::memory_order_release, std::memory_order_relaxed))
            {
                return; // 成功释放
            }
        }
        else
        { // 有等待者
            auto* waiter = reinterpret_cast<mutex_awaiter*>(old_state);
            auto* next   = waiter->m_next;
            // 尝试将锁转移给下一个等待者
            if (m_state.compare_exchange_weak(
                    old_state, reinterpret_cast<uintptr_t>(next), std::memory_order_acq_rel, std::memory_order_relaxed))
            {
                // 成功转移给下一个等待者
                waiter->resume();
                return;
            }
        }
    }
}

auto mutex::lock_guard() noexcept -> guard_awaiter
{
    return guard_awaiter(local_context(), *this);
}

bool mutex::register_waiter(mutex_awaiter* waiter) noexcept
{
    assert(waiter != nullptr);

    uintptr_t old_state = m_state.load(std::memory_order_acquire);

    while (true)
    {
        if (old_state == 0) // nolocked
        {
            // 尝试直接获取锁
            if (m_state.compare_exchange_weak(old_state, 1, std::memory_order_acq_rel, std::memory_order_relaxed))
            {
                return false;
            }
        }
        else
        {
            waiter->m_next = reinterpret_cast<mutex_awaiter*>(old_state);

            if (m_state.compare_exchange_weak(
                    old_state,
                    reinterpret_cast<uintptr_t>(waiter),
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed))
            {
                return true;
            }
        }
    }
}
}; // namespace coro
