// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file tcp.h
/// @brief Minimal message-oriented TCP server (net::tcp::TcpServer).

#include <SocketsHpp/config.h>
#include <SocketsHpp/net/common/socket_server.h>

#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

SOCKETSHPP_NS_BEGIN
namespace net
{
    /**
     * @brief TCP-specific convenience namespace.
     *
     * Provides a minimal message-oriented TCP server for examples and simple use
     * cases. For HTTP, use http::server::HttpServer instead.
     */
    namespace tcp
    {
        /**
         * @brief Minimal TCP server built on net::common::SocketServer.
         *
         * Each chunk of bytes read from a client is passed to the message handler;
         * the string it returns (if non-empty) is sent back on the same connection.
         * Without a handler the server echoes what it receives. The handler runs on
         * the reactor thread (with the connections mutex held), so it must not block;
         * it may insert Connection::Closing into conn.state to close the connection
         * after the response has been sent.
         *
         * @note A "message" is whatever one readable event delivered (up to 64 KiB);
         *       TCP does not preserve message boundaries, so framing is up to the caller.
         *
         * @code
         *   net::tcp::TcpServer server(0);  // ephemeral port on 127.0.0.1
         *   server.onMessage([](const std::string& msg, auto&) { return "echo: " + msg; });
         *   server.Start();
         *   int port = server.port();
         *   ...
         *   server.Stop();
         * @endcode
         */
        class TcpServer : public net::common::SocketServer
        {
        public:
            using Connection = net::common::SocketServer::Connection;  ///< Per-client connection state.
            using SocketAddr = net::common::SocketAddr;                ///< Socket address type.
            using SocketParams = net::common::SocketParams;            ///< Socket parameters type.

            /// @brief Handler for a received chunk of bytes.
            /// @return Bytes to send back on the same connection (empty: no response).
            using MessageHandler = std::function<std::string(const std::string& message, Connection& conn)>;

            /**
             * @brief Bind to host:port and listen (port 0 selects an ephemeral port; see port()).
             * @param port TCP port in host byte order.
             * @param host IPv4/IPv6 literal or hostname (hostnames resolve to IPv4 only).
             * @param backlog listen() backlog.
             * @throws std::invalid_argument for a malformed host,
             *         std::runtime_error if the address cannot be bound.
             * @note Does not start serving; call Start().
             */
            explicit TcpServer(int port = 0, const char* host = "127.0.0.1", int backlog = 10)
                : TcpServer(SocketAddr(host, port), backlog)
            {
            }

            /**
             * @brief Bind to an IPv4 or IPv6 address and listen.
             * @param addr Address to bind (port 0 selects an ephemeral port).
             * @param backlog listen() backlog.
             * @throws std::runtime_error if the address cannot be bound.
             * @note Does not start serving; call Start().
             */
            explicit TcpServer(const SocketAddr& addr, int backlog = 10)
                : net::common::SocketServer(addr, SocketParams{addr.m_data.sa_family, SOCK_STREAM, 0}, backlog)
            {
                if (!is_bound)
                {
                    throw std::runtime_error("TcpServer: failed to bind " + addr.toString());
                }
                onRequest = [this](Connection& conn) { handleRequest(conn); };
            }

            /// @brief Calls Stop() before members used by the reactor thread are destroyed.
            ~TcpServer() override { Stop(); }

            /// @brief Set the message handler (thread-safe; may be changed while running).
            /// @param handler New handler; an empty function restores echo behaviour.
            void onMessage(MessageHandler handler)
            {
                std::lock_guard<std::mutex> lock(m_handlerMutex);
                m_handler = std::move(handler);
            }

            /// @brief Actually bound port (useful when constructed with port 0).
            int port() const { return address().port(); }

        private:
            void handleRequest(Connection& conn)
            {
                MessageHandler handler;
                {
                    std::lock_guard<std::mutex> lock(m_handlerMutex);
                    handler = m_handler;
                }
                if (handler)
                {
                    conn.response_buffer = handler(conn.request_buffer, conn);
                }
                else
                {
                    conn.response_buffer = conn.request_buffer;
                }
                if (!conn.response_buffer.empty())
                {
                    conn.state.insert(Connection::Responding);
                }
            }

            std::mutex m_handlerMutex;
            MessageHandler m_handler;
        };
    }  // namespace tcp
}  // namespace net
SOCKETSHPP_NS_END
