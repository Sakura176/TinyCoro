#include "coro/comp/condition_variable.hpp"
#include "coro/context.hpp"
#include "coro/scheduler.hpp"
#include <memory>

namespace coro
{
// TODO[lab5b] : Add codes if you need
auto condition_variable::cv_awaiter::await_suspend(std::coroutine_handle<> h) noexcept -> bool
{
    // NOTE: 协程挂起后应该将事件注册的io_uring；
    m_await_coro = h;
    return register_lock();
}
auto condition_variable::cv_awaiter::await_resume() noexcept -> void
{
    m_ctx.unregister_wait(m_suspend_state);
    return;
}

auto condition_variable::cv_awaiter::resume() noexcept -> void
{
    log::info("resume begin");

    // 先判断条件谓词是否满足
    if (m_cond && m_cond())
    {
        // TODO: 条件谓词判断逻辑，后续增加
    }
    mutex_awaiter::resume();
}

auto condition_variable::cv_awaiter::register_lock() noexcept -> bool
{
    // 有条件谓词，且条件满足；则返回false立即恢复执行
    if (m_cond && m_cond())
    {
        return false;
    }

    // 注册等待事件到io_uring
    // QUESTION: 为何要传入一个 m_suspend_state 参数
    m_ctx.register_wait(!m_suspend_state);
    m_suspend_state = true;

    register_cv();
    // QUESTION: 解锁，但加锁区域未知
    m_mtx.unlock();

    return true;
}

auto condition_variable::cv_awaiter::register_cv() noexcept -> void
{
    log::info("register_cv");

    // 清空等待队列
    m_next = nullptr;

    // m_cv.m_lock.lock();
    // 若列表尾部为空值，则说明列表为空
    if (m_cv.m_tail == nullptr)
    {
        // 将列表头尾节点均设置为当前协程awaiter
        m_cv.m_head = m_cv.m_tail = this;
    }
    else
    {
        // 将当前协程移动到列表尾节点之后
        m_cv.m_tail->m_next = this;
        // 更新列表尾节点
        m_cv.m_tail = this;
    }
}

auto condition_variable::wait(mutex& mtx) noexcept -> cv_awaiter
{ return cv_awaiter{local_context(), mtx, *this}; }

auto condition_variable::wait(mutex& mtx, cond_type&& cond) noexcept -> cv_awaiter
{ return cv_awaiter{local_context(), mtx, *this, cond}; }

auto condition_variable::wait(mutex& mtx, cond_type& cond) noexcept -> cv_awaiter
{ return cv_awaiter{local_context(), mtx, *this, cond}; }

auto condition_variable::notify_one() noexcept -> void {
    m_lock.lock();
    auto cur = m_head;
    if (cur != nullptr) {
        m_head = reinterpret_cast<cv_awaiter*>(cur->m_next);
        if (m_head == nullptr) {
            m_tail = nullptr;
        }
        m_lock.unlock();
        cur->resume();
    }
}

auto condition_variable::notify_all() noexcept -> void {
    // m_lock.lock();
    auto cur = m_head;
    while (cur != nullptr) {
        m_head = reinterpret_cast<cv_awaiter*>(cur->m_next);
        cur->resume();
        cur = m_head;
    }
    m_tail = nullptr;
    // m_lock.unlock();
}
} // namespace coro
