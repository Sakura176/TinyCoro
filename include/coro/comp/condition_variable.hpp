/**
 * @file condition_variable.hpp
 * @author JiahuiWang
 * @brief lab5b
 * @version 1.0
 * @date 2025-03-24
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <functional>

#include "coro/attribute.hpp"
#include "coro/comp/mutex.hpp"
#include "coro/spinlock.hpp"

namespace coro
{
/**
 * @brief Welcome to tinycoro lab5b, in this part you will build the basic coroutine
 * synchronization component——condition_variable by modifing condition_variable.hpp
 * and condition_variable.cpp. Please ensure you have read the document of lab5b.
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

using cond_type = std::function<bool()>;

class condition_variable;
using cond_var = condition_variable;

// TODO[lab5b]: This condition_variable is an example to make complie success,
// You should delete it and add your implementation, I don't care what you do,
// but keep the member function and construct function's declaration same with example.
/**
 * TODO: 目标：实现一个基于协程的条件变量，支持wait和notify操作，功能类似std::condition_variable
 * 下述问题需要弄清楚，不可仅完成代码了事：
 *     1.依赖什么手段来判断条件是否完成？轮询会占用CPU资源，此处应该是使用io_uring
 *          理解错误，应该由notify来唤醒
 *     3.理清条件变量的执行过程，思路确定后再清醒代码编写
 */

/**
 * @brief condition_variable is a synchronization primitive that allows one or more coroutines to wait
 * for a condition to become true.
 */
class condition_variable final
{
public:
    struct cv_awaiter : public mutex::mutex_awaiter
    {
        cv_awaiter(context& ctx, mutex& mtx, cond_var& cv) noexcept
            : mutex_awaiter(ctx, mtx),
              m_cv(cv),
              m_suspend_state(false)
        {
        }
        cv_awaiter(context& ctx, mutex& mtx, cond_var& cv, cond_type& cond) noexcept
            : mutex_awaiter(ctx, mtx),
              m_cv(cv),
              m_cond(std::move(cond)),
              m_suspend_state(false)
        {
        }

        /**
         * @brief Suspend the current coroutine and add it to the condition variable's awaiter list.
         */
        auto await_suspend(std::coroutine_handle<> h) noexcept -> bool;

        auto await_resume() noexcept -> void;

        auto resume() noexcept -> void;

        /**
         * @brief Register the lock with the condition variable's awaiter list.
         */
        auto register_lock() noexcept -> bool;

        /**
         * @brief Register self to the condition variable's awaiter list.
         */
        auto register_cv() noexcept -> void;

    private:
        cond_type m_cond;
        cond_var& m_cv;
        bool      m_suspend_state;
    };

public:
    condition_variable() noexcept  = default;
    ~condition_variable() noexcept = default;

    CORO_NO_COPY_MOVE(condition_variable);

    auto wait(mutex& mtx) noexcept -> cv_awaiter;

    /**
     * @brief Wait for the condition variable to be notified, with a predicate.
     *
     * @param mtx The mutex to lock.
     * @param cond The predicate to check.
     * @return cv_awaiter The awaiter to use for waiting.
     */
    auto wait(mutex& mtx, cond_type&& cond) noexcept -> cv_awaiter;

    auto wait(mutex& mtx, cond_type& cond) noexcept -> cv_awaiter;

    auto notify_one() noexcept -> void;

    auto notify_all() noexcept -> void;

private:
    detail::spinlock m_lock;
    cv_awaiter*      m_tail{nullptr};
    cv_awaiter*      m_head{nullptr};
};

}; // namespace coro
