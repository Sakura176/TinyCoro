#include "coro/comp/condition_variable.hpp"
#include "coro/context.hpp"
#include "coro/scheduler.hpp"
#include <memory>

namespace coro
{
auto condition_variable::cv_awaiter::await_suspend(std::coroutine_handle<> h) noexcept -> bool
{
    // NOTE: 保存协程句柄，由 register_lock() 决定是否挂起
    //   - 条件谓词已满足 → return false，协程不挂起，继续执行
    //   - 条件谓词不满足 → 注册到 CV 等待队列、释放 mutex、return true，协程挂起
    m_await_coro = h;
    return register_lock();
}
auto condition_variable::cv_awaiter::await_resume() noexcept -> void
{
    // NOTE: 协程恢复时取消 context 上的等待计数
    // mutex 已在 resume() 中重新获取，此处无需额外加锁
    m_ctx.unregister_wait(m_suspend_state);
}

auto condition_variable::cv_awaiter::resume() noexcept -> void
{
    log::debug("resume begin");

    // NOTE: 步骤1 — 条件谓词重检，处理 spurious wakeup
    // notify_all 会唤醒所有等待者，只有满足条件的才能继续，其余重新入队
    if (m_cond && !m_cond())
    {
        // NOTE: 若是从 mutex::unlock 转移锁后谓词失败，
        // 必须释放 mutex 并清除标志，否则 mutex 永久锁定
        if (m_mutex_acquired)
        {
            m_mtx.unlock();
            m_mutex_acquired = false;
        }
        register_cv();
        return;
    }

    // NOTE: 步骤2 — 已持有 mutex（来自 mutex::unlock 转移），直接恢复协程
    if (m_mutex_acquired) {
        mutex_awaiter::resume();
        return;
    }

    // NOTE: 步骤3 — 来自 notify 唤醒，尝试直接获取 mutex
    if (m_mtx.try_lock()) {
        mutex_awaiter::resume();
        return;
    }

    // NOTE: 步骤4 — mutex 被占用，注册为 mutex 等待者
    // 设置标志：下次 resume() 由 mutex::unlock() 调用，跳过 try_lock
    m_mutex_acquired = true;
    bool need_suspend = m_mtx.register_waiter(this);
    if (!need_suspend) {
        // 竞态中直接拿到了锁，清除标志
        m_mutex_acquired = false;
        mutex_awaiter::resume();
    }
}

auto condition_variable::cv_awaiter::register_lock() noexcept -> bool
{
    // NOTE: 条件谓词已满足 → 不挂起，不释放 mutex，协程继续执行
    if (m_cond && m_cond())
    {
        return false;
    }

    // NOTE: 条件不满足 → 注册等待事件、加入 CV 队列、释放 mutex、挂起协程
    // m_suspend_state 初始为 false，首次 register_wait(1) 后置 true
    // await_resume 中用 m_suspend_state 确定 unregister 计数
    m_ctx.register_wait(!m_suspend_state);
    m_suspend_state = true;

    register_cv();
    // NOTE: 释放 mutex，让其他协程可以获取锁
    // mutex 将在 resume() 中通过 try_lock 或 register_waiter 重新获取
    m_mtx.unlock();

    return true;
}

auto condition_variable::cv_awaiter::register_cv() noexcept -> void
{
    log::debug("register_cv");
    // NOTE: 持锁操作 CV 链表，保证与 notify_one/notify_all 的并发安全
    m_cv.m_lock.lock();
    m_next = nullptr;

    if (m_cv.m_tail == nullptr)
    {
        // 链表为空，当前 awaiter 同时作为头尾
        m_cv.m_head = m_cv.m_tail = this;
    }
    else
    {
        // 追加到链表尾部
        m_cv.m_tail->m_next = this;
        m_cv.m_tail = this;
    }
    m_cv.m_lock.unlock();
}

auto condition_variable::wait(mutex& mtx) noexcept -> cv_awaiter
{ return cv_awaiter{local_context(), mtx, *this}; }

auto condition_variable::wait(mutex& mtx, cond_type&& cond) noexcept -> cv_awaiter
{ return cv_awaiter{local_context(), mtx, *this, cond}; }

auto condition_variable::wait(mutex& mtx, cond_type& cond) noexcept -> cv_awaiter
{ return cv_awaiter{local_context(), mtx, *this, cond}; }

auto condition_variable::notify_one() noexcept -> void {
    // NOTE: 持锁操作链表，弹出第一个等待者，释放锁后恢复
    m_lock.lock();
    auto cur = m_head;
    if (cur != nullptr) {
        m_head = reinterpret_cast<cv_awaiter*>(cur->m_next);
        if (m_head == nullptr) {
            m_tail = nullptr;
        }
        m_lock.unlock();
        // NOTE: 锁释放后再 resume，避免 resume 中的 register_cv 死锁
        cur->resume();
    } else {
        // NOTE: 空队列也需要解锁，否则后续 notify 永久自旋
        m_lock.unlock();
    }
}

auto condition_variable::notify_all() noexcept -> void {
    // NOTE: 先持锁摘除整个链表，释放锁后再逐个 resume
    // 避免 resume() → register_cv() 重入 m_lock 导致 spinlock 死锁
    m_lock.lock();
    auto cur = m_head;
    // NOTE: 清空链表头尾，让并发 register_cv 可以独立操作新链表
    m_head = nullptr;
    m_tail = nullptr;
    m_lock.unlock();

    while (cur != nullptr) {
        // NOTE: 使用局部变量保存 next，避免复用 m_head 导致并发覆盖
        auto next = reinterpret_cast<cv_awaiter*>(cur->m_next);
        cur->resume();
        cur = next;
    }
}
} // namespace coro
