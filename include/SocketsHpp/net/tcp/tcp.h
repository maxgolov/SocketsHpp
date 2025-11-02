// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <SocketsHpp/net/common/socket_server.h>
#include <functional>
#include <string>

SOCKETSHPP_NS_BEGIN
namespace net
{
    /**
     * @brief TCP-specific convenience namespace
     * 
     * Provides simplified TCP server API for examples and simple use cases.
     * For production use, consider using HttpServer instead.
     */
    namespace tcp
    {
        using namespace net::common;

        /**
         * @brief TCP Socket Connection wrapper
         * 
         * Simplified interface for handling TCP connections in server callbacks.
         */
        template<typename TProtocol = void>
        struct SocketConnection
        {
            using Connection = typename SocketServer::Connection;
            Connection& conn;
            SocketServer& server;

            SocketConnection(Connection& c, SocketServer& s) : conn(c), server(s) {}

            /// Get remote client address as string
            std::string remoteAddress() const
            {
                return conn.client.toString();
            }

            /// Send data to client
            void send(const std::string& data)
            {
                conn.responseBuffer.insert(conn.responseBuffer.end(), data.begin(), data.end());
                conn.state.insert(SocketServer::Connection::Responding);
            }

            /// Close the connection
            void close()
            {
                conn.state.insert(SocketServer::Connection::Closing);
            }

            /// Set callback for incoming message
            void onMessage(std::function<void(const std::string&)> callback)
            {
                conn.messageCallback = callback;
            }

            /// Set callback for disconnect
            void onDisconnect(std::function<void()> callback)
            {
                conn.disconnectCallback = callback;
            }
        };

        /**
         * @brief Simplified TCP Server
         * 
         * High-level TCP server for examples. For production, use HttpServer.
         */
        template<typename TProtocol = void>
        struct SocketServer : public net::common::SocketServer
        {
            using Connection = typename net::common::SocketServer::Connection;

            std::function<void(SocketConnection<TProtocol>&)> connectCallback;

            explicit SocketServer(int port)
                : net::common::SocketServer(
                    SocketAddr{"0.0.0.0:" + std::to_string(port)},
                    SocketParams{AF_INET, SOCK_STREAM, 0},
                    10)
            {
                // Set up request handler to call user callbacks
                onRequest = [this](Connection& conn) {
                    if (conn.messageCallback) {
                        std::string message(conn.requestBuffer.begin(), conn.requestBuffer.end());
                        conn.messageCallback(message);
                        conn.requestBuffer.clear();
                    }
                };

                // Set up response handler
                onResponse = [this](Connection& conn) {
                    // Response data already in conn.responseBuffer via send()
                };

                // Set up accept handler to call connect callback
                onAccept = [this](Connection& conn) {
                    if (connectCallback) {
                        SocketConnection<TProtocol> wrapper(conn, *this);
                        connectCallback(wrapper);
                    }
                };

                // Set up close handler to call disconnect callback
                onClose = [this](Connection& conn) {
                    if (conn.disconnectCallback) {
                        conn.disconnectCallback();
                    }
                };
            }

            /// Set callback for new connections
            void onConnect(std::function<void(SocketConnection<TProtocol>&)> callback)
            {
                connectCallback = callback;
            }

            /// Start the server (blocking)
            void run()
            {
                reactor.run();
            }
        };
    }
}
SOCKETSHPP_NS_END
