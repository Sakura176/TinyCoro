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
    log::debug("event_base::awaiter_base::await_ready: ctx_id={}", m_ctx.get_ctx_id());
    m_ctx.register_wait();
    bool is_set = m_ev.is_set();
    log::debug("event_base::awaiter_base::await_ready: event is_set={}", is_set);
    return is_set; // 检查时间是否已设置，如已设置则无需挂起协程
}

auto event_base::awaiter_base::await_suspend(std::coroutine_handle<> handle) noexcept -> bool
{
    log::debug("event_base::awaiter_base::await_suspend: ctx_id={}, handle={}", m_ctx.get_ctx_id(), reinterpret_cast<uintptr_t>(handle.address()));
    m_await_coro = handle;
    bool need_suspend = m_ev.register_awaiter(this);
    log::debug("event_base::awaiter_base::await_suspend: need_suspend={}", need_suspend);
    return need_suspend;
}

auto event_base::set_state() noexcept -> void
{
    auto old_state = m_state.load(std::memory_order_acquire);
    log::debug("event_base::set_state: old_state={}, this={}", reinterpret_cast<uintptr_t>(old_state), reinterpret_cast<uintptr_t>(this));
    // 将当前事件对象指针设置为新状态，同时返回旧状态值
    auto flag = m_state.exchange(this, std::memory_order_acq_rel);
    log::debug("event_base::set_state: exchanged flag={}", reinterpret_cast<uintptr_t>(flag));
    if (flag != this)
    {
        log::debug("event_base::set_state: flag != this, resuming awaiters");
        auto waiter = static_cast<awaiter_base*>(flag);
        resume_all_awaiter(waiter);
    }
    else
    {
        log::debug("event_base::set_state: event was already set");
    }
}

auto event_base::is_set() const noexcept -> bool
{
    return m_state.load(std::memory_order_acquire) == this;
}

auto event_base::resume_all_awaiter(detail::awaiter_ptr waiter) noexcept -> void
{
    log::debug("resume_all_awaiter: start, waiter: {}", reinterpret_cast<uintptr_t>(waiter));
    int count = 0;

    while (waiter != nullptr)
    {
        auto cur = static_cast<awaiter_base*>(waiter);

        if (!cur->m_await_coro || cur->m_await_coro.done())
        {
            log::debug("resume_all_awaiter: m_await_coro is null or done, count: {}", count);
            waiter = cur->m_next;
            continue;
        }

        // 保存下一个节点的指针
        auto next = cur->m_next;
        // 提交协程任务，会导致下一个节点失效
        log::debug("resume_all_awaiter: resuming coro handle={}, ctx_id={}", reinterpret_cast<uintptr_t>(cur->m_await_coro.address()), cur->m_ctx.get_ctx_id());
        cur->m_ctx.submit_task(cur->m_await_coro);
        count++;
        waiter = next;

        log::debug("resume_all_awaiter: resumed {} awaiters", count);
    }
}

auto event_base::register_awaiter(awaiter_base* waiter) noexcept -> bool
{
    const auto          set_state = this;
    detail::awaiter_ptr old_value = nullptr;

    do
    {
        old_value = m_state.load(std::memory_order_acquire);
        log::debug("event_base::register_awaiter: old_value={}, this={}", reinterpret_cast<uintptr_t>(old_value), reinterpret_cast<uintptr_t>(this));
        if (old_value == this)
        {
            log::debug("event_base::register_awaiter: event already set, no need to suspend");
            waiter->m_next = nullptr;
            return false;
        }
        // 新等待器插入链表头部
        waiter->m_next = static_cast<awaiter_base*>(old_value);
        log::debug("event_base::register_awaiter: waiter->m_next set to {}", reinterpret_cast<uintptr_t>(waiter->m_next));
        // CAS操作：比较m_state和old_value，如果相等则设置为waiter（新链表头）
    } while (!m_state.compare_exchange_weak(old_value, waiter, std::memory_order_acquire));

    log::debug("event_base::register_awaiter: successfully registered waiter, need to suspend");
    return true; // 成功注册，需要挂起协程
}
} // namespace detail
}; // namespace coro
