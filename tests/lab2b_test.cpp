#include <algorithm>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <vector>

#include "coro/log.hpp"
#include "coro/net/io_awaiter.hpp"
#include "coro/scheduler.hpp"
#include "gtest/gtest.h"

using namespace coro;

/*************************************************************
 *                       pre-definition                      *
 *************************************************************/

using ::coro::net::noop_awaiter;

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

class ContextTest : public ::testing::Test
{
protected:
    void SetUp() override {}

    void TearDown() override {}

    context          m_ctx;
    std::vector<int> m_vec;
};

class ContextRunTaskTest : public ::testing::TestWithParam<int>
{
protected:
    void SetUp() override {}

    void TearDown() override {}

    context          m_ctx;
    std::vector<int> m_vec;
};

class ContextMultiThreadAddTaskTest : public ContextRunTaskTest
{
};

class ContextAddNopIOTest : public ContextRunTaskTest
{
};

class SchedulerRunTaskTest : public ::testing::TestWithParam<int>
{
protected:
    void SetUp() override {}

    void TearDown() override {}

    std::vector<int> m_vec;
    std::mutex       m_mtx;
};

class SchedulerAddNopIOTest : public SchedulerRunTaskTest
{
};

task<> func(std::vector<int>& vec, int val)
{
    vec.push_back(val);
    co_return;
}

task<> func_nop(std::vector<int>& vec, int val)
{
    co_await noop_awaiter{};
    vec.push_back(val);
    co_return;
}

task<> mutex_func(std::vector<int>& vec, int val, std::mutex& mtx)
{
    mtx.lock();
    vec.push_back(val);
    mtx.unlock();
    co_return;
}

task<> mutex_func_nop(std::vector<int>& vec, int val, std::mutex& mtx)
{
    co_await noop_awaiter{};
    mtx.lock();
    vec.push_back(val);
    mtx.unlock();
    co_return;
}

/*************************************************************
 *                          tests                            *
 *************************************************************/

TEST_F(ContextTest, InitState)
{
    m_ctx.init();

    // ASSERT_TRUE(m_ctx.empty_wait_task());
    ASSERT_EQ(m_ctx.get_ctx_id(), local_context().get_ctx_id());

    m_ctx.deinit();
}

TEST_F(ContextTest, AddRegisterWait)
{
    m_ctx.init();

    m_ctx.register_wait();
    // ASSERT_FALSE(m_ctx.empty_wait_task());
    m_ctx.unregister_wait();
    // ASSERT_TRUE(m_ctx.empty_wait_task());

    m_ctx.register_wait(0);
    // ASSERT_TRUE(m_ctx.empty_wait_task());

    m_ctx.deinit();
}

TEST_F(ContextTest, SubmitTask)
{
    m_ctx.init();

    auto task = func(m_vec, 1);
    m_ctx.submit_task(task);

    m_ctx.deinit();
}

TEST_F(ContextTest, SubmitMoveTask)
{
    m_ctx.init();

    auto task   = func(m_vec, 1);
    auto handle = task.handle();
    m_ctx.submit_task(std::move(task));

    m_ctx.deinit();
    handle.destroy();
}

TEST_P(ContextRunTaskTest, RunTask)
{
    const int task_num = GetParam();

    for (int i = 0; i < task_num; i++)
    {
        m_ctx.submit_task(func(m_vec, i));
    }

    m_ctx.start();
    m_ctx.notify_stop();
    m_ctx.join();

    ASSERT_EQ(m_vec.size(), task_num);
    std::sort(m_vec.begin(), m_vec.end());
    for (int i = 0; i < task_num; i++)
    {
        ASSERT_EQ(m_vec[i], i);
    }
}

INSTANTIATE_TEST_SUITE_P(ContextRunTaskTests, ContextRunTaskTest, ::testing::Values(1, 10, 100, 10000));

TEST_P(ContextMultiThreadAddTaskTest, MultiThreadAddTask)
{
    const int thread_num = GetParam();

    std::vector<std::thread> vec;
    for (int i = 0; i < thread_num; i++)
    {
        vec.push_back(std::thread([&, i]() { m_ctx.submit_task(func(m_vec, i)); }));
    }
    for (int i = 0; i < thread_num; i++)
    {
        vec[i].join();
    }

    m_ctx.start();
    m_ctx.notify_stop();
    m_ctx.join();

    ASSERT_EQ(m_vec.size(), thread_num);
    std::sort(m_vec.begin(), m_vec.end());
    for (int i = 0; i < thread_num; i++)
    {
        ASSERT_EQ(m_vec[i], i);
    }
}

INSTANTIATE_TEST_SUITE_P(
    ContextMultiThreadAddTaskTests, ContextMultiThreadAddTaskTest, ::testing::Values(1, 10, 100, 1000));

TEST_P(ContextAddNopIOTest, AddNopIO)
{
    const int task_num = GetParam();

    for (int i = 0; i < task_num; i++)
    {
        m_ctx.submit_task(func_nop(m_vec, i));
    }
    log::debug("before m_ctx start");
    m_ctx.start();
    log::debug("after m_ctx start");
    m_ctx.notify_stop();
    m_ctx.join();

    ASSERT_EQ(m_vec.size(), task_num);
    std::sort(m_vec.begin(), m_vec.end());
    for (int i = 0; i < task_num; i++)
    {
        ASSERT_EQ(m_vec[i], i);
    }
}

INSTANTIATE_TEST_SUITE_P(ContextAddNopIOTests, ContextAddNopIOTest, ::testing::Values(1, 10, 100, 10000));

TEST_P(SchedulerRunTaskTest, RunTask)
{
    const int task_num = GetParam();
    scheduler::init();

    for (int i = 0; i < task_num; i++)
    {
        submit_to_scheduler(mutex_func(m_vec, i, m_mtx));
    }

    scheduler::start();

    scheduler::loop(false);

    ASSERT_EQ(m_vec.size(), task_num);
    std::sort(m_vec.begin(), m_vec.end());
    for (int i = 0; i < task_num; i++)
    {
        ASSERT_EQ(m_vec[i], i);
    }
}

INSTANTIATE_TEST_SUITE_P(SchedulerRunTaskTests, SchedulerRunTaskTest, ::testing::Values(1, 10, 100, 10000));

TEST_P(SchedulerAddNopIOTest, AddNopIO)
{
    const int task_num = GetParam();
    scheduler::init();

    for (int i = 0; i < task_num; i++)
    {
        submit_to_scheduler(mutex_func_nop(m_vec, i, m_mtx));
    }

    scheduler::start();

    scheduler::loop(false);

    ASSERT_EQ(m_vec.size(), task_num);
    std::sort(m_vec.begin(), m_vec.end());
    for (int i = 0; i < task_num; i++)
    {
        ASSERT_EQ(m_vec[i], i);
    }
}

INSTANTIATE_TEST_SUITE_P(SchedulerAddNopIOTests, SchedulerAddNopIOTest, ::testing::Values(1, 10, 100, 10000));
