#include "coro/context.hpp"
#include "coro/log.hpp"
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
    log::debug("begin context::notify_stop");
    m_job->request_stop();
    m_engine.wake_up();
    log::debug("end context::notify_stop");
}

auto context::submit_task(std::coroutine_handle<> handle) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    log::debug("context::submit_task: ctx_id={}, handle={}", m_id, reinterpret_cast<uintptr_t>(handle.address()));
    m_engine.submit_task(handle);
}

auto context::register_wait(int register_cnt) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    auto old_count = m_register_count.fetch_add(register_cnt);
    log::debug("context::register_wait: ctx_id={}, old_count={}, new_count={}, delta={}", 
               m_id, old_count, old_count + register_cnt, register_cnt);
}

auto context::unregister_wait(int register_cnt) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    auto old_count = m_register_count.fetch_sub(register_cnt);
    log::debug("context::unregister_wait: ctx_id={}, old_count={}, new_count={}, delta={}", 
               m_id, old_count, old_count - register_cnt, register_cnt);
}

auto context::run(stop_token token) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    log::debug("context::run: starting, ctx_id={}", m_id);
    int loop_count = 0;
    while (true)
    {
        loop_count++;
        // 1. 处理任务
        int num = m_engine.num_task_schedule();
        log::debug("context::run: loop={}, ctx_id={}, tasks_to_schedule={}, register_count={}, stop_requested={}", 
                   loop_count, m_id, num, m_register_count.load(), token.stop_requested());
        for (int i = 0; i < num; ++i)
        {
            m_engine.exec_one_task();
        }
        // 判断是否有停止信号以及是否达到停止条件
        bool empty = empty_wait_task();
        log::debug("context::run: empty_wait_task={} (register_count={}, empty_io={})", 
                   empty, m_register_count.load(), m_engine.empty_io());
        if (token.stop_requested() && empty)
        {
            bool engine_ready = m_engine.ready();
            log::debug("context::run: stop requested and empty, engine_ready={}", engine_ready);
            if (!engine_ready)
            {
                log::debug("context::run: breaking loop, ctx_id={}", m_id);
                break;
            }
            else
            {
                log::debug("context::run: engine ready, continuing");
                continue;
            }
        }
        // 2. 提交任务
        m_engine.poll_submit();
        bool engine_ready = m_engine.ready();
        if (token.stop_requested() && empty && !engine_ready)
        {
            log::debug("context::run: stop requested, empty, and engine not ready, breaking loop, ctx_id={}", m_id);
            break;
        }
        if (loop_count % 100 == 0)
        {
            log::debug("context::run: still running, loop={}, ctx_id={}", loop_count, m_id);
        }
    }
    log::debug("context::run: exiting, ctx_id={}", m_id);
}

}; // namespace coro
