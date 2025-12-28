#include "coro/comp/event.hpp"
#include "coro/detail/types.hpp"
#include "coro/log.hpp"
#include "coro/scheduler.hpp"
#include <coroutine>

namespace coro
{
namespace detail
{
auto event_base::awaiter_base::await_ready() noexcept -> bool
{
    m_ctx.register_wait();
    return m_ev.is_set(); // 检查时间是否已设置，如已设置则无需挂起协程
}

auto event_base::awaiter_base::await_suspend(std::coroutine_handle<> handle) noexcept -> bool
{
    m_await_coro = handle;
    return m_ev.register_awaiter(this);
}

auto event_base::set_state() noexcept -> void
{
    // 将当前事件对象指针设置为新状态，同时返回旧状态值
    auto flag = m_state.exchange(this, std::memory_order_acq_rel);
    if (flag != this)
    {
        auto waiter = static_cast<awaiter_base*>(flag);
        resume_all_awaiter(waiter);
    }
}

auto event_base::is_set() const noexcept -> bool
{
    return m_state.load(std::memory_order_acquire) == this;
}

auto event_base::resume_all_awaiter(detail::awaiter_ptr waiter) noexcept -> void
{
    int count = 0;

    while (waiter != nullptr)
    {
        auto cur = static_cast<awaiter_base*>(waiter);

        if (!cur->m_await_coro || cur->m_await_coro.done())
        {
            waiter = cur->m_next;
            continue;
        }

        // 保存下一个节点的指针
        auto next = cur->m_next;
        // 提交协程任务，会导致下一个节点失效
        cur->m_ctx.submit_task(cur->m_await_coro);
        count++;
        waiter = next;

        log::debug("resume_all_awaiter:resume {} awaiters", count);
    }
}

auto event_base::register_awaiter(awaiter_base* waiter) noexcept -> bool
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
        // 新等待器插入链表头部
        waiter->m_next = static_cast<awaiter_base*>(old_value);
        // CAS操作：比较m_state和old_value，如果相等则设置为waiter（新链表头）
    } while (!m_state.compare_exchange_weak(old_value, waiter, std::memory_order_acquire));

    return true; // 成功注册，需要挂起协程
}
} // namespace detail
}; // namespace coro
