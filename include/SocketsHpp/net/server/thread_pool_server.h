// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <BS_thread_pool.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

SOCKETSHPP_NS_BEGIN
namespace net
{
    namespace server
    {
        /// @brief Thread pool wrapper for HTTP/MCP server request processing
        /// @details Manages BS::thread_pool lifecycle and provides safe task submission.
        ///          The reactor pattern handles I/O in the main thread, while this pool
        ///          processes CPU-intensive tasks (parsing, business logic) in worker threads.
        class ThreadPoolServer
        {
        public:
            /// @brief Create thread pool with specified number of threads
            /// @param numThreads Number of worker threads (0 = hardware concurrency)
            explicit ThreadPoolServer(size_t numThreads = 0)
                : m_pool(numThreads == 0 ? std::thread::hardware_concurrency() : numThreads)
                , m_running(true)
            {
            }

            /// @brief Destroy thread pool and wait for all tasks to complete
            ~ThreadPoolServer()
            {
                shutdown();
            }

            // Prevent copying
            ThreadPoolServer(const ThreadPoolServer&) = delete;
            ThreadPoolServer& operator=(const ThreadPoolServer&) = delete;

            /// @brief Submit task to thread pool
            /// @tparam F Callable type
            /// @tparam Args Argument types
            /// @param f Callable to execute
            /// @param args Arguments to pass to callable
            /// @return Future for retrieving result
            template<typename F, typename... Args>
            auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>
            {
                if (!m_running.load(std::memory_order_acquire))
                {
                    throw std::runtime_error("Thread pool is shutting down");
                }
                return m_pool.submit(std::forward<F>(f), std::forward<Args>(args)...);
            }

            /// @brief Submit detached task (fire-and-forget, no future returned)
            /// @tparam F Callable type
            /// @tparam Args Argument types
            /// @param f Callable to execute
            /// @param args Arguments to pass to callable
            template<typename F, typename... Args>
            void detach_task(F&& f, Args&&... args)
            {
                if (!m_running.load(std::memory_order_acquire))
                {
                    throw std::runtime_error("Thread pool is shutting down");
                }
                m_pool.detach_task(std::forward<F>(f), std::forward<Args>(args)...);
            }

            /// @brief Get number of threads in pool
            /// @return Thread count
            size_t get_thread_count() const noexcept
            {
                return m_pool.get_thread_count();
            }

            /// @brief Get number of tasks waiting in queue
            /// @return Queue size
            size_t get_tasks_queued() const noexcept
            {
                return m_pool.get_tasks_queued();
            }

            /// @brief Get number of tasks currently running
            /// @return Running task count
            size_t get_tasks_running() const noexcept
            {
                return m_pool.get_tasks_running();
            }

            /// @brief Get total task count (queued + running)
            /// @return Total task count
            size_t get_tasks_total() const noexcept
            {
                return m_pool.get_tasks_total();
            }

            /// @brief Check if pool is running
            /// @return True if accepting tasks
            bool is_running() const noexcept
            {
                return m_running.load(std::memory_order_acquire);
            }

            /// @brief Wait for all queued tasks to complete
            void wait_for_tasks()
            {
                m_pool.wait();
            }

            /// @brief Purge all pending tasks from queue
            /// @return Number of tasks purged
            size_t purge_tasks()
            {
                return m_pool.purge();
            }

            /// @brief Gracefully shutdown thread pool
            /// @details Stops accepting new tasks and waits for queued tasks to complete
            void shutdown()
            {
                bool expected = true;
                if (m_running.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
                {
                    // Wait for all tasks to complete before destruction
                    m_pool.wait();
                }
            }

        private:
            BS::thread_pool m_pool;
            std::atomic<bool> m_running;
        };

    } // namespace server
} // namespace net
SOCKETSHPP_NS_END
