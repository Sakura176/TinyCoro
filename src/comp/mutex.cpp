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
    log::debug("mutex_awaiter::await_ready: ctx_id={}", m_ctx.get_ctx_id());
    return false;
}
auto mutex::mutex_awaiter::await_suspend(std::coroutine_handle<> handle) noexcept -> bool
{
    log::debug("mutex_awaiter::await_suspend: ctx_id={}, handle={}", m_ctx.get_ctx_id(), reinterpret_cast<uintptr_t>(handle.address()));
    m_await_coro = handle;
    m_ctx.register_wait();
    bool need_suspend = m_mtx.register_waiter(this);
    log::debug("mutex_awaiter::await_suspend: need_suspend={}", need_suspend);
    return need_suspend;
}
auto mutex::mutex_awaiter::await_resume() noexcept -> void
{
    log::debug("mutex_awaiter::await_resume: ctx_id={}", m_ctx.get_ctx_id());
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
    log::debug("mutex::unlock: old_state={}", old_state);

    while (true)
    {
        if (old_state == 1)
        {
            // 尝试直接获取锁
            if (m_state.compare_exchange_weak(old_state, 0, std::memory_order_release, std::memory_order_relaxed))
            {
                log::debug("mutex::unlock: released lock, no waiters");
                return; // 成功释放
            }
        }
        else
        { // 有等待者
            auto* waiter = reinterpret_cast<mutex_awaiter*>(old_state);
            auto* next   = waiter->m_next;
            log::debug("mutex::unlock: has waiter={}, next={}", reinterpret_cast<uintptr_t>(waiter), reinterpret_cast<uintptr_t>(next));
            // 尝试将锁转移给下一个等待者
            if (m_state.compare_exchange_weak(
                    old_state, reinterpret_cast<uintptr_t>(next), std::memory_order_acq_rel, std::memory_order_relaxed))
            {
                // 成功转移给下一个等待者
                log::debug("mutex::unlock: transferred lock to next waiter, resuming waiter={}", reinterpret_cast<uintptr_t>(waiter));
                waiter->resume();
                return;
            }
        }
        old_state = m_state.load(std::memory_order_acquire);
        log::debug("mutex::unlock: retry, new old_state={}", old_state);
    }
}

auto mutex::lock_guard() noexcept -> guard_awaiter
{
    return guard_awaiter(local_context(), *this);
}

bool mutex::register_waiter(mutex_awaiter* waiter) noexcept
{
    assert(waiter != nullptr);
    log::debug("mutex::register_waiter: waiter={}, ctx_id={}", reinterpret_cast<uintptr_t>(waiter), waiter->m_ctx.get_ctx_id());

    uintptr_t old_state = m_state.load(std::memory_order_acquire);

    while (true)
    {
        log::debug("mutex::register_waiter: old_state={}", old_state);
        if (old_state == 0) // nolocked
        {
            // 尝试直接获取锁
            if (m_state.compare_exchange_weak(old_state, 1, std::memory_order_acq_rel, std::memory_order_relaxed))
            {
                log::debug("mutex::register_waiter: acquired lock directly, no suspend");
                return false;
            }
        }
        else
        {
            waiter->m_next = reinterpret_cast<mutex_awaiter*>(old_state);
            log::debug("mutex::register_waiter: waiter->m_next={}", reinterpret_cast<uintptr_t>(waiter->m_next));

            if (m_state.compare_exchange_weak(
                    old_state,
                    reinterpret_cast<uintptr_t>(waiter),
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed))
            {
                log::debug("mutex::register_waiter: added to wait queue, need suspend");
                return true;
            }
        }
    }
}
}; // namespace coro
