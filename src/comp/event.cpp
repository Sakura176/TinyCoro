#include "coro/comp/event.hpp"
#include "coro/detail/types.hpp"
#include "coro/scheduler.hpp"

namespace coro
{
// TODO[lab4a] : Add codes if you need
auto event_base::is_set() const noexcept -> bool
{
    return m_state.load(std::memory_order_acquire) == this;
}

auto event_base::resume_all_awaiter(detail::awaiter_ptr waiter) noexcept -> void
{
    while (waiter != nullptr)
    {
        auto cur = static_cast<awaiter*>(waiter);
        cur->m_ctx.submit_task(cur->m_await_coro);
        waiter = cur->m_next;
    }
}

auto event_base::register_awaiter(awaiter_base* waiter) noexcept -> bool
{
    const auto    set_state = this;
    awaiter_base* old_value = nullptr;

    do
    {
        old_value = m_state.load(std::memory_order_acquire);
        if (old_value == this)
        {
            waiter->m_next = nullptr;
            return false;
        }
        waiter->m_next = static_cast<awaiter_base*>(old_value);
    } while (!m_state.compare_exchange_weak(old_value, waiter, std::memory_order_acquire));

    return true; // 成功注册，需要挂起协程
}
}; // namespace coro
