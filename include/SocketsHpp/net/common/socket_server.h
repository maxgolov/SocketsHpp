// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file socket_server.h
/// @brief Generic reactor-driven TCP / UDP / Unix domain server (SocketServer).

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

        using Reactor = net::utils::Reactor;            ///< Alias of net::utils::Reactor.
        using Socket = net::utils::Socket;              ///< Alias of net::utils::Socket.
        using SocketAddr = net::utils::SocketAddr;      ///< Alias of net::utils::SocketAddr.
        using SocketParams = net::utils::SocketParams;  ///< Alias of net::utils::SocketParams.
        using ScopedSocket = net::utils::ScopedSocket;  ///< Alias of net::utils::ScopedSocket.

        /// @brief Reactor-driven server for TCP, UDP and Unix domain sockets.
        ///
        /// Binds in the constructor, then Start() runs the Reactor thread. For stream
        /// sockets each accepted client gets a Connection in #connections; received
        /// bytes are passed to #onRequest, which fills Connection::response_buffer and
        /// inserts Connection::Responding to send it. For UDP every datagram is handled
        /// as a transient Connection and any response is sent with sendto().
        ///
        /// The default #onRequest only inserts Connection::Responding; since
        /// response_buffer is empty nothing is sent. Replace it with a real handler.
        ///
        /// @note All callbacks run on the single reactor thread while
        ///       #connections_mutex is held; handlers must not block.
        /// @note The server owns #server_socket and accepted client sockets and closes
        ///       them in Stop().
        struct SocketServer : public Reactor::SocketCallback
        {
            /// @brief Per-client connection state (one per accepted stream socket, or a
            ///        transient one per UDP datagram).
            struct Connection
            {
                /// @brief Connection states; several may be set at once in #state.
                enum State
                {
                    Idle,        ///< No transfer in progress; waiting for data.
                    Receiving,   ///< request_buffer holds newly received data.
                    Responding,  ///< response_buffer should be (or is being) sent.
                    Closing,     ///< Peer closed / error, or handler requested close.
                    Closed,      ///< Socket has been closed.
                    Aborted      ///< Closed while neither Idle nor Closing.
                };

                Socket socket;      ///< Client socket (for UDP: the server socket).
                SocketAddr client;  ///< Peer address.

                std::string request_buffer;   ///< Bytes read by the current event (replaced on each read).
                std::string response_buffer;  ///< Bytes to send; unsent remainder is kept across writable events.

                std::set<State> state;  ///< Current set of State flags.
                bool keepalive{ true };   ///< Re-arm for reading after a response (currently always effectively true).
            };

            SocketAddr bind_address;  ///< Requested bind address; updated with the actual one (e.g. ephemeral port) after bind.
            bool is_bound{ false };   ///< true if bind() succeeded in the constructor.
            SocketParams server_socket_params;  ///< Socket family / type / protocol of the server socket.
            Socket server_socket;               ///< Listening (TCP/Unix) or bound (UDP) socket; owned by the server.
            Reactor reactor;                    ///< Event loop dispatching to this object.

            /// @brief Called on the reactor thread with newly received data in
            ///        Connection::request_buffer. Fill response_buffer and insert
            ///        Connection::Responding to reply; insert Connection::Closing to close.
            std::function<void(Connection& conn)> onRequest;

            /// @brief Response hook; default is a no-op. Not invoked by SocketServer itself.
            std::function<void(Connection& conn)> onResponse;

            /// @brief Guards #connections; held while callbacks run.
            std::recursive_mutex connections_mutex;
            /// @brief Accepted stream connections keyed by client socket.
            std::map<Socket, Connection> connections;

            /// @brief Log helper: peer address of @p conn as a TEMPORARY C string
            ///        (valid only within the enclosing full expression).
#define CLID(conn) conn.client.toString().c_str()

            /// @brief Bound address (after bind, includes the actual port).
            const SocketAddr& address() const { return bind_address; };

            /// @brief Create the server socket and bind it; for stream sockets also listen.
            ///
            /// Bind failures are logged and leave is_bound == false (no exception).
            /// UDP sockets are made non-blocking. The reactor is not started: call Start().
            /// @param addr Address or Unix domain socket path to bind to (port 0 = ephemeral;
            ///        read the actual one from address()).
            /// @param params Socket family / type / protocol.
            /// @param numConnections listen() backlog for stream sockets (pending
            ///        connections, not a limit on active connections).
            /// @throws std::runtime_error if the socket cannot be created.
            SocketServer(SocketAddr addr, SocketParams params, int numConnections = 10)
                : bind_address(addr), server_socket_params(params), reactor(*this)
            {
                server_socket = Socket(server_socket_params);

                // Default lambda here implements an echo server
                onRequest = [](Connection& conn) {
                    conn.state.insert(SocketServer::Connection::Responding);
                };

                onResponse = [](Connection&) {
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

            /// @brief Non-copyable.
            SocketServer(SocketServer const&) = delete;
            /// @brief Non-copyable.
            SocketServer& operator=(SocketServer const&) = delete;

            /// @brief Calls Stop(), so destroying a started server neither terminates on a
            ///        joinable thread nor leaks sockets.
            virtual ~SocketServer() { Stop(); }

            /// @brief Start the reactor thread (non-blocking; returns immediately).
            void Start() { reactor.start(); }

            /// @brief Stop the event loop, close the server socket and all accepted client
            ///        connections. Safe to call more than once.
            /// @note Blocks until the reactor thread exits (up to one poll interval).
            void Stop()
            {
                // Joins the reactor thread, then closes the bound server socket
                // (the first socket registered with the reactor).
                reactor.stop();
                {
                    LOCKGUARD(connections_mutex);
                    for (auto& kv : connections)
                    {
                        Socket& csocket = kv.second.socket;
                        if (!csocket.invalid() && (csocket != server_socket))
                        {
                            csocket.close();
                        }
                    }
                    connections.clear();
                }
                if (is_bound)
                {
                    // Already closed by reactor.stop()
                    server_socket.m_sock = Socket::Invalid;
                    is_bound = false;
                }
                else if (!server_socket.invalid())
                {
                    // Bind failed: socket was never handed to the reactor.
                    server_socket.close();
                }
            }

            /// @brief Accept a client on the listening socket, make it non-blocking and
            ///        register it (Readable | Closed) in #connections.
            /// @param socket Listening socket.
            virtual void onSocketAcceptable(Socket socket) override
            {
                LOG_TRACE("Server: accepting socket fd=0x%llx", static_cast<unsigned long long>(socket.m_sock));

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

            /// @brief Handle a Readable event: read stream data (ReadStreamBuffer) or one
            ///        datagram (ReadDatagramBuffer), call #onRequest if data arrived, then
            ///        drive the connection state (HandleConnection / UDP reply).
            /// @param socket Client socket (stream) or server socket (UDP).
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
                        // Only hand actual data to the request handler; a wakeup
                        // that produced no data (EWOULDBLOCK) or EOF is not a request.
                        if (!conn_tcp.request_buffer.empty())
                        {
                            onRequest(conn_tcp);
                        }
                        HandleConnection(conn_tcp);
                        return;
                    }
                }

                if (server_socket_params.type == SOCK_STREAM)
                {
                    // Stale event for a stream socket that has already been closed.
                    return;
                }

                // UDP datagram: each datagram is handled as a short-lived connection
                // bound to the server socket. It is not stored in `connections`:
                // the response (if any) is sent synchronously with sendto(), and the
                // server socket must never be closed by per-datagram handling.
                Connection conn_udp;
                conn_udp.socket = socket;  // Server socket
                conn_udp.state = { Connection::Idle };

                // Read datagram and capture client address
                ReadDatagramBuffer(conn_udp);
                if (!conn_udp.state.count(Connection::Receiving))
                {
                    return;
                }

                LOCKGUARD(connections_mutex);
                onRequest(conn_udp);
                if (conn_udp.state.count(Connection::Responding))
                {
                    WriteResponseBuffer(conn_udp);
                }
            }

            /// @brief Handle a Writable event: continue sending the pending response.
            /// @param socket Client socket.
            virtual void onSocketWritable(Socket socket) override
            {
                LOG_TRACE("Server: writing socket fd=0x%llx", static_cast<unsigned long long>(socket.m_sock));
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

            /// @brief Handle a hang-up / error event: mark the connection Closing and close it.
            ///        Ignored for UDP servers.
            /// @param socket Client socket.
            virtual void onSocketClosed(Socket socket) override
            {
                LOG_TRACE("Server: closing socket fd=0x%llx", static_cast<unsigned long long>(socket.m_sock));
                if (server_socket_params.type != SOCK_STREAM)
                {
                    // UDP: the reactor reports the server socket itself on shutdown.
                    return;
                }
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

            /// @brief Read available data (up to 64 KiB per event) from a stream connection
            ///        into request_buffer without blocking.
            ///
            /// Inserts Connection::Receiving if data was read and Connection::Closing on
            /// EOF or a hard error. A would-block wakeup leaves the state unchanged.
            /// The caller (onSocketReadable) then invokes #onRequest and HandleConnection().
            /// @param conn_tcp Connection to read from.
            virtual void ReadStreamBuffer(Connection& conn_tcp)
            {
                // Read what is available without blocking. Distinguish:
                // - data           -> Receiving
                // - recv() == 0    -> EOF (peer closed): Closing
                // - hard error     -> Closing
                // - EWOULDBLOCK    -> spurious wakeup: nothing to do
                static constexpr size_t kChunkSize = 4096;
                static constexpr size_t kMaxReadPerEvent = 64 * 1024;
                conn_tcp.request_buffer.clear();
                char chunk[kChunkSize];
                bool eof = false;
                int hardError = 0;
                while (conn_tcp.request_buffer.size() < kMaxReadPerEvent)
                {
                    int n = conn_tcp.socket.recv(chunk, sizeof(chunk));
                    if (n > 0)
                    {
                        conn_tcp.request_buffer.append(chunk, static_cast<size_t>(n));
                        continue;
                    }
                    if (n == 0)
                    {
                        eof = true;
                        break;
                    }
                    int err = conn_tcp.socket.error();
#ifndef _WIN32
                    if (err == EINTR)
                    {
                        continue;
                    }
#endif
                    if (!Socket::isWouldBlock(err))
                    {
                        hardError = err;
                    }
                    break;
                }

                if (!conn_tcp.request_buffer.empty())
                {
                    LOG_TRACE("Server: [%s] stream read %zu bytes", CLID(conn_tcp), conn_tcp.request_buffer.size());
                    // Handle connection: process request_buffer
                    conn_tcp.state.insert(Connection::Receiving);
                }
                if (eof || (hardError != 0))
                {
                    LOG_TRACE("Server: [%s] client stream closed (eof=%d, error=%d)", CLID(conn_tcp),
                        static_cast<int>(eof), hardError);
                    // Any data read above is still processed (and responded to)
                    // before the connection is closed.
                    conn_tcp.state.insert(Connection::Closing);
                }
            }

            /// @brief Receive one datagram (up to 65535 bytes) into request_buffer and the
            ///        sender into Connection::client; inserts Connection::Receiving on success.
            /// @param conn_udp Transient connection whose socket is the server socket.
            virtual void ReadDatagramBuffer(Connection& conn_udp)
            {
                // Maximum size is 0xffff - (sizeof(IP Header) + sizeof(UDP Header)).
                // Try to read the entire datagram.
                conn_udp.request_buffer.resize(0xffff);
                int size = conn_udp.socket.recvfrom((void*)(conn_udp.request_buffer.data()), 0xffff, 0,
                    conn_udp.client);
                if (size > 0)
                {
                    LOG_TRACE("Server: [%s] datagram read %d bytes", CLID(conn_udp), size);
                    conn_udp.request_buffer.resize(size);
                    // Handle connection: process request_buffer
                    conn_udp.state.insert(Connection::Receiving);
                }
                else
                {
                    conn_udp.request_buffer.resize(0);
                    LOG_TRACE("Server: failed to read client datagram, error=%d", conn_udp.socket.error());
                }
            }

            /// @brief Send as much of response_buffer as possible without blocking.
            ///
            /// UDP: sends the whole buffer with one sendto(). Stream: sends what the socket
            /// accepts, keeps the unsent remainder in response_buffer and arms Writable.
            /// A hard send error clears the buffer and marks the connection Closing.
            /// @param conn Client-server connection.
            /// @return true if data remains to be sent (call again on the next Writable event).
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

                // Handle TCP and Unix Domain response.
                // Keep Closed armed so that a peer reset is still observed.
                reactor.addSocket(conn.socket, Reactor::Writable | Reactor::Closed);
                int sendError = 0;
                total_bytes_sent = conn.socket.writeall(conn.response_buffer, &sendError);
                if ((sendError != 0) && !Socket::isWouldBlock(sendError))
                {
                    // Hard error (EPIPE, ECONNRESET, ...): the peer is gone. Drop the
                    // pending response and close instead of re-arming Writable forever.
                    LOG_WARN("Server: [%s] send failed, error=%d; closing connection", CLID(conn), sendError);
                    conn.response_buffer.clear();
                    conn.state.erase(Connection::Responding);
                    conn.state.insert(Connection::Closing);
                    return false;
                }
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

            /// @brief Unregister and close the client socket and erase it from #connections.
            ///        Calls onConnectionAborted() first if the connection was neither Idle
            ///        nor Closing.
            /// @param conn Connection to close; if it lives in #connections the reference
            ///        is invalid after this call.
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

            /// @brief Hook for connections closed unexpectedly; logs a warning.
            void onConnectionAborted(Connection& conn)
            {
                LOG_WARN("Server: [%s] connection closed unexpectedly", CLID(conn));
            }

            /// @brief Shut down the send direction, then close via onConnectionClosed().
            /// @param conn Connection to close (invalid after the call if it was in #connections).
            void CloseConnection(Connection& conn)
            {
                LOG_TRACE("Server: [%s] closing connection...", CLID(conn));
                conn.socket.shutdown(Socket::ShutdownSend);
                onConnectionClosed(conn);
            }

            /// @brief Advance a stream connection after an event.
            ///
            /// - Responding: write the response (WriteResponseBuffer); return while data is pending.
            /// - Closing: close the connection (onConnectionClosed).
            /// - Otherwise (keepalive): re-arm Readable | Closed and mark Idle.
            /// @param conn Connection to process.
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
