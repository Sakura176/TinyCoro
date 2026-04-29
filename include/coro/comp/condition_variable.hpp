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
 * synchronization component -- condition_variable by modifying condition_variable.hpp
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

/**
 * NOTE: condition_variable implementation key points
 *   1. cv_awaiter extends mutex_awaiter, reusing the mutex waiter queue mechanism
 *   2. resume() is virtual (declared in mutex_awaiter), ensuring correct dispatch
 *      when mutex::unlock() transfers the lock to a CV waiter
 *   3. m_mutex_acquired flag distinguishes two resume() call sources:
 *      - called from notify_one/notify_all  -> need to acquire mutex first
 *      - called from mutex::unlock() transfer -> already hold mutex, resume directly
 *   4. register_lock() releases the mutex; resume() re-acquires it,
 *      guaranteeing the mutex is held when wait() returns
 *   5. The CV waiter linked list (m_head/m_tail) is protected by m_lock;
 *      notify_all detaches the list under lock before iterating
 *   6. Predicate is re-checked in resume(); unsatisfied waiters rejoin the CV queue
 *      (this handles spurious wakeups from notify_all)
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
        // NOTE: true => next resume() is from mutex::unlock() lock transfer,
        // skip try_lock and directly resume the coroutine
        bool      m_mutex_acquired{false};
        // NOTE: tracks whether a wait has been registered on context,
        // used by await_resume to correctly unregister
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
