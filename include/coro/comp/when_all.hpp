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
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>

#include "coro/attribute.hpp"
#include "coro/comp/event.hpp"
#include "coro/comp/latch.hpp"
#include "coro/concepts/awaitable.hpp"
#include "coro/concepts/common.hpp"
#include "coro/log.hpp"
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

// template<typename T>
// class when_all_ready_awaitable;

template<typename task_container_type>
class when_all_ready_range_awaitable;
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
                log::debug(
                    "when_all_task_promise::completion_notifier::await_suspend: coro={}, latch={}",
                    reinterpret_cast<uintptr_t>(coro.address()),
                    reinterpret_cast<uintptr_t>(coro.promise().m_latch));
                coro.promise().m_latch->count_down();
            }
            auto await_resume() const noexcept {}
        };
        return completion_notifier{};
    }

    void unhandled_exception() noexcept { log::warn("when_all: unhandled exception"); }

    // TODO: 为何不直接传递指针
    void start(latch& latch)
    {
        log::debug("when_all_task_promise::start: setting latch={}", reinterpret_cast<uintptr_t>(&latch));
        m_latch = &latch;
    }
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
    using storage_type = std::add_pointer_t<return_type>;

    auto get_return_object() noexcept -> decltype(auto);
    /**
     * @brief Sets the pointer to the return value.
     * NOTE: the when_all_task's func start will call this function to set the pointer.
     */
    auto set_pointer(storage_type ptr) -> void { m_data = ptr; };

    /**
     * @brief Sets the return value.
     * NOTE: this func is zero copy by write data to the external storage pointer.
     */
    auto return_value(return_type value) -> void { *m_data = value; };

private:
    storage_type m_data;
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
    explicit when_all_ready_awaitable_base(std::tuple<task_types...>&& tasks) noexcept
        : m_latch(sizeof...(task_types)),
          m_tasks(std::move(tasks)) // 直接移动整个 tuple 赋值给成员变量
    {
    }
    CORO_NO_COPY_MOVE(when_all_ready_awaitable_base);

protected:
    latch                     m_latch;
    std::tuple<task_types...> m_tasks;
};

// DOCS: 模版类型必须先定义了主模版后才能再特化
template<typename T>
class when_all_ready_awaitable;

template<>
class when_all_ready_awaitable<std::tuple<>>
{
public:
    constexpr when_all_ready_awaitable() noexcept {}
    explicit constexpr when_all_ready_awaitable(std::tuple<>) noexcept {}

    constexpr auto await_ready() const noexcept -> bool { return true; }
    constexpr auto await_suspend(std::coroutine_handle<>) noexcept -> bool { return false; }
    constexpr auto await_resume() const noexcept -> std::tuple<> { return {}; }
};

/**
 * @brief void type specialization of when_all_ready_awaitable
 */
template<typename... task_types>
    requires(concepts::all_void_type<typename task_types::rt...>)
class when_all_ready_awaitable<std::tuple<task_types...>> : public when_all_ready_awaitable_base<task_types...>
{
public:
    using when_all_ready_awaitable_base<task_types...>::when_all_ready_awaitable_base;

    auto operator co_await() noexcept
    {
        std::apply([this](auto&&... tasks) { ((tasks.start(this->m_latch)), ...); }, this->m_tasks);
        return this->m_latch.wait();
    }
};

template<typename task_type, typename... task_types>
    requires(
        concepts::all_noref_pod<typename task_type::rt, typename task_types::rt...> &&
        concepts::all_same_type<typename task_type::rt, typename task_types::rt...>)
class when_all_ready_awaitable<std::tuple<task_type, task_types...>>
    : public when_all_ready_awaitable_base<task_type, task_types...> // ? 基类的模版参数定义的是一个，为何能传入两个
{
public:
    // DOCS: 使用array来处理同类型情况，避免CWG#1430缺陷
    using storage_type = std::array<typename task_type::rt, 1 + sizeof...(task_types)>;
    using when_all_ready_awaitable_base<task_type, task_types...>::when_all_ready_awaitable_base;
    /**
     * DOCS: 此处需达成两个需求
     * 1. 调用方阻塞，直到所有任务完成，latch带计数功能，符合需求
     * 2. 任务结束当调用方恢复时，需返回array结果
     */
    auto operator co_await() noexcept
    {
        /**
         * @brief 内部定义awaiter壳子，实际调用latch内部awaiter能力
         * DOCS: 基于装饰器的组合模式，复用已有awaiter能力，避免重复实现
         * 在结束时传递数据完成第二个需求
         * 优点：
         * 1. 复用已有awaiter能力，无需重复实现
         * 2. 在结束时传递数据，避免额外的内存分配
         * 3. 内部结构体，生命周期严格保证在co_await期间
         */
        struct awaiter
        {
            auto await_ready() noexcept -> bool { return m_awaiter.await_ready(); }
            auto await_suspend(std::coroutine_handle<> awaiting_coro) noexcept -> bool
            {
                return m_awaiter.await_suspend(awaiting_coro);
            }
            auto await_resume() noexcept -> decltype(auto)
            {
                m_awaiter.await_resume(); // 清理状态
                return std::move(m_data); // 转移预分配的数组
            }

            latch::event_t::awaiter m_awaiter;
            storage_type&           m_data;
        };

        std::apply(
            [this](auto&... tasks)
            {
                size_t p{0};
                ((tasks.start(this->m_latch, &(this->m_data[p++]))), ...);
                /**
                 * DOCS: 折叠展开如下
                 * task_0.start(latch, &data[0]);
                 * task_1.start(latch, &data[1]);
                 * ...
                 */
            },
            this->m_tasks);
        return awaiter{this->m_latch.wait(), m_data};
    }

private:
    storage_type m_data;
};

template<typename task_container_type>
class when_all_ready_range_awaitable_range_base
{
public:
    explicit when_all_ready_range_awaitable_range_base(task_container_type&& tasks)
        : m_latch(std::ranges::size(tasks)),
          m_tasks(std::move(tasks))
    {
    }

protected:
    latch               m_latch;
    task_container_type m_tasks;
};

template<typename task_container_type>
class when_all_ready_range_awaitable : public when_all_ready_range_awaitable_range_base<task_container_type>
{
public:
    using return_type  = typename std::ranges::range_value_t<task_container_type>::rt;
    using storage_type = std::vector<return_type>;

    auto operator co_await() noexcept
    {
        struct awaiter
        {
            auto await_ready() noexcept -> bool { return m_awaiter.await_ready(); }
            auto await_suspend(std::coroutine_handle<> awaiting_coro) noexcept -> bool
            {
                return m_awaiter.await_suspend(awaiting_coro);
            }
            auto await_resume() noexcept -> decltype(auto)
            {
                m_awaiter.await_resume(); // 清理状态
                return std::move(m_data); // 转移预分配的数组
            }

            latch::event_t::awaiter m_awaiter;
            storage_type&           m_data;
        };
        storage_type data(std::ranges::size(this->m_tasks));
        m_data = data;

        size_t p{0};
        for (auto& tasks : this->m_tasks)
        {
            tasks.start(this->latch, &(this->m_data[p++]));
        }
        return awaiter{this->m_latch.wait(), m_data};
    }

private:
    storage_type m_data;
};

template<typename task_container_type>
    requires(std::is_void_v<typename std::ranges::range_value_t<task_container_type>::rt>)
class when_all_ready_range_awaitable<task_container_type>
    : public when_all_ready_range_awaitable_range_base<task_container_type>
{
public:
    auto operator co_await() noexcept -> latch::event_t::awaiter
    {
        for (auto& tasks : this->m_tasks)
        {
            // 不传指针，只传 latch
            tasks.start(this->m_latch);
        }
        // 直接返回底层的 latch awaiter，不需要组合模式
        return this->m_latch.wait();
    }
};

template<typename return_type>
struct when_all_task
{
public:
    using promise_type     = detail::when_all_task_promise<return_type>;
    using coro_handle_type = std::coroutine_handle<promise_type>;
    using storage_type     = std::add_pointer_t<return_type>;
    using rt               = return_type;

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
        log::debug(
            "when_all_task::start: coro={}, latch={}, has_pointer={}",
            reinterpret_cast<uintptr_t>(m_coro.address()),
            reinterpret_cast<uintptr_t>(&l),
            p != nullptr);
        if constexpr (!std::is_void_v<return_type>)
        {
            m_coro.promise().set_pointer(p);
        }
        m_coro.promise().start(l);
        // NOTE:
        // 使用 submit_to_context 而非 submit_to_scheduler 保证子任务在调用者所在
        // context 执行。submit_to_scheduler 在 kLongRunMode=true 时会通过 dispatcher
        // 将任务分发到其他 context，若那些 context 已停止则子任务永不被执行，导致
        // latch 无法归零而永久挂起。缺点：长运行模式下失去跨 context 的负载均衡，
        // 但 when_all 典型场景是 I/O 密集型（实质并行在内核而非用户态调度层），
        // 因此单 context 顺序执行的影响可以接受。
        submit_to_context(m_coro);
        log::debug("when_all_task::start: submitted to scheduler");
    }

private:
    coro_handle_type m_coro;
};

template<
    concepts::awaitable awaitable,
    typename T = typename concepts::awaitable_traits<awaitable&&>::awaiter_return_type>
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

} // namespace detail

template<
    std::ranges::range  range_type,
    concepts::awaitable awaitable_type = std::ranges::range_value_t<range_type>,
    typename return_type               = typename concepts::awaitable_traits<awaitable_type>::awaiter_return_type>
static auto when_all(range_type&& awaitables)
{
    // 1. 统一包装类型：无论输入什么，内部只持有 vector<when_all_task<T>>
    using task_container_type = std::vector<detail::when_all_task<std::remove_reference_t<return_type>>>;
    task_container_type output_tasks;

    // 2. 性能优化：如果输入容器支持 O(1) 获取大小，提前预留内存
    if constexpr (std::ranges::sized_range<range_type>)
    {
        output_tasks.reserve(std::size(awaitables));
    }

    // 3. 运行期循环：逐个调用 make_when_all_task 进行类型擦除和包装
    for (auto&& a : awaitables)
    {
        output_tasks.emplace_back(detail::make_when_all_task(std::move(a)));
    }

    // 4. 将装有标准任务的 vector 移交给底层的 Awaitable
    return detail::when_all_ready_range_awaitable<task_container_type>(std::move(output_tasks));
}

template<concepts::awaitable... awaitable_type>
[[CORO_TEST_USED(lab5a)]] [[CORO_AWAIT_HINT]] static auto when_all(awaitable_type... awaitables) noexcept
    -> decltype(auto)
{
    // TODO[lab5a] : Add codes if you need,
    // change return type to awaiter implemented by you
    return detail::when_all_ready_awaitable<std::tuple<detail::when_all_task<
        std::remove_reference_t<typename concepts::awaitable_traits<awaitable_type>::awaiter_return_type>>...>>(
        std::make_tuple(detail::make_when_all_task(std::move(awaitables))...));
}

namespace detail
{
template<concepts::conventional_type return_type>
auto when_all_task_promise<return_type>::get_return_object() noexcept -> decltype(auto)
{
    return when_all_task<return_type>{
        when_all_task_promise_base<return_type>::coroutine_handle_type::from_promise(*this)};
}

// template<> NOTE: 该全特化情况无需template
auto when_all_task_promise<void>::get_return_object() noexcept -> decltype(auto)
{
    return when_all_task<void>{when_all_task_promise_base<void>::coroutine_handle_type::from_promise(*this)};
}
} // namespace detail

}; // namespace coro
