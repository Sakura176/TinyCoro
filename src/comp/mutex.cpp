#include "coro/comp/mutex.hpp"
#include "coro/context.hpp"
#include "coro/log.hpp"
#include "coro/scheduler.hpp"
#include <atomic>
#include <cassert>

namespace coro
{
// TODO[lab4d] : Add codes if you need
// 直接挂起协程
auto mutex::mutex_awaiter::await_ready() noexcept -> bool
{
    log::info("await_ready");
    auto ret = m_mtx.try_lock_impl();
    log::info("try_lock_impl {}", ret);
    return ret;
}
auto mutex::mutex_awaiter::await_suspend(std::coroutine_handle<> handle) noexcept -> bool
{
    log::info("await_suspend");
    m_await_coro = handle;
    m_ctx.register_wait();
    return m_mtx.enqueue_waiter(this);
}
auto mutex::mutex_awaiter::await_resume() noexcept -> void
{
    log::info("await_resume");
    m_ctx.unregister_wait();
}

auto mutex::try_lock() noexcept -> bool
{
    return try_lock_impl();
}
auto mutex::lock() noexcept -> mutex_awaiter
{
    // 直接返回等待器，内部判断是否挂起
    return mutex_awaiter(local_context(), *this);
};

auto mutex::unlock() noexcept -> void
{
    // 快速释放锁
    m_locked.store(false, std::memory_order_release);
    // 存在等待者，则尝试唤醒一个
    if (m_waiters.load(std::memory_order_acquire) != nullptr)
    {
        dequeue_and_resume_one();
    }
}

auto mutex::lock_guard() noexcept -> guard_awaiter
{
    return guard_awaiter(local_context(), *this);
}

bool mutex::try_lock_impl() noexcept
{
    bool expected = false;
    return m_locked.compare_exchange_strong(expected, true, std::memory_order_relaxed);
}

bool mutex::enqueue_waiter(mutex_awaiter* waiter) noexcept
{
    log::info("enqueue_waiter");
    assert(waiter != nullptr);

    if (try_lock_impl())
    {
        waiter->should_suspend.store(false, std::memory_order_release);
        return false;
    }

    // 将等待者加入队列头部
    auto* old_head = m_waiters.load(std::memory_order_acquire);
    do
    {
        waiter->m_next = old_head;
    } while (!m_waiters.compare_exchange_weak(old_head, waiter, std::memory_order_release, std::memory_order_acquire));

    return true;
}

void mutex::dequeue_and_resume_one() noexcept
{
    log::info("dequeue_and_resume_one");

    mutex_awaiter* waiter = m_waiters.load(std::memory_order_acquire);

    while (waiter != nullptr)
    {
        auto* next = waiter->m_next;
        if (m_waiters.compare_exchange_weak(waiter, next, std::memory_order_release, std::memory_order_acquire))
        {
            // 成功移除等待者，唤醒它
            waiter->resume();
            return;
        }
        // CAS 失败，重新加载
        waiter = m_waiters.load(std::memory_order_acquire);
    }
    // 没有等待者，直接释放锁
    m_locked.store(false, std::memory_order_release);
}
}; // namespace coro
