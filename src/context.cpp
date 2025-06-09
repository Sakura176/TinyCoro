#include "coro/context.hpp"
#include "coro/scheduler.hpp"

namespace coro
{
context::context() noexcept
{
    m_id = ginfo.context_id.fetch_add(1, std::memory_order_relaxed);
}

auto context::init() noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_engine.init();
    linfo.ctx = this;
}

auto context::deinit() noexcept -> void
{
    // TODO[lab2b]: Add you codes
    linfo.ctx = nullptr;
    m_engine.deinit();
}

auto context::start() noexcept -> void
{
    m_job = make_unique<jthread>(
        [this](stop_token token)
        {
            this->init();
            this->run(token);
            this->deinit();
        });
}

auto context::notify_stop() noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_job->request_stop();
    m_engine.wake_up();
}

auto context::submit_task(std::coroutine_handle<> handle) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_engine.submit_task(handle);
}

auto context::register_wait(int register_cnt) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_register_count.fetch_add(register_cnt);
}

auto context::unregister_wait(int register_cnt) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_register_count.fetch_sub(register_cnt);
}

auto context::run(stop_token token) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    while (true)
    {
        // 1. 处理任务
        int num = m_engine.num_task_schedule();
        for (int i = 0; i < num; ++i)
        {
            m_engine.exec_one_task();
        }
        // 判断是否有停止信号以及是否达到停止条件
        if (token.stop_requested() && empty_wait_task())
        {
            if (!m_engine.ready())
                break;
            else
                continue;
        }
        // 2. 提交任务
        m_engine.poll_submit();
        if (token.stop_requested() && empty_wait_task() && !m_engine.ready())
            break;
    }
}

}; // namespace coro
