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
}; // namespace detail

// Just make compile success
template<typename... Awaitables>
struct when_all_awaiter
{
    std::tuple<Awaitables...> awaitables;
    std::array<std::coroutine_handle<>, sizeof...(Awaitables)> handles;
    latch latch{sizeof...(Awaitables)};
    std::mutex exception_mutex;
    std::exception_ptr exception;
    std::coroutine_handle<> coro;

    when_all_awaiter(Awaitables&&... aws) noexcept: awaitables(std::move(aws)...) {}

    // 直接挂起
    auto await_ready() -> bool { return false; }
    auto await_suspend(std::coroutine_handle<> h) -> bool {  return false; }
    // auto await_resume() -> std::array<return_type, 1> { return {}; }
};

// template<>
// struct awaiter<void> : detail::noop_awaiter
// {
// public:
//     awaiter(context& ctx) : m_ctx(ctx) {}

// private:
//     context& m_ctx;
// };

template<concepts::awaitable... awaitables_type>
[[CORO_TEST_USED(lab5a)]] [[CORO_AWAIT_HINT]] static auto when_all(awaitables_type... awaitables) noexcept
    -> awaiter<int>
{
    // TODO[lab5a] : Add codes if you need,
    // change return type to awaiter implemented by you

    return {};
}

}; // namespace coro
