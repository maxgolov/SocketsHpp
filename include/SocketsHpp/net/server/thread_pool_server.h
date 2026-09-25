// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file thread_pool_server.h
/// @brief ThreadPoolServer: a thin standalone wrapper over BS::thread_pool.

#include <SocketsHpp/config.h>
#include <BS_thread_pool.hpp>

#include <atomic>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

SOCKETSHPP_NS_BEGIN
namespace net
{
    namespace server
    {
        /// @brief Thread pool wrapper for offloading request processing from a reactor thread.
        /// @details Manages the BS::thread_pool lifecycle and rejects submissions after
        ///          shutdown(). Intended for running handlers (parsing, business logic) on
        ///          worker threads while a reactor thread handles I/O. Standalone utility:
        ///          HttpServer / MCPServer use BS::thread_pool directly, not this class.
        /// @note All member functions may be called concurrently from any thread.
        ///       Tasks run concurrently with each other, so the code they call must be
        ///       thread-safe.
        class ThreadPoolServer
        {
        public:
            /// @brief Create the pool and start its worker threads immediately.
            /// @param numThreads Number of worker threads (0 = std::thread::hardware_concurrency()).
            explicit ThreadPoolServer(size_t numThreads = 0)
                : m_pool(numThreads == 0 ? std::thread::hardware_concurrency() : numThreads)
                , m_running(true)
            {
            }

            /// @brief Calls shutdown(), blocking until queued and running tasks complete.
            ~ThreadPoolServer()
            {
                shutdown();
            }

            /// @brief Non-copyable.
            ThreadPoolServer(const ThreadPoolServer&) = delete;
            /// @brief Non-copyable.
            ThreadPoolServer& operator=(const ThreadPoolServer&) = delete;

            /// @brief Queue a task and get a future for its result.
            /// @tparam F Callable type
            /// @tparam Args Argument types
            /// @param f Callable to execute
            /// @param args Arguments, decay-copied (or moved) into the task
            /// @return std::future of the callable's result; exceptions thrown by the
            ///         task are rethrown from future::get().
            /// @throws std::runtime_error if shutdown() has been called.
            template<typename F, typename... Args>
            auto submit(F&& f, Args&&... args)
            {
                if (!m_running.load(std::memory_order_acquire))
                {
                    throw std::runtime_error("Thread pool is shutting down");
                }
                // BS::thread_pool tasks take no arguments: bind them into a nullary callable.
                return m_pool.submit_task(bind_args(std::forward<F>(f), std::forward<Args>(args)...));
            }

            /// @brief Queue a fire-and-forget task (no future returned).
            /// @tparam F Callable type
            /// @tparam Args Argument types
            /// @param f Callable to execute
            /// @param args Arguments, decay-copied (or moved) into the task
            /// @throws std::runtime_error if shutdown() has been called.
            /// @warning Exceptions thrown by the task are silently swallowed by the pool.
            template<typename F, typename... Args>
            void detach_task(F&& f, Args&&... args)
            {
                if (!m_running.load(std::memory_order_acquire))
                {
                    throw std::runtime_error("Thread pool is shutting down");
                }
                m_pool.detach_task(bind_args(std::forward<F>(f), std::forward<Args>(args)...));
            }

            /// @brief Get number of threads in pool
            /// @return Thread count
            size_t get_thread_count() const noexcept
            {
                return m_pool.get_thread_count();
            }

            /// @brief Get number of tasks waiting in queue
            /// @return Queue size
            size_t get_tasks_queued() const
            {
                return m_pool.get_tasks_queued();
            }

            /// @brief Get number of tasks currently running
            /// @return Running task count
            size_t get_tasks_running() const
            {
                return m_pool.get_tasks_running();
            }

            /// @brief Get total task count (queued + running)
            /// @return Total task count
            size_t get_tasks_total() const
            {
                return m_pool.get_tasks_total();
            }

            /// @brief Check if pool is running
            /// @return True if accepting tasks
            bool is_running() const noexcept
            {
                return m_running.load(std::memory_order_acquire);
            }

            /// @brief Block until all queued and running tasks have completed.
            /// @warning Deadlocks if called from a pool worker thread.
            void wait_for_tasks()
            {
                m_pool.wait();
            }

            /// @brief Discard queued tasks that have not started (running tasks are unaffected).
            /// @return Number of tasks that were queued just before the purge
            ///         (approximate if tasks are submitted concurrently)
            size_t purge_tasks()
            {
                size_t queued = m_pool.get_tasks_queued();
                m_pool.purge();
                return queued;
            }

            /// @brief Gracefully shut down the pool.
            /// @details Stops accepting new tasks and blocks until queued and running tasks
            ///          complete. Worker threads stay alive until destruction. Idempotent.
            /// @warning Must not be called from a pool worker thread (it would wait on itself).
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
            /// @brief Wrap a callable and its arguments into a nullary callable.
            template<typename F, typename... Args>
            static auto bind_args(F&& f, Args&&... args)
            {
                return [fn = std::forward<F>(f),
                        tup = std::make_tuple(std::forward<Args>(args)...)]() mutable -> decltype(auto) {
                    return std::apply(fn, std::move(tup));
                };
            }

            BS::thread_pool<> m_pool;
            std::atomic<bool> m_running;
        };

    } // namespace server
} // namespace net
SOCKETSHPP_NS_END
