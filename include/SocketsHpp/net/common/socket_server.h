// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "./socket_tools.h"

SOCKETSHPP_NS_BEGIN
namespace net
{
    namespace common {

        using Reactor = net::utils::Reactor;
        using Socket = net::utils::Socket;
        using SocketAddr = net::utils::SocketAddr;
        using SocketParams = net::utils::SocketParams;

        /**
         * @brief Common Server for TCP, UDP and Unix Domain.
         */
        struct SocketServer : public Reactor::SocketCallback
        {
            struct Connection
            {
                enum State
                {
                    Idle,        // No data transfer initiated
                    Receiving,   // Receiving data
                    Responding,  // Sending data
                    Closing,     // Closing connection
                    Closed,      // Closed connection
                    Aborted      // Connection aborted
                };

                Socket socket;      // Active client-server socket
                SocketAddr client;  // Client address

                std::string request_buffer;   // Receive buffer for current event
                std::string response_buffer;  // Send buffer for current event

                std::set<State> state;  // Current connection state
                bool keepalive{ true };   // Keep connection alive (reserved for future use)
            };

            SocketAddr bind_address;  // Server bind address
            bool is_bound{ false };
            SocketParams server_socket_params;  // Server socket params
            Socket server_socket;               // Server listening socket
            Reactor reactor;                    // Socket event handler

            // Custom callback when server receives data
            std::function<void(Connection& conn)> onRequest;

            // Custom callback when server sends a response
            std::function<void(Connection& conn)> onResponse;

            // Active client-server connections protected by recursive mutex
            std::recursive_mutex connections_mutex;
            std::map<Socket, Connection> connections;

            // Macro to safely obtain TEMPORARY string buffer pointer
#define CLID(conn) conn.client.toString().c_str()

            const SocketAddr& address() const { return bind_address; };

            /**
             * @brief Route to start TCP, UDP or Unix Domain socket server.
             * @param addr Address or Unix domain socket name to bind to.
             * @param sock Socket type.
             * @param numConnections Maximum number of connections.
             */
            SocketServer(SocketAddr addr, SocketParams params, int numConnections = 10)
                : bind_address(addr), server_socket_params(params), reactor(*this)
            {
                server_socket = Socket(server_socket_params);

                // Default lambda here implements an echo server
                onRequest = [this](Connection& conn) {
                    conn.state.insert(SocketServer::Connection::Responding);
                };

                onResponse = [this](Connection&) {
                    // Empty response
                };

                int rc = server_socket.bind(bind_address);
                if (rc != 0)
                {
                    LOG_ERROR("Server: bind failed! result=%d", rc);
                    return;
                }

                is_bound = true;
                LOG_INFO("Server: bind successful. result=%d", rc);
                server_socket.getsockname(bind_address);
                if (server_socket_params.type == SOCK_STREAM)
                {
                    // In TCP and Unix Domain mode we listen and accept.
                    reactor.addSocket(server_socket, Reactor::Acceptable);
                    server_socket.listen(numConnections);
                }
                else
                {
                    // In UDP mode we read in a loop, no need to accept.
                    // Set socket to non-blocking so recvfrom() doesn't block indefinitely
                    server_socket.setNonBlocking();
                    reactor.addSocket(server_socket, Reactor::Readable);
                }

                LOG_INFO("Server: Listening on %s://%s", server_socket_params.scheme(),
                    bind_address.toString().c_str());
            }

            /**
             * @brief Start server.
             */
            void Start() { reactor.start(); }

            /**
             * @brief Stop server.
             */
            void Stop() { reactor.stop(); }

            /**
             * @brief Handle Reactor::State::Acceptable event.
             * @param socket Client socket.
             */
            virtual void onSocketAcceptable(Socket socket) override
            {
                LOG_TRACE("Server: accepting socket fd=0x%llx", socket.m_sock);

                Socket csocket;
                SocketAddr caddr;
                if (socket.accept(csocket, caddr))
                {
#ifdef HAVE_UNIX_DOMAIN
                    // If server is Unix domain, then the client socket is also Unix domain
                    if (bind_address.isUnixDomain)
                    {
                        caddr.isUnixDomain = bind_address.isUnixDomain;
                        // Sometimes AF_UNIX does not auto-populate
                        // the bind address on accept. Thus, copy.
                        std::copy(std::begin(bind_address.m_data_un.sun_path),
                            std::end(bind_address.m_data_un.sun_path), std::begin(caddr.m_data_un.sun_path));
                    };
#endif

                    LOCKGUARD(connections_mutex);
                    csocket.setNonBlocking();
                    Connection& conn = connections[csocket];
                    conn.socket = csocket;
                    conn.state = { Connection::Idle };
                    conn.client = caddr;
                    reactor.addSocket(csocket, Reactor::Readable | Reactor::Closed);
                    LOG_TRACE("Server: [%s] accepted", CLID(conn));
                }
            }

            /**
             * @brief Handle Reactor::State::Reasable event.
             * @param socket Client socket.
             */
            virtual void onSocketReadable(Socket socket) override
            {
                LOG_TRACE("Server: reading socket fd=0x%x", static_cast<int>(socket.m_sock));
                decltype(connections)::iterator it;
                {
                    LOCKGUARD(connections_mutex);
                    it = connections.find(socket);
                    if (it != connections.end())
                    {
                        // TCP or Unix domain connection.
                        Connection& conn_tcp = it->second;
                        ReadStreamBuffer(conn_tcp);
                        onRequest(conn_tcp);
                        HandleConnection(conn_tcp);
                        return;
                    }
                }
                // UDP datagram connection (not found in connections map)
                // For UDP, we need to store the connection temporarily to handle async response.
                // Create a unique pseudo-socket identifier using server socket + client address hash.
                Connection conn_udp;
                    conn_udp.socket = socket;  // Server socket
                    conn_udp.state = { Connection::Receiving };
                    
                    // Read datagram and capture client address
                    ReadDatagramBuffer(conn_udp);
                    
                    if (conn_udp.state.count(Connection::Receiving))
                    {
                        // Store connection in map for async handling
                        // Use a pseudo-socket based on client address hash
                        Socket pseudo_socket;
                        pseudo_socket.m_sock = static_cast<Socket::Type>(
                            std::hash<std::string>{}(conn_udp.client.toString()));
                        
                        LOCKGUARD(connections_mutex);
                        Connection& stored_conn = connections[pseudo_socket];
                        stored_conn = conn_udp;
                        stored_conn.socket = socket;  // Keep real server socket
                        
                        // Process request
                        onRequest(stored_conn);
                        HandleConnection(stored_conn);
                        
                        // For UDP, if response is ready, send immediately and cleanup
                        if (stored_conn.state.count(Connection::Responding))
                        {
                            WriteResponseBuffer(stored_conn);
                            connections.erase(pseudo_socket);
                        }
                    }
            }

            /**
             * @brief Event triggered when server may write data back to client.
             * @param socket Client socket.
             */
            virtual void onSocketWritable(Socket socket) override
            {
                LOG_TRACE("Server: writing socket fd=0x%llx", socket.m_sock);
                LOCKGUARD(connections_mutex);
                auto it = connections.find(socket);
                if (it == connections.end())
                {
                    LOG_ERROR("Server: socket not found in connections map!");
                    return;
                }
                Connection& conn = it->second;
                conn.state.insert(Connection::Responding);
                HandleConnection(conn);
            }

            /**
             * @brief Handle event when socket is closed.
             * @param socket
             */
            virtual void onSocketClosed(Socket socket) override
            {
                LOG_TRACE("Server: closing socket fd=0x%llx", socket.m_sock);
                LOCKGUARD(connections_mutex);
                auto it = connections.find(socket);
                if (it != connections.end())
                {
                    Connection& conn = it->second;
                    conn.state.insert(Connection::Closing);
                    HandleConnection(conn);
                    return;
                }
                LOG_ERROR("Server: socket not found in connections map!");
            }

            /**
             * @brief Read from TCP or Unix Domain connection into request_buffer.
             * This function invokes `HandleConnection` to process the buffer.
             *
             * @param conn_tcp Connection object.
             */
            virtual void ReadStreamBuffer(Connection& conn_tcp)
            {
                conn_tcp.request_buffer.clear();
                conn_tcp.request_buffer.resize(4096, 0);
                size_t size = conn_tcp.socket.readall(conn_tcp.request_buffer);
                if (size > 0)
                {
                    LOG_TRACE("Server: [%s] stream read %zu bytes", CLID(conn_tcp), size);
                    conn_tcp.request_buffer.resize(size);
                    // Handle connection: process request_buffer
                    conn_tcp.state.insert(Connection::Receiving);
                }
                else
                {
                    conn_tcp.request_buffer.resize(0);
                    LOG_ERROR("Server: [%s] failed to read client stream, errno=%d", CLID(conn_tcp), errno);
                    conn_tcp.state.insert(Connection::Closing);
                }
            }

            /**
             * @brief Read from UDP connection into request_buffer.
             * This function invokes `HandleConnection` to process the buffer.
             *
             * @param conn_udp
             */
            virtual void ReadDatagramBuffer(Connection& conn_udp)
            {
                // Maximum size is 0xffff - (sizeof(IP Header) + sizeof(UDP Header)).
                // Try to read the entire datagram.
                conn_udp.request_buffer.resize(0xffff);
                int size = conn_udp.socket.recvfrom((void*)(conn_udp.request_buffer.data()), 0xffff, 0,
                    conn_udp.client);
                if (size > 0)
                {
                    LOG_ERROR("Server: [%s] datagram read %d bytes", CLID(conn_udp), size);
                    conn_udp.request_buffer.resize(size);
                    // Handle connection: process request_buffer
                    conn_udp.state.insert(Connection::Receiving);
                }
                else
                {
                    conn_udp.request_buffer.resize(0);
                    LOG_ERROR("Server: [%s] failed to read client datagram", CLID(conn_udp));
                }
            }

            /**
             * @brief Handle a timeslice of sending data back to client. If sending is blocked,
             * but not all data has been sent, then this function returns with an indication
             * that it needs to be called for the same connection again.
             *
             * @param conn Client-server connection.
             *
             * @return true if not all data has been bytes_sent.
             */
            bool WriteResponseBuffer(Connection& conn)
            {
                if (conn.response_buffer.empty())
                {
                    LOG_TRACE("Server: [%s] response blocked, empty response buffer!", CLID(conn));
                    return false;
                }

                size_t total_bytes_sent = 0;
                uint32_t optval = 0;

                conn.socket.getsockopt(SOL_SOCKET, SO_TYPE, optval);

                // Handle UDP response
                if (optval == SOCK_DGRAM)
                {
                    total_bytes_sent =
                        conn.socket.sendto(conn.response_buffer.data(),
                            static_cast<int>(conn.response_buffer.size()), 0, conn.client);
                    LOG_TRACE("Server: [%s] datagram sent %zu bytes", CLID(conn), total_bytes_sent);
                    return false;
                }

                // Handle TCP and Unix Domain response
                reactor.addSocket(conn.socket, Reactor::Writable);
                total_bytes_sent = conn.socket.writeall(conn.response_buffer);
                if (conn.response_buffer.size() != total_bytes_sent)
                {
                    conn.response_buffer.erase(0, total_bytes_sent);
                    LOG_WARN("Server: [%s] response blocked, total sent %zu bytes", CLID(conn), total_bytes_sent);
                    // Need to send more
                    conn.state.insert(Connection::Responding);
                    return true;
                }

                // Done sending
                conn.state.erase(Connection::Responding);
                conn.state.insert(Connection::Idle);
                LOG_TRACE("Server: [%s] response complete, total sent %zu bytes", CLID(conn), total_bytes_sent);
                return false;
            }

            /**
             * @brief Handle event when connection is closed.
             * @param conn
             */
            void onConnectionClosed(Connection& conn)
            {
                LOG_TRACE("Server: [%s] connection closing...", CLID(conn));
                if ((!conn.state.count(Connection::Idle)) && (!conn.state.count(Connection::Closing)))
                {
                    conn.state = { Connection::Aborted };
                    onConnectionAborted(conn);
                }

                // reactor.addSocket(conn.socket, SocketTools::Reactor::Closed);

                reactor.removeSocket(conn.socket);
                LOCKGUARD(connections_mutex);
                auto it = connections.find(conn.socket);
                conn.socket.close();
                conn.state.clear();
                conn.state.insert(Connection::Closed);
                LOG_TRACE("Server: [%s] connection closed.", CLID(conn));
                if (it != connections.end())
                {
                    connections.erase(it);
                }
            }

            void onConnectionAborted(Connection& conn)
            {
                LOG_WARN("Server: [%s] connection closed unexpectedly", CLID(conn));
            }

            void CloseConnection(Connection& conn)
            {
                LOG_TRACE("Server: [%s] closing connection...", CLID(conn));
                conn.socket.shutdown(Socket::ShutdownSend);
                onConnectionClosed(conn);
            }

            /**
             * @brief Handle connection state update.
             *
             * Connection states:
             * - Idle             - start receiving.
             * - Receiving        - handle client request.
             * - Responding       - respond back to client.
             * - Closing          - closing connection.
             * - Closed           - connection closed.
             *
             * @param conn
             */
            void HandleConnection(Connection& conn)
            {

                if (conn.state.count(Connection::Responding))
                {
                    reactor.addSocket(conn.socket, Reactor::Writable | Reactor::Closed);
                    // Got data to send back
                    LOG_TRACE("Server: [%s] responding...", CLID(conn));
                    // If WriteResponseBuffer returns true, then more data to send.
                    if (WriteResponseBuffer(conn))
                    {
                        return;
                    }
                    // No more data to send. Stop responding.
                    conn.state.erase(Connection::Responding);
                    reactor.addSocket(conn.socket, Reactor::Readable | Reactor::Closed);
                }

                if (conn.state.count(Connection::Closing))
                {
                    onConnectionClosed(conn);
                    return;
                }

                // If we are done responding, we may need to keep the socket open
                if (conn.keepalive)
                {
                    LOG_TRACE("Server: [%s] idle (keep-alive)", CLID(conn));
                    reactor.addSocket(conn.socket, Reactor::Readable | Reactor::Closed);
                    conn.state.insert(Connection::Idle);
                }
            }
        };
    }
}
SOCKETSHPP_NS_END
