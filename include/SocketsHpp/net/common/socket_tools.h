// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _MSC_VER
#  pragma warning(push)
// Disable: warning C4267: 'argument': conversion from `size_t` to `int`, possible loss of data.
//
// WinSock vs POSIX sockets use different definition of socket payload size, e.g.
// in definition of socket ::send 'len' argument:
// - WinSock: int len
// - Linux:   size_t len
// - BSD:     size_t len
//
// We keep C++ method signature identical and prefer `size_t`. It is expected that Windows
// client cann not physically attempt to send a buffer larger than a size of max int.
//
#  pragma warning(disable : 4267)
#endif

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <WS2tcpip.h>
#  include <WinSock2.h>
#  include <Windows.h>

#  ifdef min
// NOMINMAX may be a better choice. However, defining it globally
// may break other code that depends on its macro definitions
// in Windows SDK.
#    undef min
#    undef max
#  endif

// This code requires WinSock2 on Windows.
#  pragma comment(lib, "ws2_32.lib")
// Workaround for libcurl redefinition of afunix.h struct :
// https://github.com/curl/curl/blob/7645324072c2f052fa662aded6f26821141ecda1/lib/config-win32.h#L721
// Unfortunately libcurl defines a structure that should otherwise be normally defined by afunix.h .
// When that happens, we cannot build the sockets library with Unix domain support.
#  if !defined(USE_UNIX_SOCKETS)
#    ifdef __has_include
#      if __has_include(<afunix.h>)
// Win 10 SDK 17063+ is necessary for Unix domain sockets support.
#        include <afunix.h>
#        define HAVE_UNIX_DOMAIN
#      endif
#    endif
#  endif

#else
#  define HAVE_UNIX_DOMAIN
#  include <unistd.h>

#  ifdef __linux__
#    include <sys/epoll.h>
#  endif

#  if __APPLE__
#    include "TargetConditionals.h"
// Use kqueue on mac
#    include <sys/event.h>
#    include <sys/time.h>
#    include <sys/types.h>
#  endif

// Common POSIX headers for Linux and Mac OS X
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/un.h>

#endif

#if !defined(_MSC_VER) && !defined(__STDC_LIB_EXT1__)
#  ifndef strncpy_s
#    define strncpy_s(dest, destsz, src, count) \
      strncpy(dest, src, (destsz <= count) ? destsz : count)
#  endif
#endif

SOCKETSHPP_NS_BEGIN
namespace net
{
    namespace common {

        /// <summary>
        /// A simple thread, derived class overloads onThread() method.
        /// </summary>
        struct Thread
        {
            std::thread m_thread;

            std::atomic<bool> m_terminate{ false };

            /// <summary>
            /// Thread Constructor
            /// </summary>
            /// <returns>Thread</returns>
            Thread() {}

            /// <summary>
            /// Start Thread
            /// </summary>
            void startThread(bool wait = true)
            {
                if (m_thread.joinable())
                {
                    // Already started: assigning over a joinable std::thread
                    // would call std::terminate.
                    return;
                }
                m_terminate = false;
                m_thread = std::thread([&]() { this->onThread(); });
                if (wait)
                {
                    waitForStart();
                }
            }

            /// <summary>
            /// Wait for thread start
            /// </summary>
            void waitForStart()
            {
                while (!m_thread.joinable())
                {
                    std::this_thread::yield();
                }
            }

            /// <summary>
            /// Join Thread
            /// </summary>
            void joinThread()
            {
                m_terminate = true;
                if (m_thread.joinable())
                {
                    if (m_thread.get_id() == std::this_thread::get_id())
                    {
                        // Called from the worker itself (e.g. stop() from a callback):
                        // joining would deadlock, so let the thread finish on its own.
                        m_thread.detach();
                    }
                    else
                    {
                        m_thread.join();
                    }
                }
            }

            /// <summary>
            /// Indicates if this thread should terminate
            /// </summary>
            /// <returns></returns>
            bool shouldTerminate() const { return m_terminate; }

            /// <summary>
            /// Must be implemented by children
            /// </summary>
            virtual void onThread() = 0;

            /// <summary>
            /// Thread destructor. Derived classes should call joinThread() in their
            /// own destructor (before their members are destroyed); this is a last
            /// resort so that destroying a running thread never calls std::terminate.
            /// </summary>
            virtual ~Thread() noexcept { joinThread(); }
        };

    };  // namespace common

    namespace utils
    {

#ifdef _WIN32
        // WinSocks need extra (de)initialization, solved by a global object here,
        // whose constructor/destructor will be called before and after main().
        struct WsaInitializer
        {
            WsaInitializer()
            {
                WSADATA wsaData;
                WSAStartup(MAKEWORD(2, 2), &wsaData);
            }

            ~WsaInitializer() { WSACleanup(); }
        };

        static WsaInitializer g_wsaInitializer;

#endif

        /// <summary>
        /// Encapsulation of C struct sockaddr[_in|_un] with additional helper methods.
        /// Both Internet and Unix Domain sockets are suported.
        /// The struct may be cast directly to sockaddr:
        /// - operator sockaddr *()
        /// - size_t size() const - returns proper size depending on socket type.
        /// </summary>
        struct SocketAddr
        {
            // The union is only as big as necessary to hold its largest data member.
            // Modern OS (both Un*x and Windows) require at least sizeof(sockaddr_un).
            union
            {
                sockaddr m_data;
                sockaddr_in m_data_in;
                sockaddr_in6 m_data_in6;
#ifdef HAVE_UNIX_DOMAIN
                sockaddr_un m_data_un;
#endif
            };

            constexpr static u_long const Loopback = 0x7F000001;

            // Indicator that the sockaddr is sockaddr_un
            bool isUnixDomain;

            /// <summary>
            /// SocketAddr constructor
            /// </summary>
            /// <returns>SocketAddr</returns>
            SocketAddr()
            {
                isUnixDomain = false;
#ifdef HAVE_UNIX_DOMAIN
                memset(&m_data_un, 0, sizeof(m_data_un));
#else
                memset(&m_data_in6, 0, sizeof(m_data_in6));
#endif
            }

            SocketAddr(u_long addr, int port) : SocketAddr()
            {
                isUnixDomain = false;
                m_data_in.sin_family = AF_INET;
                m_data_in.sin_port = htons(static_cast<unsigned short>(port));
                m_data_in.sin_addr.s_addr = htonl(addr);
            }

            /// <summary>
            /// Construct from a host (IPv4 literal, IPv6 literal with or without
            /// square brackets, or hostname) and a numeric port. An optional
            /// "scheme://" prefix is ignored.
            /// </summary>
            SocketAddr(const char* addr, int port) : SocketAddr()
            {
                if (addr == nullptr)
                {
                    throw std::invalid_argument("SocketAddr: null address");
                }
                std::string host = stripScheme(addr, nullptr);
                size_t len = host.length();
                if ((len >= 2) && (host[0] == '[') && (host[len - 1] == ']'))
                {
                    setIPv6(host.substr(1, len - 2), port, addr);
                }
                else if (std::count(host.begin(), host.end(), ':') > 1)
                {
                    setIPv6(host, port, addr);
                }
                else
                {
                    setIPv4(host, port, addr);
                }
            }

            /// <summary>
            /// Parse "[scheme://]host[:port]" or a Unix domain socket path.
            ///
            /// Accepted forms (port defaults to 0 when omitted):
            /// - IPv4 or hostname: "127.0.0.1", "127.0.0.1:8080", "localhost:80"
            /// - IPv6: "[::1]:8080", "[::1]", or bare "::1" / "fe80::1" (no port)
            /// - Unix domain (unixDomain == true, or "unix://" scheme): a filesystem path
            ///
            /// Throws std::invalid_argument on malformed addresses or ports.
            /// </summary>
            SocketAddr(const char* addr, bool unixDomain = false) : SocketAddr()
            {
                if (addr == nullptr)
                {
                    throw std::invalid_argument("SocketAddr: null address");
                }
                std::string scheme;
                std::string ipAddress = stripScheme(addr, &scheme);
                isUnixDomain = unixDomain || (scheme == "unix");

                if (isUnixDomain)
                {
#ifdef HAVE_UNIX_DOMAIN
                    m_data_un.sun_family = AF_UNIX;
                    // Max length of Unix domain filename is up to 108 chars (incl. terminator)
                    size_t pathLen = ipAddress.length();
                    if (pathLen >= sizeof(m_data_un.sun_path))
                    {
                        throw std::invalid_argument("Unix socket path too long (max " +
                            std::to_string(sizeof(m_data_un.sun_path) - 1) + " chars): " + std::string(addr));
                    }
                    // sun_path is already zero-filled by the default constructor.
                    memcpy(m_data_un.sun_path, ipAddress.data(), pathLen);
                    return;
#else
                    throw std::invalid_argument("Unix domain sockets are not supported: " + std::string(addr));
#endif
                }

                int port = 0;
                if (!ipAddress.empty() && (ipAddress[0] == '['))
                {
                    // Bracketed IPv6: "[addr]" or "[addr]:port"
                    size_t close = ipAddress.find(']');
                    if (close == std::string::npos)
                    {
                        throw std::invalid_argument("Invalid IPv6 address: " + std::string(addr));
                    }
                    std::string rest = ipAddress.substr(close + 1);
                    if (!rest.empty())
                    {
                        if (rest[0] != ':')
                        {
                            throw std::invalid_argument("Invalid IPv6 address: " + std::string(addr));
                        }
                        port = parsePort(rest.substr(1), addr);
                    }
                    setIPv6(ipAddress.substr(1, close - 1), port, addr);
                    return;
                }

                size_t numColons = static_cast<size_t>(std::count(ipAddress.begin(), ipAddress.end(), ':'));
                if (numColons > 1)
                {
                    // Bare IPv6 literal without brackets cannot carry a port.
                    setIPv6(ipAddress, 0, addr);
                    return;
                }

                if (numColons == 1)
                {
                    size_t colon = ipAddress.find(':');
                    port = parsePort(ipAddress.substr(colon + 1), addr);
                    ipAddress.erase(colon);
                }
                setIPv4(ipAddress, port, addr);
            }

            /// <summary>
            /// Size of the address storage (the largest supported sockaddr type).
            /// Use this as the in/out length for recvfrom/accept/getsockname.
            /// </summary>
            static constexpr size_t capacity()
            {
#ifdef HAVE_UNIX_DOMAIN
                return (sizeof(sockaddr_un) > sizeof(sockaddr_in6)) ? sizeof(sockaddr_un) : sizeof(sockaddr_in6);
#else
                return sizeof(sockaddr_in6);
#endif
            }

        private:
            /// Remove an optional "scheme://" prefix; optionally return the scheme.
            static std::string stripScheme(const char* addr, std::string* scheme)
            {
                std::string result = addr;
                auto found = result.find("://");
                if (found != std::string::npos)
                {
                    if (scheme != nullptr)
                    {
                        *scheme = result.substr(0, found);
                    }
                    result.erase(0, found + 3);
                }
                return result;
            }

            /// Parse a decimal port number in range [0..65535].
            static int parsePort(const std::string& text, const char* addr)
            {
                if (text.empty() || (text.length() > 5) ||
                    !std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0') && (c <= '9'); }))
                {
                    throw std::invalid_argument("Invalid port in address: " + std::string(addr));
                }
                int port = std::stoi(text);
                if (port > 65535)
                {
                    throw std::invalid_argument("Port out of range in address: " + std::string(addr));
                }
                return port;
            }

            void setIPv6(const std::string& host, int port, const char* addr)
            {
                m_data_in6.sin6_family = AF_INET6;
                m_data_in6.sin6_port = htons(static_cast<unsigned short>(port));
                if (::inet_pton(AF_INET6, host.c_str(), &m_data_in6.sin6_addr) != 1)
                {
                    LOG_ERROR("Invalid IPv6 address: %s", addr);
                    throw std::invalid_argument("Invalid IPv6 address: " + std::string(addr));
                }
            }

            void setIPv4(const std::string& host, int port, const char* addr)
            {
                m_data_in.sin_family = AF_INET;
                m_data_in.sin_port = htons(static_cast<unsigned short>(port));
                // Try inet_pton first for IP addresses
                if (::inet_pton(AF_INET, host.c_str(), &m_data_in.sin_addr) == 1)
                {
                    return;
                }
                // inet_pton failed, try getaddrinfo for hostname resolution
                struct addrinfo hints = {};
                struct addrinfo* result = nullptr;
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                if (!host.empty() && (::getaddrinfo(host.c_str(), nullptr, &hints, &result) == 0) && (result != nullptr))
                {
                    sockaddr_in* resolved = reinterpret_cast<sockaddr_in*>(result->ai_addr);
                    m_data_in.sin_addr = resolved->sin_addr;
                    ::freeaddrinfo(result);
                    return;
                }
                LOG_ERROR("Invalid IPv4 address or hostname: %s", addr);
                throw std::invalid_argument("Invalid IPv4 address or hostname: " + std::string(addr));
            }

        public:

            SocketAddr(SocketAddr const& other) = default;

            SocketAddr& operator=(SocketAddr const& other) = default;

            operator sockaddr* () { return &m_data; }

            operator const sockaddr* () const { return &m_data; }

            size_t size() const
            {
#ifdef HAVE_UNIX_DOMAIN
                // Unix domain struct m_data_un
                if (isUnixDomain)
                    return sizeof(m_data_un);
#endif
                // IPv4 struct m_data_in
                if (m_data.sa_family == AF_INET)
                    return sizeof(m_data_in);
                // IPv6 struct m_data_in6
                if (m_data.sa_family == AF_INET6)
                    return sizeof(m_data_in6);
                // RAW socket?
                return sizeof(m_data);
            }

            int port() const
            {
#ifdef HAVE_UNIX_DOMAIN
                if (isUnixDomain)
                {
                    return -1;
                }
#endif

                switch (m_data.sa_family)
                {
                case AF_INET6: {
                    return ntohs(m_data_in6.sin6_port);
                }
                case AF_INET: {
                    return ntohs(m_data_in.sin_port);
                }
                default:
                    return -1;
                }
            }

            std::string toString() const
            {
                std::ostringstream os;

#ifdef HAVE_UNIX_DOMAIN
                if (isUnixDomain)
                {
                    os << (const char*)(m_data_un.sun_path);
                }
                else
#endif
                {
                    switch (m_data.sa_family)
                    {
                    case AF_INET6: {
                        char buff[NI_MAXHOST] = { 0 };
                        inet_ntop(AF_INET6, &(m_data_in6.sin6_addr), buff, sizeof(buff));
                        os << '[' << buff << ']';
                        os << ':' << ntohs(m_data_in6.sin6_port);
                        break;
                    }
                    case AF_INET: {
                        u_long addr = ntohl(m_data_in.sin_addr.s_addr);
                        os << (addr >> 24) << '.' << ((addr >> 16) & 255) << '.' << ((addr >> 8) & 255) << '.'
                            << (addr & 255);
                        os << ':' << ntohs(m_data_in.sin_port);
                        break;
                    }
                    default:
                        os << "[?AF?" << m_data.sa_family << ']';
                    }
                };
                return os.str();
            }
        };  // struct SocketAddr

        static const char* kSchemeUDP = "udp";
        static const char* kSchemeTCP = "tcp";
        static const char* kSchemeUnix = "unix";
        static const char* kSchemeUnk = "unknown";

        struct SocketParams
        {
            int af;     // POSIX socket domain
            int type;   // POSIX socket type
            int proto;  // POSIX socket protocol

            /**
             * @brief Determine connection scheme based on socket parameters:
             * "tcp", "udp", "unix" or "unknown".
             *
             * @return Text representation of scheme.
             */
            inline const char* scheme()
            {
                if ((af == AF_INET) || (af == AF_INET6))
                {
                    if (type == SOCK_DGRAM)
                        return kSchemeUDP;
                    if (type == SOCK_STREAM)
                        return kSchemeTCP;
                }
                if (af == AF_UNIX)
                {
                    return kSchemeUnix;
                }
                return kSchemeUnk;
            }
        };

        /// <summary>
        /// Encapsulation of a socket (non-exclusive ownership).
        /// IMPORTANT: This class does NOT automatically close the socket in its destructor
        /// for backward compatibility and to support non-owning references.
        /// Users MUST call close() explicitly, or use ScopedSocket for RAII behavior.
        /// </summary>
        struct Socket
        {
#ifdef _WIN32
            typedef SOCKET Type;
            static constexpr Type const Invalid = INVALID_SOCKET;
#else
            typedef int Type; /* POSIX m_sock type is int */
            static constexpr Type const Invalid = -1;
#endif

            Type m_sock;

            /// @brief Construct socket from parameters
            Socket(SocketParams params) : Socket(params.af, params.type, params.proto) {}

            /// @brief Construct socket from existing handle (non-owning)
            Socket(Type sock = Invalid) noexcept : m_sock(sock) {}

            /// @brief Create new socket with specified parameters
            /// @throws std::runtime_error if socket creation fails
            Socket(int af, int type, int proto)
            {
                m_sock = ::socket(af, type, proto);
                if (m_sock == Invalid)
                {
#ifdef _WIN32
                    int err = ::WSAGetLastError();
                    throw std::runtime_error("Failed to create socket, error: " + std::to_string(err));
#else
                    throw std::runtime_error("Failed to create socket: " + std::string(strerror(errno)));
#endif
                }
                suppressSigPipe();
                setCloseOnExec();
            }

            /// @brief Do not leak the socket into child processes (POSIX FD_CLOEXEC).
            /// No-op on Windows, where sockets are not inherited by default.
            void setCloseOnExec() noexcept
            {
#if !defined(_WIN32) && defined(FD_CLOEXEC)
                int fdflags = ::fcntl(m_sock, F_GETFD, 0);
                if (fdflags >= 0)
                {
                    ::fcntl(m_sock, F_SETFD, fdflags | FD_CLOEXEC);
                }
#endif
            }

            /// @brief Suppress SIGPIPE for writes to a peer-closed socket.
            /// On Apple platforms send() has no MSG_NOSIGNAL flag, so a write to
            /// a closed connection raises SIGPIPE and terminates the process.
            /// SO_NOSIGPIPE makes such writes fail with EPIPE instead. This is a
            /// no-op where the option is unavailable (Linux uses MSG_NOSIGNAL,
            /// Windows has no SIGPIPE).
            void suppressSigPipe() noexcept
            {
#if defined(SO_NOSIGPIPE)
                int on = 1;
                ::setsockopt(m_sock, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<char*>(&on), sizeof(on));
#endif
            }

            // Copy operations allowed (non-owning semantics)
            Socket(Socket const& other) = default;
            Socket& operator=(Socket const& other) = default;

            // Move operations (transfers handle without closing)
            Socket(Socket&& other) noexcept : m_sock(other.m_sock)
            {
                other.m_sock = Invalid;
            }

            Socket& operator=(Socket&& other) noexcept
            {
                if (this != &other)
                {
                    m_sock = other.m_sock;
                    other.m_sock = Invalid;
                }
                return *this;
            }

            /// @brief Destructor does NOT close socket (call close() explicitly)
            ~Socket() = default;

            operator Socket::Type() const noexcept { return m_sock; }

            bool operator==(Socket const& other) const noexcept { return (m_sock == other.m_sock); }

            bool operator!=(Socket const& other) const noexcept { return (m_sock != other.m_sock); }

            bool operator<(Socket const& other) const noexcept { return (m_sock < other.m_sock); }

            /// @brief Check if socket handle is invalid
            bool invalid() const noexcept { return (m_sock == Invalid); }

            void setNonBlocking()
            {
                assert(m_sock != Invalid);
#ifdef _WIN32
                u_long value = 1;
                ::ioctlsocket(m_sock, FIONBIO, &value);
#else
                int flags = ::fcntl(m_sock, F_GETFL, 0);
                ::fcntl(m_sock, F_SETFL, flags | O_NONBLOCK);
#endif
            }

            bool setReuseAddr()
            {
                assert(m_sock != Invalid);
#ifdef _WIN32
                BOOL value = TRUE;
#else
                int value = 1;
#endif
                return (::setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&value),
                    sizeof(value)) == 0);
            }

            bool setNoDelay()
            {
                assert(m_sock != Invalid);
#ifdef _WIN32
                BOOL value = TRUE;
#else
                int value = 1;
#endif
                return (::setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&value),
                    sizeof(value)) == 0);
            }

            bool connect(SocketAddr const& addr)
            {
                assert(m_sock != Invalid);
                return (::connect(m_sock, (const sockaddr*)addr, addr.size()) == 0);
            }

            void close()
            {
#ifdef _WIN32
                ::closesocket(m_sock);
#else
                ::close(m_sock);
#endif
                m_sock = Invalid;
            }

            int recvfrom(_Out_cap_(size) void* buffer, size_t size, int flags, SocketAddr& clientAddr)
            {
                assert(m_sock != Invalid);
                // Pass the full storage size: the peer may be of a different
                // (larger) family than whatever clientAddr currently holds.
#ifdef _WIN32
                int len = static_cast<int>(SocketAddr::capacity());
#else
                socklen_t len = static_cast<socklen_t>(SocketAddr::capacity());
#endif
                return static_cast<int>(
                    ::recvfrom(m_sock, reinterpret_cast<char*>(buffer), size, flags, clientAddr, &len));
            }

            int recv(_Out_cap_(size) void* buffer, size_t size, int flags = 0)
            {
                assert(m_sock != Invalid);
                return static_cast<int>(::recv(m_sock, reinterpret_cast<char*>(buffer), size, flags));
            }

            template <typename T>
            size_t readall(T& buffer)
            {
                size_t total_bytes_received = 0;
                int bytes_received = 0;
                // Read response fully
                do
                {
                    bytes_received = recv((void*)(buffer.data() + total_bytes_received),
                        buffer.size() - total_bytes_received);
                    if (bytes_received > 0)
                    {
                        total_bytes_received += bytes_received;
                    }
                    else if (bytes_received == 0)
                    {
#ifndef _WIN32
                        // recv() on a blocking socket may return EAGAIN in case
                        // if the call timed out (no data received in time period
                        // specified as socket timeout)
                        if (errno == EAGAIN)
                            continue;
#else
                        // recv() returns 0 only when 0-byte buffer is requested
                        // or when the other peer has gracefully disconnected.
                        // No data received.
                        break;
#endif
                    }
                    else
                    {
                        // recv() error occurred.
                        break;
                    }
                } while ((bytes_received > 0) && (total_bytes_received < buffer.size()));
                buffer.resize(total_bytes_received);
                return total_bytes_received;
            }

            /// @brief Send as much of buffer as possible.
            /// @param buffer Data to send.
            /// @param lastError Optional out-parameter: 0 if everything was sent or
            ///        sending stopped without an error; otherwise the socket error
            ///        code of the failed send() (see isWouldBlock()).
            /// @return Number of bytes sent.
            template <typename T>
            size_t writeall(T& buffer, int* lastError = nullptr)
            {
                size_t total_bytes_sent = 0;
                int bytes_sent = 0;
                if (lastError != nullptr)
                {
                    *lastError = 0;
                }
                // Write response fully
                do
                {
                    bytes_sent =
                        send((void*)(buffer.data() + total_bytes_sent), buffer.size() - total_bytes_sent);
                    if (bytes_sent > 0)
                    {
                        total_bytes_sent += bytes_sent;
                    }
                    else if (bytes_sent == 0)
                    {
                        // No more data to send or can't send anymore.
                        break;
                    }
                    else
                    {
                        // send() error occurred: would-block or a hard error.
                        int err = error();
#ifndef _WIN32
                        if (err == EINTR)
                        {
                            continue;
                        }
#endif
                        if (lastError != nullptr)
                        {
                            *lastError = err;
                        }
                        break;
                    }
                } while (total_bytes_sent < buffer.size());
                return total_bytes_sent;
            }

            /// @brief True if err is a transient "try again later" socket error.
            static bool isWouldBlock(int err) noexcept
            {
#ifdef _WIN32
                return (err == WSAEWOULDBLOCK) || (err == WSAEINTR) || (err == WSAEINPROGRESS);
#else
                return (err == EAGAIN) || (err == EWOULDBLOCK) || (err == EINTR);
#endif
            }

            int send(void const* buffer, size_t size)
            {
                assert(m_sock != Invalid);
                if ((m_sock == Invalid) || (buffer == nullptr) || (size == 0))
                    return 0;
                // Avoid SIGPIPE on a write to a peer-closed socket. Linux exposes
                // MSG_NOSIGNAL; Apple relies on SO_NOSIGPIPE set at creation/accept.
#if defined(MSG_NOSIGNAL)
                constexpr int sendFlags = MSG_NOSIGNAL;
#else
                constexpr int sendFlags = 0;
#endif
                return static_cast<int>(
                    ::send(m_sock, reinterpret_cast<char const*>(buffer), size, sendFlags));
            }

            int sendto(void const* buffer, size_t size, int flags, SocketAddr& destAddr)
            {
                assert(m_sock != Invalid);
                if ((m_sock == Invalid) || (buffer == nullptr) || (size == 0))
                    return 0;
                int len = destAddr.size();
                return static_cast<int>(
                    ::sendto(m_sock, reinterpret_cast<char const*>(buffer), size, flags, destAddr, len));
            }

            int bind(SocketAddr const& addr)
            {
                assert(m_sock != Invalid);
                return ::bind(m_sock, addr, addr.size());
            }

            bool getsockname(SocketAddr& addr) const
            {
                assert(m_sock != Invalid);
#ifdef _WIN32
                int addrlen = static_cast<int>(SocketAddr::capacity());
#else
                socklen_t addrlen = static_cast<socklen_t>(SocketAddr::capacity());
#endif
                return (::getsockname(m_sock, addr, &addrlen) == 0);
            }

            template <typename T>
            int getsockopt(int level, int optname, T& optval)
            {
#ifdef _WIN32
                int optlen = sizeof(T);
                return ::getsockopt(m_sock, level, optname, (char*)(&optval), &optlen);
#else
                socklen_t optlen = sizeof(T);
                return ::getsockopt(m_sock, level, optname, (void*)&optval, &optlen);
#endif
            }

            bool listen(size_t backlog)
            {
                assert(m_sock != Invalid);
                return (::listen(m_sock, backlog) == 0);
            }

            bool accept(Socket& csock, SocketAddr& caddr)
            {
                assert(m_sock != Invalid);
#ifdef _WIN32
                int addrlen = static_cast<int>(SocketAddr::capacity());
#else
                socklen_t addrlen = static_cast<socklen_t>(SocketAddr::capacity());
#endif
                csock = ::accept(m_sock, caddr, &addrlen);
                if (!csock.invalid())
                {
                    csock.suppressSigPipe();
                    csock.setCloseOnExec();
                    return true;
                }
                return false;
            }

            bool shutdown(int how)
            {
                assert(m_sock != Invalid);
                return (::shutdown(m_sock, how) == 0);
            }

            int error() const
            {
#ifdef _WIN32
                return ::WSAGetLastError();
#else
                return errno;
#endif
            }

            enum
            {
#ifdef _WIN32
                ErrorWouldBlock = WSAEWOULDBLOCK
#else
                ErrorWouldBlock = EWOULDBLOCK
#endif
            };

            enum
            {
#ifdef _WIN32
                ShutdownReceive = SD_RECEIVE,
                ShutdownSend = SD_SEND,
                ShutdownBoth = SD_BOTH
#else
                ShutdownReceive = SHUT_RD,
                ShutdownSend = SHUT_WR,
                ShutdownBoth = SHUT_RDWR
#endif
            };
        };

        /// <summary>
        /// RAII wrapper for Socket providing automatic cleanup.
        /// Use this when you want automatic socket closure on scope exit.
        /// </summary>
        class ScopedSocket
        {
        private:
            Socket m_socket;
            bool m_owned;

        public:
            /// @brief Construct from Socket, taking ownership
            explicit ScopedSocket(Socket&& sock) noexcept
                : m_socket(std::move(sock)), m_owned(true) {}

            /// @brief Create new socket with parameters
            ScopedSocket(int af, int type, int proto)
                : m_socket(af, type, proto), m_owned(true) {}

            /// @brief Non-copyable
            ScopedSocket(ScopedSocket const&) = delete;
            ScopedSocket& operator=(ScopedSocket const&) = delete;

            /// @brief Movable
            ScopedSocket(ScopedSocket&& other) noexcept
                : m_socket(std::move(other.m_socket)), m_owned(other.m_owned)
            {
                other.m_owned = false;
            }

            ScopedSocket& operator=(ScopedSocket&& other) noexcept
            {
                if (this != &other)
                {
                    if (m_owned && !m_socket.invalid())
                    {
                        m_socket.close();
                    }
                    m_socket = std::move(other.m_socket);
                    m_owned = other.m_owned;
                    other.m_owned = false;
                }
                return *this;
            }

            /// @brief Auto-close socket on destruction
            ~ScopedSocket()
            {
                if (m_owned && !m_socket.invalid())
                {
                    m_socket.close();
                }
            }

            /// @brief Get underlying socket (non-owning reference)
            Socket& get() noexcept { return m_socket; }
            Socket const& get() const noexcept { return m_socket; }

            /// @brief Release ownership (caller must close)
            Socket release() noexcept
            {
                m_owned = false;
                return m_socket;
            }

            /// @brief Forwarding methods to underlying socket
            bool invalid() const noexcept { return m_socket.invalid(); }
            void setNonBlocking() { m_socket.setNonBlocking(); }
            bool setReuseAddr() { return m_socket.setReuseAddr(); }
            bool setNoDelay() { return m_socket.setNoDelay(); }
            bool connect(SocketAddr const& addr) { return m_socket.connect(addr); }
            int send(void const* buffer, size_t size) { return m_socket.send(buffer, size); }
            int recv(void* buffer, size_t size, int flags = 0) { return m_socket.recv(buffer, size, flags); }
            int bind(SocketAddr const& addr) { return m_socket.bind(addr); }
            bool listen(size_t backlog) { return m_socket.listen(backlog); }
            bool accept(Socket& csock, SocketAddr& caddr) { return m_socket.accept(csock, caddr); }
            void close() { if (m_owned) m_socket.close(); m_owned = false; }
        };

        /// <summary>
        /// Socket Data
        /// </summary>
        struct SocketData
        {
            Socket socket;
            int flags;

            SocketData() noexcept : socket(), flags(0) {}

            bool operator==(Socket s) const noexcept { return (socket == s); }
        };

        /// <summary>
        /// Socket Reactor
        /// </summary>
        struct Reactor : protected common::Thread
        {
            /// <summary>
            /// Socket State callback
            /// </summary>
            class SocketCallback
            {
            public:
                virtual void onSocketReadable(Socket sock) = 0;
                virtual void onSocketWritable(Socket sock) = 0;
                virtual void onSocketAcceptable(Socket sock) = 0;
                virtual void onSocketClosed(Socket sock) = 0;
            };

            /// <summary>
            /// Socket State
            /// </summary>
            enum State
            {
                Readable = 1,
                Writable = 2,
                Acceptable = 4,
                Closed = 8
            };

            SocketCallback& m_callback;

            std::recursive_mutex m_sockets_mutex;
            std::vector<SocketData> m_sockets;

            // Event loop is required for stream sockets
            bool m_streaming{ true };

#ifdef _WIN32
            /* use WinSock events on Windows */
            std::vector<WSAEVENT> m_events{};
#endif

#ifdef __linux__
            /* use epoll on Linux */
            int m_epollFd{ -1 };
#endif

#ifdef TARGET_OS_MAC
            /* use kqueue on Mac */
            int kq{ -1 };
            static constexpr int KQUEUE_SIZE = config::KQUEUE_DEFAULT_SIZE;
            struct kevent m_events[KQUEUE_SIZE];
#endif

        public:
            Reactor(SocketCallback& callback) : m_callback(callback)
            {
#ifdef __linux__
#  if defined(ANDROID) || defined(__ANDROID__)
                // epoll_create() requires size > 0 (it is otherwise ignored).
                m_epollFd = ::epoll_create(1);
                if (m_epollFd >= 0)
                {
                    ::fcntl(m_epollFd, F_SETFD, FD_CLOEXEC);
                }
#  else
                m_epollFd = ::epoll_create1(EPOLL_CLOEXEC);
#  endif
                if (m_epollFd < 0)
                {
                    LOG_ERROR("Reactor: epoll_create failed! errno=%d", errno);
                }
#endif

#ifdef TARGET_OS_MAC
                bzero(&m_events[0], sizeof(m_events));
                kq = kqueue();
                if (kq >= 0)
                {
                    ::fcntl(kq, F_SETFD, FD_CLOEXEC);
                }
#endif
            }

            Reactor(Reactor const&) = delete;
            Reactor& operator=(Reactor const&) = delete;

            /// <summary>
            /// Destructor. Stops the worker thread if the owner did not call stop().
            /// Sockets are not closed here: they belong to the owner (see stop()).
            /// </summary>
            ~Reactor()
            {
                joinThread();
#ifdef _WIN32
                for (auto& hEvent : m_events)
                {
                    ::WSACloseEvent(hEvent);
                }
                m_events.clear();
#endif
#ifdef __linux__
                if (m_epollFd >= 0)
                {
                    ::close(m_epollFd);
                }
#endif
#ifdef TARGET_OS_MAC
                if (kq >= 0)
                {
                    ::close(kq);
                }
#endif
            }

            /// <summary>
            /// Add Socket
            /// </summary>
            /// <param name="socket"></param>
            /// <param name="flags"></param>
            void addSocket(const Socket& socket, int flags)
            {
                if (flags == 0)
                {
                    removeSocket(socket);
                    return;
                }

                LOCKGUARD(m_sockets_mutex);
                if ((flags == State::Readable) && (m_sockets.size() == 0))
                {
                    // No listen/accept - readable UDP datagram
                    m_streaming = false;
                    LOG_TRACE("Reactor: Adding datagram socket 0x%x with flags 0x%x", static_cast<int>(socket),
                        flags);
                    m_sockets.push_back(SocketData());
                    m_sockets.back().socket = socket;
                    m_sockets.back().flags = 0;
                    return;
                }

                if (m_streaming)
                {
                    auto it = std::find(m_sockets.begin(), m_sockets.end(), socket);
                    if (it == m_sockets.end())
                    {
                        LOG_TRACE("Reactor: Adding socket 0x%x with flags 0x%x", static_cast<int>(socket), flags);
#ifdef _WIN32
                        m_events.push_back(::WSACreateEvent());
#endif
#ifdef __linux__
                        epoll_event event = {};
                        event.data.fd = socket;
                        event.events = 0;
                        if (::epoll_ctl(m_epollFd, EPOLL_CTL_ADD, socket, &event) != 0)
                        {
                            LOG_ERROR("Reactor: epoll_ctl failed! errno=%d", errno);
                        }
#endif
                        m_sockets.push_back(SocketData());
                        m_sockets.back().socket = socket;
                        m_sockets.back().flags = 0;
                        it = m_sockets.end() - 1;
                    }
                    else
                    {
                        LOG_TRACE("Reactor: Updating socket 0x%x with flags 0x%x", static_cast<int>(socket), flags);
                    }

                    if (it->flags != flags)
                    {
                        it->flags = flags;
#ifdef _WIN32
                        long lNetworkEvents = 0;
                        if (it->flags & Readable)
                        {
                            lNetworkEvents |= FD_READ;
                        }
                        if (it->flags & Writable)
                        {
                            lNetworkEvents |= FD_WRITE;
                        }
                        if (it->flags & Acceptable)
                        {
                            lNetworkEvents |= FD_ACCEPT;
                        }
                        if (it->flags & Closed)
                        {
                            lNetworkEvents |= FD_CLOSE;
                        }
                        auto eventIt = m_events.begin() + std::distance(m_sockets.begin(), it);
                        ::WSAEventSelect(socket, *eventIt, lNetworkEvents);
#endif
#ifdef __linux__
                        int events = 0;
                        if (it->flags & Readable)
                        {
                            events |= EPOLLIN;
                        };
                        if (it->flags & Writable)
                        {
                            events |= EPOLLOUT;
                        };
                        if (it->flags & Acceptable)
                        {
                            events |= EPOLLIN;
                        };
                        // if (it->flags & Closed) - always handled (EPOLLERR | EPOLLHUP)
                        epoll_event event = {};
                        event.data.fd = socket;
                        event.events = events;
                        if (::epoll_ctl(m_epollFd, EPOLL_CTL_MOD, socket, &event) != 0)
                        {
                            LOG_ERROR("Reactor: epoll_ctl failed! errno=%d", errno);
                        }
#endif
#ifdef TARGET_OS_MAC
                        kqueueArm(socket, flags);
#endif
                    }
                }
            }

            /// <summary>
            /// Remove Socket
            /// </summary>
            /// <param name="socket"></param>
            void removeSocket(const Socket& socket)
            {
                LOCKGUARD(m_sockets_mutex);
                LOG_TRACE("Reactor: Removing socket 0x%x", static_cast<int>(socket));
                auto it = std::find(m_sockets.begin(), m_sockets.end(), socket);
                if (it != m_sockets.end())
                {
                    if (m_streaming)
                    {
#ifdef _WIN32
                        auto eventIt = m_events.begin() + std::distance(m_sockets.begin(), it);
                        ::WSAEventSelect(it->socket, *eventIt, 0);
                        ::WSACloseEvent(*eventIt);
                        m_events.erase(eventIt);
#endif
#ifdef __linux__
                        if (::epoll_ctl(m_epollFd, EPOLL_CTL_DEL, socket, nullptr) != 0)
                        {
                            LOG_ERROR("Reactor: epoll_ctl failed! errno=%d", errno);
                        };
#endif
#ifdef TARGET_OS_MAC
                        kqueueDisarm(socket);
#endif
                    }
                    m_sockets.erase(it);
                }
            }

            /// <summary>
            /// Start server
            /// </summary>
            void start()
            {
                LOG_INFO("Reactor: Starting...");
                startThread();
            }

            /// <summary>
            /// Stop the event loop, unregister all sockets and close the first
            /// registered socket (the listening / bound server socket). Other
            /// sockets are unregistered but not closed. Safe to call more than once.
            /// </summary>
            void stop()
            {
                LOG_INFO("Reactor: Stopping...");
                // Signal and join the worker first. The UDP receive loop polls with a
                // timeout and observes the terminate flag, so the socket is never
                // closed underneath a thread that may still be reading from it.
                joinThread();

                // Only acquire the lock after the worker(s) have joined
                LOCKGUARD(m_sockets_mutex);
                if (m_streaming)
                {
#ifdef _WIN32
                    for (size_t i = 0; (i < m_sockets.size()) && (i < m_events.size()); i++)
                    {
                        ::WSAEventSelect(m_sockets[i].socket, m_events[i], 0);
                    }
                    for (auto& hEvent : m_events)
                    {
                        ::WSACloseEvent(hEvent);
                    }
                    m_events.clear();
#else /* Linux and Mac */
                    for (auto& sd : m_sockets)
                    {
#  ifdef __linux__
                        if (::epoll_ctl(m_epollFd, EPOLL_CTL_DEL, sd.socket, nullptr) != 0)
                        {
                            LOG_ERROR("Reactor: epoll_ctl failed! errno=%d", errno);
                        };
#  endif
#  ifdef TARGET_OS_MAC
                        kqueueDisarm(sd.socket);
#  endif
                    }
#endif
                }
                // unbind
                if (m_sockets.size() && !m_sockets[0].socket.invalid())
                {
                    m_sockets[0].socket.close();
                }
                m_sockets.clear();
            }

            /// <summary>
            /// Thread Loop for async events processing
            /// </summary>
            virtual void onThread() override
            {
                LOG_INFO("Reactor: Thread started");

                if (!m_streaming)
                {
                    // UDP Server implementation.
                    // Process only one bound address at the moment, not many.
                    // This single-threaded implementation passes UDP buffers
                    // to onSocketReadable, that should decide what to do with
                    // the socket. Callback may implement its own thread pool.
                    Socket socket;
                    {
                        LOCKGUARD(m_sockets_mutex);
                        if (m_sockets.empty())
                        {
                            return;
                        }
                        socket = m_sockets[0].socket;
                    }
                    LOG_TRACE("Reactor: socket 0x%x receive loop started...", static_cast<int>(socket));
                    while (!shouldTerminate())
                    {
                        // Wait (bounded) for a datagram instead of spinning on a
                        // non-blocking recvfrom() that returns EWOULDBLOCK.
                        int ready = waitReadable(socket, config::REACTOR_POLL_TIMEOUT_MS);
                        if (ready == 0)
                        {
                            continue;
                        }
                        if (ready < 0)
                        {
                            // Unexpected wait error: back off rather than spin.
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            continue;
                        }
                        m_callback.onSocketReadable(socket);
                    }
                    m_callback.onSocketClosed(socket);
                    LOG_TRACE("Reactor: socket 0x%x closed.", static_cast<int>(socket));
                    return;
                }

                while (!shouldTerminate())
                {
                    // TCP and Unix Domain Server implementation.
                    //
                    // Use event-based notification with array of client
                    // bidirectional stream sockets concurrently processed
                    // by single thread. Facility differs depending on OS:
                    //
                    // - Windows: use WSA socket events
                    // - Linux:   use epoll
                    // - Mac:     use kqueue
                    //
#ifdef _WIN32
                    DWORD numEvents = static_cast<DWORD>(m_events.size());
                    DWORD dwResult = ::WSAWaitForMultipleEvents(numEvents,
                        m_events.data(), FALSE, config::REACTOR_POLL_TIMEOUT_MS, FALSE);
                    if (dwResult == WSA_WAIT_TIMEOUT)
                    {
                        continue;
                    }

                    if (dwResult == WSA_WAIT_FAILED)
                    {
                        // E.g. no events registered, or an event handle was closed
                        // concurrently. Back off instead of busy-looping.
                        LOG_WARN("Reactor: WSAWaitForMultipleEvents failed, error=%d", ::WSAGetLastError());
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        continue;
                    }

                    if (dwResult >= WSA_WAIT_EVENT_0 + numEvents)
                    {
                        LOG_WARN("Reactor: stale event on closed socket dwResult=%d", (int)dwResult);
                        continue;
                    }

                    size_t index = static_cast<size_t>(dwResult - WSA_WAIT_EVENT_0);

                    Socket socket;
                    int flags = 0;
                    WSAEVENT hEvent = WSA_INVALID_EVENT;
                    {
                        LOCKGUARD(m_sockets_mutex);
                        if ((index >= m_sockets.size()) || (index >= m_events.size()))
                        {
                            continue;
                        }
                        socket = m_sockets[index].socket;
                        flags = m_sockets[index].flags;
                        hEvent = m_events[index];
                    }

                    WSANETWORKEVENTS ne = {};
                    ::WSAEnumNetworkEvents(socket, hEvent, &ne);
                    LOG_TRACE(
                        "Reactor: Handling socket 0x%x (index %d) with active flags 0x%x "
                        "(armed 0x%x)",
                        static_cast<int>(socket), static_cast<int>(index), ne.lNetworkEvents, flags);

                    if ((flags & Readable) && (ne.lNetworkEvents & FD_READ))
                    {
                        m_callback.onSocketReadable(socket);
                    }
                    if ((flags & Writable) && (ne.lNetworkEvents & FD_WRITE))
                    {
                        m_callback.onSocketWritable(socket);
                    }
                    if ((flags & Acceptable) && (ne.lNetworkEvents & FD_ACCEPT))
                    {
                        m_callback.onSocketAcceptable(socket);
                    }
                    if ((flags & Closed) && (ne.lNetworkEvents & FD_CLOSE))
                    {
                        m_callback.onSocketClosed(socket);
                    }
#endif

#ifdef __linux__
                    {
                        epoll_event events[4];
                        int result = ::epoll_wait(m_epollFd, events, sizeof(events) / sizeof(events[0]), config::REACTOR_POLL_TIMEOUT_MS);
                        if (result == 0)
                            continue;
                        if (result < 0)
                        {
                            if (errno != EINTR)
                            {
                                LOG_ERROR("Reactor: got errno=%d!", errno);
                                // Back off instead of busy-looping on a persistent error.
                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            }
                            continue;
                        };
                        assert(result >= 1 && static_cast<size_t>(result) <= sizeof(events) / sizeof(events[0]));

                        struct ReadySocket
                        {
                            Socket socket;
                            int flags;
                            uint32_t events;
                        };
                        std::vector<ReadySocket> ready;
                        ready.reserve(result);
                        for (int i = 0; i < result; i++)
                        {
                            LOCKGUARD(m_sockets_mutex);
                            auto it = std::find(m_sockets.begin(), m_sockets.end(), events[i].data.fd);
                            if (it == m_sockets.end())
                                continue;
                            ready.push_back({it->socket, it->flags, events[i].events});
                        }

                        for (const auto& readySocket : ready)
                        {
                            Socket socket = readySocket.socket;
                            int flags = readySocket.flags;
                            uint32_t activeEvents = readySocket.events;

                            LOG_TRACE("Reactor: Handling socket 0x%x active flags 0x%x (armed 0x%x)",
                                static_cast<int>(socket), activeEvents, flags);

                            if ((flags & Readable) && (activeEvents & EPOLLIN))
                            {
                                m_callback.onSocketReadable(socket);
                            }
                            if ((flags & Writable) && (activeEvents & EPOLLOUT))
                            {
                                m_callback.onSocketWritable(socket);
                            }
                            if ((flags & Acceptable) && (activeEvents & EPOLLIN))
                            {
                                m_callback.onSocketAcceptable(socket);
                            }
                            if ((flags & Closed) && (activeEvents & (EPOLLHUP | EPOLLERR)))
                            {
                                LOG_TRACE("Reactor: handling socket 0x%x onSocketClosed", static_cast<int>(socket));
                                m_callback.onSocketClosed(socket);
                            }
                        }
                    }
#endif

#if defined(TARGET_OS_MAC)
                    {
                        constexpr unsigned waitms = config::REACTOR_POLL_TIMEOUT_MS;
                        struct timespec timeout;
                        timeout.tv_sec = waitms / 1000;
                        timeout.tv_nsec = (waitms % 1000) * 1000 * 1000;

                        int nev = kevent(kq, NULL, 0, m_events, KQUEUE_SIZE, &timeout);
                        if (nev < 0)
                        {
                            if (errno != EINTR)
                            {
                                LOG_ERROR("Reactor: kevent failed, errno=%d", errno);
                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            }
                            continue;
                        }

                        // Snapshot the ready sockets while holding m_sockets_mutex,
                        // then invoke the user callbacks after releasing the lock.
                        // Holding the lock across callbacks caused a lock inversion
                        // with m_connectionsMutex (a persistent SSE worker holding
                        // m_connectionsMutex may call back into the reactor), and a
                        // socket may be concurrently removed.
                        struct ReadyEvent
                        {
                            Socket socket;
                            int flags;
                            int filter;
                            unsigned evflags;
                        };
                        std::vector<ReadyEvent> ready;
                        if (nev > 0)
                        {
                            ready.reserve(static_cast<size_t>(nev));
                        }

                        {
                            LOCKGUARD(m_sockets_mutex);
                            for (int i = 0; i < nev; i++)
                            {
                                struct kevent& event = m_events[i];
                                int fd = (int)event.ident;
                                auto it = std::find(m_sockets.begin(), m_sockets.end(), fd);
                                if (it == m_sockets.end())
                                    continue;

                                LOG_TRACE("Handling socket 0x%x filter %d flags 0x%x (armed 0x%x)",
                                    static_cast<int>(it->socket), static_cast<int>(event.filter),
                                    static_cast<unsigned>(event.flags), it->flags);

                                ready.push_back(
                                    { it->socket, it->flags, event.filter, static_cast<unsigned>(event.flags) });
                            }
                        }

                        for (const auto& readyEvent : ready)
                        {
                            Socket socket = readyEvent.socket;
                            int flags = readyEvent.flags;
                            // EOF (peer closed) / error is reported on the read or write
                            // filter itself via EV_EOF / EV_ERROR.
                            bool eof = (readyEvent.evflags & (EV_EOF | EV_ERROR)) != 0;

                            if (readyEvent.filter == EVFILT_READ)
                            {
                                if (flags & Acceptable)
                                {
                                    m_callback.onSocketAcceptable(socket);
                                }
                                if (flags & Readable)
                                {
                                    // Readable handler drains remaining data and
                                    // observes EOF as a 0-byte read.
                                    m_callback.onSocketReadable(socket);
                                }
                                else if (eof && (flags & Closed))
                                {
                                    m_callback.onSocketClosed(socket);
                                }
                                continue;
                            }

                            if (readyEvent.filter == EVFILT_WRITE)
                            {
                                if (flags & Writable)
                                {
                                    m_callback.onSocketWritable(socket);
                                }
                                else if (eof && (flags & Closed))
                                {
                                    m_callback.onSocketClosed(socket);
                                }
                                continue;
                            }
                            LOG_ERROR("Reactor: unhandled kevent!");
                        }
                    }
#endif
                }
                LOG_TRACE("Reactor: Thread done");
            }

        private:
            /// <summary>
            /// Wait until socket is readable (or has a pending error).
            /// </summary>
            /// <returns>1 if ready, 0 on timeout, -1 on wait error.</returns>
            static int waitReadable(const Socket& socket, unsigned timeoutMs)
            {
#ifdef _WIN32
                fd_set readSet;
                fd_set errorSet;
                FD_ZERO(&readSet);
                FD_ZERO(&errorSet);
                FD_SET(socket.m_sock, &readSet);
                FD_SET(socket.m_sock, &errorSet);
                timeval tv;
                tv.tv_sec = static_cast<long>(timeoutMs / 1000);
                tv.tv_usec = static_cast<long>((timeoutMs % 1000) * 1000);
                int rc = ::select(0, &readSet, nullptr, &errorSet, &tv);
                if (rc == SOCKET_ERROR)
                {
                    return -1;
                }
                return (rc > 0) ? 1 : 0;
#else
                pollfd pfd = {};
                pfd.fd = socket.m_sock;
                pfd.events = POLLIN;
                int rc = ::poll(&pfd, 1, static_cast<int>(timeoutMs));
                if (rc < 0)
                {
                    return (errno == EINTR) ? 0 : -1;
                }
                if (rc == 0)
                {
                    return 0;
                }
                if (pfd.revents & POLLNVAL)
                {
                    // Socket is not open.
                    return -1;
                }
                // POLLIN, or POLLERR (pending ICMP error that recvfrom() will clear).
                return 1;
#endif
            }

#ifdef TARGET_OS_MAC
            /// <summary>
            /// (Re)register kqueue filters for a socket according to armed flags.
            /// kqueue filters are level-triggered, so EVFILT_WRITE is registered only
            /// while Writable is armed (otherwise an idle socket spins the loop).
            /// When only Closed is armed, EVFILT_READ is edge-triggered (EV_CLEAR) so
            /// that unread data does not spin the loop while EV_EOF is still observed.
            /// </summary>
            void kqueueArm(const Socket& socket, int flags)
            {
                uintptr_t ident = static_cast<uintptr_t>(socket.m_sock);
                struct kevent kev;
                // Deleting may fail with ENOENT when the filter is absent - that is fine.
                // Delete + add (rather than modify) so that EV_CLEAR is applied reliably.
                EV_SET(&kev, ident, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                (void)kevent(kq, &kev, 1, NULL, 0, NULL);
                if (flags & (Readable | Acceptable))
                {
                    EV_SET(&kev, ident, EVFILT_READ, EV_ADD, 0, 0, NULL);
                    (void)kevent(kq, &kev, 1, NULL, 0, NULL);
                }
                else if (flags & Closed)
                {
                    EV_SET(&kev, ident, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
                    (void)kevent(kq, &kev, 1, NULL, 0, NULL);
                }
                EV_SET(&kev, ident, EVFILT_WRITE, (flags & Writable) ? EV_ADD : EV_DELETE, 0, 0, NULL);
                (void)kevent(kq, &kev, 1, NULL, 0, NULL);
            }

            /// <summary>
            /// Remove all kqueue filters for a socket (missing filters are ignored).
            /// </summary>
            void kqueueDisarm(const Socket& socket)
            {
                uintptr_t ident = static_cast<uintptr_t>(socket.m_sock);
                struct kevent kev;
                EV_SET(&kev, ident, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                (void)kevent(kq, &kev, 1, NULL, 0, NULL);
                EV_SET(&kev, ident, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
                (void)kevent(kq, &kev, 1, NULL, 0, NULL);
            }
#endif
        };

    }
}
SOCKETSHPP_NS_END

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
