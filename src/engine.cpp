#include "coro/engine.hpp"
#include "config.h"
#include "coro/log.hpp"
#include "coro/meta_info.hpp"
#include "coro/net/io_info.hpp"
#include "coro/task.hpp"
#include <cassert>
#include <coroutine>

namespace coro::detail
{
using std::memory_order_relaxed;

auto engine::init() noexcept -> void
{
    // TODO[lab2a]: Add you codes
    linfo.egn              = this;
    m_running_io_count     = 0;
    m_wait_submit_io_count = 0;
    m_upxy.init(config::kEntryLength);
}

auto engine::deinit() noexcept -> void
{
    // TODO[lab2a]: Add you codes
    m_upxy.deinit();
    m_running_io_count     = 0;
    m_wait_submit_io_count = 0;
    linfo.egn              = nullptr;
    mpmc_queue<coroutine_handle<>> task_queue;
    m_task_queue.swap(task_queue);
}

auto engine::ready() noexcept -> bool
{
    // TODO[lab2a]: Add you codes
    return !m_task_queue.was_empty();
}

auto engine::get_free_urs() noexcept -> ursptr
{
    // TODO[lab2a]: Add you codes
    return m_upxy.get_free_sqe();
}

auto engine::num_task_schedule() noexcept -> size_t
{
    // TODO[lab2a]: Add you codes
    return m_task_queue.was_size();
}

auto engine::schedule() noexcept -> coroutine_handle<>
{
    // TODO[lab2a]: Add you codes
    auto coro = m_task_queue.pop();
    assert(coro != nullptr);
    return coro;
}

auto engine::submit_task(coroutine_handle<> handle) noexcept -> void
{
    // TODO[lab2a]: Add you codes
    assert(handle != nullptr && "submit null coroutine");
    m_task_queue.push(handle);
    // 写入增量到ev_fd，必须先写入再读取，否则读取会阻塞
    wake_up();
}

auto engine::exec_one_task() noexcept -> void
{
    auto coro = schedule();
    coro.resume();
    if (coro.done())
    {
        clean(coro);
    }
}

auto engine::handle_cqe_entry(urcptr cqe) noexcept -> void
{
    auto data = reinterpret_cast<net::detail::io_info*>(io_uring_cqe_get_data(cqe));
    data->cb(data, cqe->res);
}

auto engine::poll_submit() noexcept -> void
{
    // TODO[lab2a]: Add you codes
    // 先提交待处理事件
    int wait_submit_io_num = m_wait_submit_io_count.load();
    if (wait_submit_io_num > 0)
    {
        int num = m_upxy.submit();
        m_running_io_count.fetch_add(num);
        m_wait_submit_io_count.store(0);
    }
    // 获取已完成事件数
    auto ev_num = m_upxy.wait_eventfd();
    int  num    = m_upxy.peek_batch_cqe(m_urc.data(), ev_num);
    for (int i = 0; i < num; ++i)
    {
        handle_cqe_entry(m_urc[i]);
    }
    m_upxy.cq_advance(num);
    m_running_io_count.fetch_sub(num);
}

auto engine::add_io_submit() noexcept -> void
{
    // TODO[lab2a]: Add you codes
    m_wait_submit_io_count.fetch_add(1);
    m_upxy.write_eventfd(1);
}

auto engine::empty_io() noexcept -> bool
{
    // TODO[lab2a]: Add you codes
    return m_running_io_count == 0 && m_wait_submit_io_count == 0;
}

auto engine::wake_up() noexcept -> void
{
    m_upxy.write_eventfd(1);
}
}; // namespace coro::detail
