#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "config.h"
#include "coro/dispatcher.hpp"

namespace coro
{

/**
 * @brief scheduler just control context to run and stop,
 * it also use dispatcher to decide which context can accept the task
 *
 */
class scheduler
{
    friend context;

public:
    inline static auto init(size_t ctx_cnt = std::thread::hardware_concurrency()) noexcept -> void
    {
        if (ctx_cnt == 0)
        {
            ctx_cnt = std::thread::hardware_concurrency();
        }
        get_instance()->init_impl(ctx_cnt);
    }

    /**
     * @brief start all context
     *
     */
    inline static auto start() noexcept -> void { get_instance()->start_impl(); }

    /**
     * @brief if long_run_mode is true, scheduler will never let context stop,
     * otherwise scheduler will send stop signal first to context
     *
     * @param long_run_mode
     */
    inline static auto loop(bool long_run_mode = config::kLongRunMode) noexcept -> void
    {
        get_instance()->loop_impl(long_run_mode);
    }

    static inline auto submit(task<void>&& task) noexcept -> void
    {
        auto handle = task.handle();
        task.detach();
        submit(handle);
    }

    static inline auto submit(task<void>& task) noexcept -> void { submit(task.handle()); }

    inline static auto submit(std::coroutine_handle<> handle) noexcept -> void
    {
        get_instance()->submit_task_impl(handle);
    }

private:
    static auto get_instance() noexcept -> scheduler*
    {
        static scheduler sc;
        return &sc;
    }

    auto init_impl(size_t ctx_cnt) noexcept -> void;

    auto start_impl() noexcept -> void;

    auto loop_impl(bool long_run_mode) noexcept -> void;

    auto stop_impl() noexcept -> void;

    auto join_impl() noexcept -> void;

    auto submit_task_impl(std::coroutine_handle<> handle) noexcept -> void;

private:
    size_t                                              m_ctx_cnt{0};
    detail::ctx_container                               m_ctxs;
    detail::dispatcher<coro::config::kDispatchStrategy> m_dispatcher;
};

inline void submit_to_scheduler(task<void>&& task) noexcept
{
    if (config::kLongRunMode)
    {
        scheduler::submit(std::move(task));
    }
    else
    {
        submit_to_context(std::move(task));
    }
}

inline void submit_to_scheduler(task<void>& task) noexcept
{
    if (config::kLongRunMode)
    {
        scheduler::submit(task.handle());
    }
    else
    {
        submit_to_context(task.handle());
    }
}

inline void submit_to_scheduler(std::coroutine_handle<> handle) noexcept
{
    if (config::kLongRunMode)
    {
        scheduler::submit(handle);
    }
    else
    {
        submit_to_context(handle);
    }
}

}; // namespace coro
