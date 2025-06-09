#include "coro/scheduler.hpp"

namespace coro
{
auto scheduler::init_impl(size_t ctx_cnt) noexcept -> void
{
    m_ctx_cnt = ctx_cnt;
    m_ctxs    = detail::ctx_container{};
    m_ctxs.reserve(m_ctx_cnt);
    for (int i = 0; i < m_ctx_cnt; i++)
    {
        m_ctxs.emplace_back(std::make_unique<context>());
    }
    m_dispatcher.init(m_ctx_cnt, &m_ctxs);
}

auto scheduler::start_impl() noexcept -> void
{
    for (int i = 0; i < m_ctx_cnt; i++)
    {
        m_ctxs[i]->start();
    }
}

auto scheduler::loop_impl(bool long_run_mode) noexcept -> void
{
    if (long_run_mode)
    {
        join_impl();
    }
    else
    {
        stop_impl();
    }
}

auto scheduler::stop_impl() noexcept -> void
{
    for (int i = 0; i < m_ctx_cnt; i++)
    {
        m_ctxs[i]->notify_stop();
    }

    for (int i = 0; i < m_ctx_cnt; i++)
    {
        m_ctxs[i]->join();
    }
}

auto scheduler::join_impl() noexcept -> void
{
    m_ctxs[0]->join();
}

auto scheduler::submit_task_impl(std::coroutine_handle<> handle) noexcept -> void
{
    size_t ctx_id = m_dispatcher.dispatch();
    m_ctxs[ctx_id]->submit_task(handle);
}
}; // namespace coro
