// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <SocketsHpp/net/server/thread_pool_server.h>

#include <atomic>
#include <stdexcept>
#include <string>

using SOCKETSHPP_NS::net::server::ThreadPoolServer;

namespace
{
    TEST(ThreadPoolServerTest, SubmitWithArgumentsReturnsResult)
    {
        ThreadPoolServer pool(2);
        auto sum = pool.submit([](int a, int b) { return a + b; }, 2, 3);
        EXPECT_EQ(sum.get(), 5);

        auto text = pool.submit([](const std::string& s) { return s + "!"; }, std::string("hi"));
        EXPECT_EQ(text.get(), "hi!");

        auto nothing = pool.submit([] {});
        nothing.get();
    }

    TEST(ThreadPoolServerTest, DetachTaskAndWait)
    {
        ThreadPoolServer pool(2);
        std::atomic<int> total{0};
        for (int i = 1; i <= 10; i++)
        {
            pool.detach_task([&total](int k) { total += k; }, i);
        }
        pool.wait_for_tasks();
        EXPECT_EQ(total.load(), 55);
        EXPECT_EQ(pool.get_thread_count(), 2u);
        EXPECT_EQ(pool.get_tasks_total(), 0u);
        EXPECT_EQ(pool.get_tasks_queued(), 0u);
        EXPECT_EQ(pool.get_tasks_running(), 0u);
        EXPECT_EQ(pool.purge_tasks(), 0u);
    }

    TEST(ThreadPoolServerTest, ShutdownRejectsNewTasks)
    {
        ThreadPoolServer pool(1);
        EXPECT_TRUE(pool.is_running());
        pool.shutdown();
        EXPECT_FALSE(pool.is_running());
        EXPECT_THROW(pool.detach_task([] {}), std::runtime_error);
        EXPECT_THROW((void)pool.submit([] { return 1; }), std::runtime_error);
        pool.shutdown();  // idempotent
    }
}  // namespace
