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

#include <array>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <tuple>

#include "coro/attribute.hpp"
#include "coro/comp/latch.hpp"
#include "coro/concepts/awaitable.hpp"

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
template<typename T>
struct result_wrapper
{
    using type = T;
};
template<>
struct result_wrapper<void>
{
    using type = std::monostate;
};
template<typename T>
using result_wrapper_t = typename result_wrapper<T>::type;

// 通用的continuation结构体
template<size_t I, typename Awaitable, typename Awaiter, typename WhenAllAwaiter>
struct Continuation {
    WhenAllAwaiter* self;
    Awaiter awaiter;
    
    void operator()() {
        try {
            // 存储结果
            using return_type = typename concepts::awaitable_traits<Awaitable>::awaiter_return_type;
            if constexpr (!std::is_void_v<return_type>) {
                std::get<I>(self->results) = awaiter.await_resume();
            } else {
                std::get<I>(self->results) = std::monostate{};
            }
        } catch (...) {
            // 捕获异常
            std::lock_guard<std::mutex> lock(self->exception_mutex);
            if (!self->exception) {
                self->exception = std::current_exception();
            }
        }
        
        // 减少计数
        self->latch.count_down();
        
        // 检查是否所有任务都已完成
        if (self->latch.m_count.load(std::memory_order_acquire) == 0) {
            // 恢复主协程
            self->coro.resume();
        }
    }
};
}; // namespace detail

// Just make compile success
template<typename... Awaitables>
struct when_all_awaiter
{
    std::tuple<Awaitables...> awaitables;
    latch latch{sizeof...(Awaitables)};
    std::mutex exception_mutex;
    std::exception_ptr exception;
    std::coroutine_handle<> coro;
    std::tuple<detail::result_wrapper_t<typename concepts::awaitable_traits<Awaitables>::awaiter_return_type>...> results;

    when_all_awaiter(Awaitables&&... aws) noexcept: awaitables(std::forward<Awaitables>(aws)...) {}

    // 直接挂起
    auto await_ready() -> bool { return false; }
    
    // 处理单个可等待对象
    template<size_t I, typename Awaitable>
    void process_awaitable(Awaitable&& awaitable)
    {
        // 获取awaiter
        auto awaiter = concepts::get_awaiter(std::forward<Awaitable>(awaitable));
        
        // 挂起当前协程，当awaitable完成时继续执行
        awaiter.await_suspend(std::coroutine_handle<>::from_address(
            [this, I, awaiter = std::move(awaiter)]() mutable {
                try {
                    // 存储结果
                    using return_type = typename concepts::awaitable_traits<Awaitable>::awaiter_return_type;
                    if constexpr (!std::is_void_v<return_type>) {
                        std::get<I>(results) = awaiter.await_resume();
                    } else {
                        std::get<I>(results) = std::monostate{};
                    }
                } catch (...) {
                    // 捕获异常
                    std::lock_guard<std::mutex> lock(exception_mutex);
                    if (!exception) {
                        exception = std::current_exception();
                    }
                }
                
                // 减少计数
                latch.count_down();
                
                // 检查是否所有任务都已完成
                if (latch.m_count.load(std::memory_order_acquire) == 0) {
                    // 恢复主协程
                    coro.resume();
                }
            }
        ));
    }
    
    // 处理所有可等待对象
    template<size_t... Is, typename... Aws>
    void process_all_awaitables(std::index_sequence<Is...>, Aws&&... aws)
    {
        (process_awaitable<Is>(std::forward<Aws>(aws)), ...);
    }
    
    auto await_suspend(std::coroutine_handle<> h) -> bool 
    {
        coro = h;
        
        // 遍历所有可等待对象
        std::apply([this](auto&&... aws) {
            process_all_awaitables(std::index_sequence_for<Awaitables...>{}, std::forward<decltype(aws)>(aws)...);
        }, awaitables);
        
        return true;
    }
    
    auto await_resume() -> std::tuple<detail::result_wrapper_t<typename concepts::awaitable_traits<Awaitables>::awaiter_return_type>...>
    {
        // 检查是否有异常
        if (exception) {
            std::rethrow_exception(exception);
        }
        
        return results;
    }
};

// 为tuple添加范围循环支持
template<typename... Args>
struct tuple_iterator
{
    const std::tuple<Args...>& tuple;
    size_t index;
    
    tuple_iterator(const std::tuple<Args...>& t, size_t i) : tuple(t), index(i) {}
    
    auto operator*() const {
        return std::apply([this](const auto&... args) {
            return std::get<index>(std::make_tuple(args...));
        }, tuple);
    }
    
    tuple_iterator& operator++() { index++; return *this; }
    
    bool operator!=(const tuple_iterator& other) const { return index != other.index; }
};

template<typename... Args>
auto begin(const std::tuple<Args...>& t) {
    return tuple_iterator<Args...>(t, 0);
}

template<typename... Args>
auto end(const std::tuple<Args...>& t) {
    return tuple_iterator<Args...>(t, sizeof...(Args));
}

// template<>
// struct awaiter<void> : detail::noop_awaiter
// {
// public:
//     awaiter(context& ctx) : m_ctx(ctx) {}

// private:
//     context& m_ctx;
// };

template<concepts::awaitable... awaitables_type>
[[CORO_TEST_USED(lab5a)]] [[CORO_AWAIT_HINT]] static auto when_all(awaitables_type&&... awaitables) noexcept
    -> when_all_awaiter<awaitables_type...>
{
    // TODO[lab5a] : Add codes if you need,
    // change return type to awaiter implemented by you

    return {std::forward<awaitables_type>(awaitables)...};
}

}; // namespace coro
