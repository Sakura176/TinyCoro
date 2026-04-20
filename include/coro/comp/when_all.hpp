/**
 * @file when_all.hpp
 * @author JiahuiWang
 * @brief lab5a
 * @version 1.0
 * @date 2025-03-24
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <coroutine>
#include <cstddef>
#include <exception>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>

#include "coro/attribute.hpp"
#include "coro/comp/latch.hpp"
#include "coro/concepts/awaitable.hpp"
#include "coro/concepts/common.hpp"
#include "coro/scheduler.hpp"
#include "coro/task.hpp"

namespace coro
{
/**
 * @brief Welcome to tinycoro lab5a, in this part you will build the basic coroutine
 * synchronization component - when_all by modifing when_all.hpp.
 * Please ensure you have read the document of lab5a.
 *
 * @warning You should carefully consider whether each implementation should be thread-safe.
 *
 * You should follow the rules below in this part:
 *
 * @note The location marked by todo is where you must add code, but you can also add code anywhere
 * you want, such as function and class definitions, even member variables.
 *
 * @note lab4 and lab5 are free designed lab, leave the interfaces that the test case will use,
 * and then, enjoy yourself!
 */
namespace detail
{
// TODO[lab5a]: Add code that you don't want to use externally in namespace detail

/**
 * NOTE: CRTP静态多态
 * 前向声明（造出一个不完整类型的名字） -> 基类将其作为类型参数生成指针级别的别名
 *      -> 派生类真正定义 -> 模板函数在真正被调用时延迟实例化（此时派生类已完整）。
 */

/**
 * @brief forward declaration of when_all_task_promise.
 */
template<typename return_type>
class when_all_task_promise;

/**
 * @brief Base class for when_all task promises.
 * NOTE: forward declaration and definition later
 * NOTE: template param just for the final_suspend func
 */
template<typename return_type>
struct when_all_task_promise_base
{
protected:
    latch* m_latch{nullptr};

public:
    // NOTE: coroutine_handle 是一个包装了指针的轻量级对象，其指向不完整的类型是合法的
    using coroutine_handle_type = std::coroutine_handle<when_all_task_promise<return_type>>;
    /**
     * @brief coroutine initial suspend, always suspends the coroutine.
     */
    auto initial_suspend() noexcept -> std::suspend_always { return {}; }
    auto final_suspend() noexcept
    {
        struct completion_notifier
        {
            // suspends the coroutine until the latch count reaches zero.
            auto await_ready() const noexcept -> bool { return false; }
            // resumes the coroutine after the latch count reaches zero.
            void await_suspend(coroutine_handle_type coro) const noexcept
            {
                // TODO: 通过handle找到latch，调用count_down；在子任务完成的最后时刻递减计数器，性能高
                coro.promise().m_latch->count_down();
            }
        };
        return completion_notifier{};
    }

    void unhandled_exception() noexcept { log::warn("when_all: unhandled exception"); }

    // TODO: 为何不直接传递指针
    void start(latch& latch) { m_latch = &latch; }
};
template<>
struct when_all_task_promise<void> : public when_all_task_promise_base<void>
{
    auto get_return_object() noexcept -> decltype(auto);
    auto return_void() noexcept -> void {};
};

template<concepts::conventional_type return_type>
struct when_all_task_promise<return_type> : public when_all_task_promise_base<return_type>
{
public:
    // TODO: what diff to T*;
    using storge_type = std::add_pointer_t<return_type>;

    /**
     * @brief Sets the pointer to the return value.
     * NOTE: the when_all_task's func start will call this function to set the pointer.
     */
    auto set_pointer(storge_type ptr) -> void { m_data = ptr; };

    /**
     * @brief Sets the return value.
     * NOTE: this func is zero copy by write data to the external storage pointer.
     */
    auto return_value(return_type value) -> void { *m_data = value; };

private:
    storge_type m_data;
};

template<typename... task_types>
class when_all_ready_awaitable_base
{
public:
    explicit when_all_ready_awaitable_base(task_types&&... tasks) noexcept
        : m_latch(sizeof...(task_types)),
          m_tasks(std::move(tasks)...)
    {
    }

    CORO_NO_COPY_MOVE(when_all_ready_awaitable_base);

private:
    latch                     m_latch;
    std::tuple<task_types...> m_tasks;
};

template<>
class when_all_ready_awaitable<std::tuple<>>
{
    constexpr auto await_ready() const noexcept -> bool { return true; }
    constexpr auto await_suspend() noexcept -> void {}
    constexpr auto await_resume() const noexcept -> std::tuple<> { return {}; }
};

template<typename... task_types>
requires(concepts::all_void_type<typename task_types::rt...>)
class when_all_ready_awaitable<std::tuple<task_types...>> : public when_all_ready_awaitable_base<task_types...>
{
public:
    auto operator co_await() noexcept
    {
        std::apply([this](auto&&... tasks) { ((tasks.start(this->m_latch)), ...); }, this->m_tasks);
        return this->m_latch.wait();
    }
};
} // namespace detail

template<typename return_type>
struct when_all_task
{
public:
    using promise_type     = detail::when_all_task_promise<return_type>;
    using coro_handle_type = std::coroutine_handle<promise_type>;
    using storage_type     = std::add_pointer_t<return_type>;

    explicit when_all_task(coro_handle_type coro) noexcept : m_coro(coro) {}
    when_all_task(const when_all_task&) = delete;
    when_all_task(when_all_task&& other) noexcept : m_coro(other.m_coro) { other.m_coro = nullptr; }
    when_all_task& operator=(const when_all_task&) = delete;

    // TODO: why move operator= need to be deleted
    when_all_task& operator=(when_all_task&& other) = delete;

    ~when_all_task()
    {
        if (m_coro)
            m_coro.destroy();
    }

    void start(latch& l, storage_type p = nullptr) noexcept
    {
        if constexpr (!std::is_void_v<return_type>)
        {
            m_coro.promise().set_pointer(p);
        }
        m_coro.promise().start(l);
        submit_to_scheduler(m_coro);
    }

private:
    coro_handle_type m_coro;
};

template<
    concepts::awaitable awaitable,
    typename T = typename concepts::awaitable_traits<awaitable&&>::awaitable_return_type>
static auto make_when_all_task(awaitable&& a) -> when_all_task<T>
{
    if constexpr (std::is_void_v<T>)
    {
        co_await std::forward<awaitable>(a);
        co_return;
    }
    else
    {
        co_return co_await std::forward<awaitable>(a);
    }
}

template<concepts::awaitable... awaitables_type>
[[CORO_TEST_USED(lab5a)]] [[CORO_AWAIT_HINT]] static auto when_all(awaitables_type&&... awaitables) noexcept
    -> when_all_awaiter<awaitables_type...>
{
    // TODO[lab5a] : Add codes if you need,
    // change return type to awaiter implemented by you

    return {std::forward<awaitables_type>(awaitables)...};
}

}; // namespace coro
