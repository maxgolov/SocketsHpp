// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file process.h
/// @brief Child processes with piped stdin/stdout (and optionally stderr):
///        utils::ChildProcess, utils::PipeReader and utils::writeAll().
///
/// Used by the MCP client's STDIO transport (mcp::client::MCPClient) and by
/// mcp::server::StdioServerTransport, but usable on its own.
///
/// - POSIX: fork() + execve() (the executable is resolved against PATH before the
///   fork, so only async-signal-safe calls run in the child). Every descriptor the
///   library creates is close-on-exec. Writes never raise SIGPIPE: a write to a pipe
///   whose reader is gone fails with EPIPE instead.
/// - Windows: CreateProcessW() with anonymous pipes (UTF-8 arguments are converted
///   to UTF-16). `.cmd` / `.bat` commands (e.g. `npx`) are run through `cmd.exe /d /s /c`.

#include <SocketsHpp/config.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <windows.h>
#  ifdef min
#    undef min
#    undef max
#  endif
#  include <cwchar>
#else
#  include <fcntl.h>
#  include <poll.h>
#  include <pthread.h>
#  include <signal.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <time.h>
#  include <unistd.h>
#  ifdef __APPLE__
#    include <crt_externs.h>
#  else
extern "C"
{
    /// @brief The current process environment (POSIX; declared here because not every
    ///        C library declares it in <unistd.h>).
    extern char** environ;
}
#  endif
#endif

SOCKETSHPP_NS_BEGIN
namespace utils
{
#ifdef _WIN32
    /// @brief Native pipe/file handle type: HANDLE on Windows, a file descriptor on POSIX.
    using NativeHandle = void*;
#else
    /// @brief Native pipe/file handle type: HANDLE on Windows, a file descriptor on POSIX.
    using NativeHandle = int;
#endif

    /// @brief The invalid NativeHandle value (INVALID_HANDLE_VALUE on Windows, -1 on POSIX).
    /// @return The platform's invalid handle.
    inline NativeHandle invalidNativeHandle()
    {
#ifdef _WIN32
        return INVALID_HANDLE_VALUE;
#else
        return -1;
#endif
    }

    /// @brief Implementation details of process.h.
    namespace detail
    {
        /// @brief Close a handle (if valid) and reset it to invalidNativeHandle().
        /// @param h Handle to close.
        inline void closeNativeHandle(NativeHandle& h)
        {
#ifdef _WIN32
            if (h != INVALID_HANDLE_VALUE && h != nullptr)
                ::CloseHandle(h);
#else
            if (h >= 0)
                ::close(h);
#endif
            h = invalidNativeHandle();
        }

#ifndef _WIN32
        /// @brief Move a descriptor above 2 (so dup2() onto 0/1/2 in a child never
        ///        clobbers another pipe end) and make it close-on-exec.
        /// @param fd Descriptor; replaced by the new one when it had to be moved.
        /// @return false if duplicating failed.
        inline bool moveAboveStdio(int& fd)
        {
            if (fd > 2)
                return true;
            const int moved = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
            if (moved < 0)
                return false;
            ::close(fd);
            fd = moved;
            return true;
        }

        /// @brief Create a close-on-exec pipe whose ends are both above 2.
        /// @param fds Receives {read end, write end}.
        /// @return false on failure (nothing is left open).
        inline bool makePipe(int fds[2])
        {
#  if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
            if (::pipe2(fds, O_CLOEXEC) != 0)
                return false;
#  else
            if (::pipe(fds) != 0)
                return false;
            ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
            ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
#  endif
            if (!moveAboveStdio(fds[0]) || !moveAboveStdio(fds[1]))
            {
                ::close(fds[0]);
                ::close(fds[1]);
                return false;
            }
            return true;
        }

        /// @brief The current process environment ("NAME=value" strings).
        /// @return environ (or _NSGetEnviron() on Apple platforms).
        inline char** currentEnviron()
        {
#  ifdef __APPLE__
            return *_NSGetEnviron();
#  else
            return environ;
#  endif
        }

        /// @brief Resolve a command like execvp() would: a name containing '/' is used
        ///        as is, anything else is searched in the colon-separated @p path.
        /// @param command Command name or path.
        /// @param path    Search path (the child's PATH).
        /// @return Path of an executable regular file, or "" if none was found.
        inline std::string resolveExecutable(const std::string& command, const std::string& path)
        {
            auto isExecutable = [](const std::string& p) {
                struct stat st;
                return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && ::access(p.c_str(), X_OK) == 0;
            };
            if (command.empty())
                return std::string();
            if (command.find('/') != std::string::npos)
                return isExecutable(command) ? command : std::string();
            size_t start = 0;
            while (start <= path.size())
            {
                size_t colon = path.find(':', start);
                std::string dir = path.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
                if (dir.empty())
                    dir = ".";  // an empty PATH entry means the current directory
                const std::string candidate = dir + "/" + command;
                if (isExecutable(candidate))
                    return candidate;
                if (colon == std::string::npos)
                    break;
                start = colon + 1;
            }
            return std::string();
        }
#else
        /// @brief Convert UTF-8 to UTF-16.
        /// @param s UTF-8 text.
        /// @return The UTF-16 text (invalid sequences are replaced).
        inline std::wstring toWide(const std::string& s)
        {
            if (s.empty())
                return std::wstring();
            const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(static_cast<size_t>(n), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
            return w;
        }

        /// @brief Quote one argument for CreateProcess() following the MSVC runtime's
        ///        command-line parsing rules (backslashes before quotes are doubled).
        /// @param arg Argument.
        /// @return The argument, quoted if needed.
        inline std::wstring quoteArgument(const std::wstring& arg)
        {
            if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos)
                return arg;
            std::wstring out = L"\"";
            for (auto it = arg.begin();; ++it)
            {
                size_t backslashes = 0;
                while (it != arg.end() && *it == L'\\')
                {
                    ++it;
                    ++backslashes;
                }
                if (it == arg.end())
                {
                    out.append(backslashes * 2, L'\\');
                    break;
                }
                if (*it == L'"')
                {
                    out.append(backslashes * 2 + 1, L'\\');
                    out.push_back(L'"');
                }
                else
                {
                    out.append(backslashes, L'\\');
                    out.push_back(*it);
                }
            }
            out.push_back(L'"');
            return out;
        }

        /// @brief Case-insensitive wide-string equality (ASCII folding is enough for
        ///        environment variable names and file extensions).
        /// @param a First string.
        /// @param b Second string.
        /// @return true if equal ignoring case.
        inline bool iequalsWide(const std::wstring& a, const std::wstring& b)
        {
            return a.size() == b.size() && ::_wcsicmp(a.c_str(), b.c_str()) == 0;
        }

        /// @brief Name part of a "NAME=value" environment entry (a leading '=' is part
        ///        of the name, as in the hidden "=C:" entries).
        /// @param entry Environment entry.
        /// @return The name.
        inline std::wstring envName(const std::wstring& entry)
        {
            const size_t eq = entry.find(L'=', 1);
            return entry.substr(0, eq);
        }

        /// @brief Whether @p path names an existing file (not a directory).
        /// @param path Path.
        /// @return true for an existing non-directory.
        inline bool fileExists(const std::wstring& path)
        {
            const DWORD attrs = ::GetFileAttributesW(path.c_str());
            return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
        }

        /// @brief Resolve a command like cmd.exe would: try the name as given (when it has
        ///        an extension) and with each PATHEXT extension, in the current directory
        ///        and then in each PATH directory (only the current path when the command
        ///        contains a directory part).
        /// @param command Command name or path.
        /// @param path    Semicolon-separated search path.
        /// @return Full path of the file found, or L"" if none.
        inline std::wstring resolveExecutable(const std::wstring& command, const std::wstring& path)
        {
            if (command.empty())
                return std::wstring();
            const size_t lastSep = command.find_last_of(L"\\/:");
            const size_t dot = command.find_last_of(L'.');
            const bool hasExtension = dot != std::wstring::npos && (lastSep == std::wstring::npos || dot > lastSep);

            std::vector<std::wstring> exts;
            if (hasExtension)
                exts.push_back(L"");
            std::wstring pathext = L".COM;.EXE;.BAT;.CMD";
            wchar_t buf[1024];
            const DWORD n = ::GetEnvironmentVariableW(L"PATHEXT", buf, 1024);
            if (n > 0 && n < 1024)
                pathext.assign(buf, n);
            size_t start = 0;
            while (start <= pathext.size())
            {
                size_t semi = pathext.find(L';', start);
                std::wstring e = pathext.substr(start, semi == std::wstring::npos ? std::wstring::npos : semi - start);
                if (!e.empty())
                    exts.push_back(e);
                if (semi == std::wstring::npos)
                    break;
                start = semi + 1;
            }

            std::vector<std::wstring> dirs;
            if (lastSep != std::wstring::npos)
            {
                dirs.push_back(L"");
            }
            else
            {
                dirs.push_back(L".\\");
                start = 0;
                while (start <= path.size())
                {
                    size_t semi = path.find(L';', start);
                    std::wstring d = path.substr(start, semi == std::wstring::npos ? std::wstring::npos : semi - start);
                    if (d.size() >= 2 && d.front() == L'"' && d.back() == L'"')
                        d = d.substr(1, d.size() - 2);
                    if (!d.empty())
                    {
                        if (d.back() != L'\\' && d.back() != L'/')
                            d.push_back(L'\\');
                        dirs.push_back(d);
                    }
                    if (semi == std::wstring::npos)
                        break;
                    start = semi + 1;
                }
            }

            for (const auto& dir : dirs)
            {
                for (const auto& ext : exts)
                {
                    const std::wstring candidate = dir + command + ext;
                    if (fileExists(candidate))
                    {
                        wchar_t full[MAX_PATH * 4];
                        const DWORD len = ::GetFullPathNameW(candidate.c_str(), MAX_PATH * 4, full, nullptr);
                        return (len > 0 && len < MAX_PATH * 4) ? std::wstring(full, len) : candidate;
                    }
                }
            }
            return std::wstring();
        }
#endif
    }  // namespace detail

    /// @brief Write all bytes to a pipe or file handle.
    ///
    /// Retries partial writes and EINTR. On POSIX a write to a pipe whose read end is
    /// closed fails with EPIPE instead of raising SIGPIPE (the signal is blocked for
    /// the calling thread during the write and a resulting pending SIGPIPE is consumed).
    /// @param h    Handle to write to.
    /// @param data Bytes to write.
    /// @param len  Number of bytes.
    /// @return true if every byte was written; false on error (e.g. the reader exited).
    inline bool writeAll(NativeHandle h, const char* data, size_t len)
    {
#ifdef _WIN32
        while (len > 0)
        {
            DWORD written = 0;
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(len, 1u << 20));
            if (!::WriteFile(h, data, chunk, &written, nullptr))
                return false;
            data += written;
            len -= written;
        }
        return true;
#else
#  if !defined(__APPLE__)
        sigset_t pipeSet;
        sigset_t oldMask;
        sigemptyset(&pipeSet);
        sigaddset(&pipeSet, SIGPIPE);
        ::pthread_sigmask(SIG_BLOCK, &pipeSet, &oldMask);
        sigset_t pending;
        sigemptyset(&pending);
        ::sigpending(&pending);
        const bool wasPending = sigismember(&pending, SIGPIPE) == 1;
#  endif
        bool ok = true;
        bool brokenPipe = false;
        while (len > 0)
        {
            const ssize_t n = ::write(h, data, len);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                brokenPipe = (errno == EPIPE);
                ok = false;
                break;
            }
            data += n;
            len -= static_cast<size_t>(n);
        }
#  if !defined(__APPLE__)
        if (brokenPipe && !wasPending)
        {
            // Consume the SIGPIPE our own write raised so it is never delivered.
            struct timespec zero = {0, 0};
            while (::sigtimedwait(&pipeSet, nullptr, &zero) < 0 && errno == EINTR)
            {
            }
        }
        ::pthread_sigmask(SIG_SETMASK, &oldMask, nullptr);
#  else
        (void)brokenPipe;  // Apple: descriptors are marked F_SETNOSIGPIPE by their owner
#  endif
        return ok;
#endif
    }

    /// @brief Blocking reads from a pipe or file handle that another thread can interrupt.
    ///
    /// POSIX: poll() on the handle and an internal wake-up pipe. Windows: ReadFile()
    /// interrupted with CancelSynchronousIo(). The handle itself is not owned.
    /// @note read() may be called from one thread at a time; interrupt() from any thread.
    class PipeReader
    {
    public:
        /// @brief Create a reader with no handle (read() fails until open()).
        PipeReader() = default;
        /// @brief Non-copyable.
        PipeReader(const PipeReader&) = delete;
        /// @brief Non-copyable.
        PipeReader& operator=(const PipeReader&) = delete;
        /// @brief Releases the internal wake-up resources (not the handle).
        ~PipeReader()
        {
#ifndef _WIN32
            detail::closeNativeHandle(m_wake[0]);
            detail::closeNativeHandle(m_wake[1]);
#endif
        }

        /// @brief Attach the handle to read from (not owned) and clear a previous interrupt().
        /// @param h Readable pipe or file handle.
        /// @return false if the wake-up pipe could not be created (POSIX).
        bool open(NativeHandle h)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_handle = h;
            m_interrupted = false;
#ifndef _WIN32
            detail::closeNativeHandle(m_wake[0]);
            detail::closeNativeHandle(m_wake[1]);
            return detail::makePipe(m_wake);
#else
            return true;
#endif
        }

        /// @brief Read up to @p size bytes, blocking until data, end of file or interrupt().
        /// @param buffer Destination.
        /// @param size   Capacity of @p buffer (> 0).
        /// @return Bytes read (> 0), 0 at end of file (all writers closed), or -1 on error
        ///         or after interrupt().
        long read(char* buffer, size_t size)
        {
#ifdef _WIN32
            for (;;)
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_interrupted)
                        return -1;
                    ::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentThread(), ::GetCurrentProcess(),
                                      &m_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
                }
                DWORD got = 0;
                const BOOL ok = ::ReadFile(m_handle, buffer, static_cast<DWORD>(std::min<size_t>(size, 1u << 20)),
                                           &got, nullptr);
                const DWORD err = ok ? 0 : ::GetLastError();
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_thread != nullptr)
                    {
                        ::CloseHandle(m_thread);
                        m_thread = nullptr;
                    }
                    if (m_interrupted)
                        return -1;
                }
                if (ok)
                {
                    if (got == 0)
                        continue;  // a zero-length write on the other end; not EOF
                    return static_cast<long>(got);
                }
                if (err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF)
                    return 0;
                return -1;
            }
#else
            for (;;)
            {
                struct pollfd fds[2];
                fds[0].fd = m_handle;
                fds[0].events = POLLIN;
                fds[0].revents = 0;
                fds[1].fd = m_wake[0];
                fds[1].events = POLLIN;
                fds[1].revents = 0;
                const int r = ::poll(fds, 2, -1);
                if (r < 0)
                {
                    if (errno == EINTR)
                        continue;
                    return -1;
                }
                if (fds[1].revents != 0)
                    return -1;  // interrupted (the wake byte is left in place: sticky)
                if (fds[0].revents & POLLNVAL)
                    return -1;
                if (fds[0].revents != 0)
                {
                    const ssize_t n = ::read(m_handle, buffer, size);
                    if (n < 0)
                    {
                        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                            continue;
                        return -1;
                    }
                    return static_cast<long>(n);
                }
            }
#endif
        }

        /// @brief Make the current and every later read() return -1 until open() is
        ///        called again. Thread-safe.
        /// @note Windows: waits (at most about one second) for a read() blocked in
        ///       ReadFile() to be cancelled.
        void interrupt()
        {
#ifdef _WIN32
            for (int attempt = 0; attempt < 200; ++attempt)
            {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_interrupted = true;
                    if (m_thread == nullptr)
                        return;  // no read in progress; later reads see the flag
                    if (::CancelSynchronousIo(m_thread))
                        return;
                }
                // The reader published its thread but has not entered ReadFile() yet.
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
#else
            std::lock_guard<std::mutex> lock(m_mutex);
            m_interrupted = true;
            if (m_wake[1] >= 0)
            {
                const char byte = 1;
                while (::write(m_wake[1], &byte, 1) < 0 && errno == EINTR)
                {
                }
            }
#endif
        }

    private:
        std::mutex m_mutex;
        NativeHandle m_handle = invalidNativeHandle();
        bool m_interrupted = false;
#ifdef _WIN32
        HANDLE m_thread = nullptr;  // thread blocked in ReadFile(), if any
#else
        int m_wake[2] = {-1, -1};
#endif
    };

    /// @brief How a child process's standard error is connected (ProcessOptions::stderrMode).
    enum class StderrMode
    {
        Inherit,  ///< Share the parent's standard error (default).
        Pipe,     ///< Capture it; read with ChildProcess::readStderr().
        Discard   ///< Redirect it to the null device.
    };

    /// @brief Parameters for ChildProcess::start().
    struct ProcessOptions
    {
        /// @brief Program to run. A name without a directory part is searched in PATH
        ///        (the child's PATH when #env overrides it); on Windows the PATHEXT
        ///        extensions are tried too.
        std::string command;
        /// @brief Arguments after the program name (argv[1..]).
        std::vector<std::string> args;
        /// @brief Environment variables added to (or replacing entries of) the parent's
        ///        environment, which the child otherwise inherits unchanged.
        std::map<std::string, std::string> env;
        /// @brief Working directory of the child (default: the parent's).
        std::optional<std::string> cwd;
        /// @brief Where the child's standard error goes (default StderrMode::Inherit).
        StderrMode stderrMode = StderrMode::Inherit;
    };

    /// @brief A child process whose stdin and stdout (and optionally stderr) are pipes.
    ///
    /// The destructor closes stdin, kills the child if it is still running and reaps
    /// it, so no zombie process is left behind. For a graceful shutdown call
    /// closeStdin(), then waitForExit() with a timeout, then terminate() / kill().
    ///
    /// @note Thread-safety: writeStdin()/closeStdin() may be called from one thread and
    ///       readStdout()/readStderr() from others; interruptReads(), waitForExit(),
    ///       running(), terminate() and kill() are thread-safe. start() and the
    ///       destructor must not run concurrently with anything else.
    class ChildProcess
    {
    public:
        /// @brief Create an object with no process; call start().
        ChildProcess() = default;
        /// @brief Non-copyable.
        ChildProcess(const ChildProcess&) = delete;
        /// @brief Non-copyable.
        ChildProcess& operator=(const ChildProcess&) = delete;

        /// @brief Kills the child if it is still running, reaps it and closes all pipes.
        ~ChildProcess()
        {
            if (m_started)
            {
                closeStdin();
                if (!waitForExit(0))
                {
                    kill();
                    waitForExit(-1);
                }
            }
            interruptReads();
            {
                std::lock_guard<std::mutex> lock(m_stdinMutex);
                detail::closeNativeHandle(m_stdin);
            }
            detail::closeNativeHandle(m_stdout);
            detail::closeNativeHandle(m_stderr);
#ifdef _WIN32
            if (m_process != nullptr)
                ::CloseHandle(m_process);
#endif
        }

        /// @brief Start the process.
        /// @param options Program, arguments, environment, working directory, stderr mode.
        /// @param error   Optional; receives a description when starting fails.
        /// @return true if the process was started (on POSIX: execve() succeeded). false
        ///         if the executable was not found, pipes could not be created, the
        ///         working directory is invalid or exec failed, or if start() was
        ///         already called.
        bool start(const ProcessOptions& options, std::string* error = nullptr)
        {
            auto fail = [error](const std::string& why) {
                if (error)
                    *error = why;
                return false;
            };
            if (m_started)
                return fail("process already started");
#ifdef _WIN32
            return startWindows(options, fail);
#else
            return startPosix(options, fail);
#endif
        }

        /// @brief Write bytes to the child's stdin.
        /// @param data Bytes.
        /// @param len  Byte count.
        /// @return false if stdin is closed or the child stopped reading (e.g. exited).
        bool writeStdin(const char* data, size_t len)
        {
            std::lock_guard<std::mutex> lock(m_stdinMutex);
            if (m_stdin == invalidNativeHandle())
                return false;
            const bool ok = writeAll(m_stdin, data, len);
            if (m_closeStdinPending)
                detail::closeNativeHandle(m_stdin);
            return ok;
        }

        /// @brief Close the child's stdin (it sees end of file). Never blocks: if a
        ///        writeStdin() is in progress on another thread, stdin is closed as soon
        ///        as that write returns.
        void closeStdin()
        {
            std::unique_lock<std::mutex> lock(m_stdinMutex, std::try_to_lock);
            if (!lock.owns_lock())
            {
                m_closeStdinPending = true;
                return;
            }
            detail::closeNativeHandle(m_stdin);
        }

        /// @brief Read from the child's stdout (blocking).
        /// @param buffer Destination.
        /// @param size   Capacity (> 0).
        /// @return Bytes read, 0 at end of file, -1 on error or after interruptReads().
        long readStdout(char* buffer, size_t size) { return m_stdoutReader.read(buffer, size); }

        /// @brief Read from the child's stderr (only with StderrMode::Pipe; blocking).
        /// @param buffer Destination.
        /// @param size   Capacity (> 0).
        /// @return Bytes read, 0 at end of file, -1 on error, after interruptReads() or
        ///         when stderr is not piped.
        long readStderr(char* buffer, size_t size)
        {
            if (m_stderr == invalidNativeHandle())
                return -1;
            return m_stderrReader.read(buffer, size);
        }

        /// @brief Make blocked and future readStdout()/readStderr() calls return -1.
        ///        Thread-safe. Useful when a grandchild keeps the pipes open after the
        ///        child exited.
        void interruptReads()
        {
            m_stdoutReader.interrupt();
            m_stderrReader.interrupt();
        }

        /// @brief Wait for the child to exit and reap it.
        /// @param timeoutMs Maximum wait in milliseconds; 0 only polls; < 0 waits forever.
        /// @return true if the child has exited (or was never started).
        bool waitForExit(int timeoutMs)
        {
            std::lock_guard<std::mutex> lock(m_waitMutex);
            if (!m_started || m_exited)
                return true;
#ifdef _WIN32
            const DWORD r = ::WaitForSingleObject(m_process, timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs));
            if (r != WAIT_OBJECT_0)
                return false;
            DWORD code = 0;
            ::GetExitCodeProcess(m_process, &code);
            m_exitCode = static_cast<int>(code);
            m_exited = true;
            return true;
#else
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs < 0 ? 0 : timeoutMs);
            for (;;)
            {
                int status = 0;
                const pid_t r = ::waitpid(m_pid, &status, timeoutMs < 0 ? 0 : WNOHANG);
                if (r == m_pid)
                {
                    if (WIFEXITED(status))
                        m_exitCode = WEXITSTATUS(status);
                    else if (WIFSIGNALED(status))
                        m_exitCode = 128 + WTERMSIG(status);
                    m_exited = true;
                    return true;
                }
                if (r < 0)
                {
                    if (errno == EINTR)
                        continue;
                    // ECHILD: already reaped elsewhere (e.g. SIGCHLD set to SIG_IGN)
                    m_exited = true;
                    return true;
                }
                const auto now = std::chrono::steady_clock::now();
                if (now >= deadline)
                    return false;
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
                std::this_thread::sleep_for(std::min(left, std::chrono::milliseconds(5)));
            }
#endif
        }

        /// @brief Whether the child was started and has not exited yet (polls; reaps it
        ///        if it has exited). Thread-safe.
        /// @return true while running.
        bool running() { return m_started && !waitForExit(0); }

        /// @brief Ask the child to terminate: SIGTERM on POSIX; TerminateProcess() on
        ///        Windows, which has no graceful equivalent for a console-less child.
        ///        No-op once the child has been reaped. Thread-safe.
        void terminate()
        {
#ifdef _WIN32
            kill();
#else
            sendSignal(SIGTERM);
#endif
        }

        /// @brief Kill the child (SIGKILL / TerminateProcess()). No-op once it has been
        ///        reaped. Thread-safe.
        void kill()
        {
#ifdef _WIN32
            std::lock_guard<std::mutex> lock(m_waitMutex);
            if (m_started && !m_exited)
                ::TerminateProcess(m_process, 1);
#else
            sendSignal(SIGKILL);
#endif
        }

        /// @brief Exit status once waitForExit() / running() observed the exit.
        /// @return The exit code; on POSIX 128 + signal number for a child killed by a
        ///         signal; -1 while running, if never started, or if unknown.
        int exitCode() const
        {
            std::lock_guard<std::mutex> lock(m_waitMutex);
            return m_exitCode;
        }

        /// @brief Process id of the child.
        /// @return The pid, or -1 if not started.
        std::int64_t pid() const { return m_started ? static_cast<std::int64_t>(m_pid) : -1; }

    private:
        bool m_started = false;
        bool m_exited = false;
        int m_exitCode = -1;
        bool m_closeStdinPending = false;
        mutable std::mutex m_waitMutex;
        std::mutex m_stdinMutex;
        NativeHandle m_stdin = invalidNativeHandle();
        NativeHandle m_stdout = invalidNativeHandle();
        NativeHandle m_stderr = invalidNativeHandle();
        PipeReader m_stdoutReader;
        PipeReader m_stderrReader;
#ifdef _WIN32
        HANDLE m_process = nullptr;
        DWORD m_pid = 0;
#else
        pid_t m_pid = -1;

        void sendSignal(int sig)
        {
            std::lock_guard<std::mutex> lock(m_waitMutex);
            if (m_started && !m_exited && m_pid > 0)
                ::kill(m_pid, sig);  // not reaped yet, so the pid cannot have been reused
        }

        template <typename Fail>
        bool startPosix(const ProcessOptions& options, Fail& fail)
        {
            // Everything the child needs is prepared before fork(): after it only
            // async-signal-safe calls are made.
            std::map<std::string, std::string> envMap;
            for (char** e = detail::currentEnviron(); e != nullptr && *e != nullptr; ++e)
            {
                const std::string entry(*e);
                const size_t eq = entry.find('=');
                if (eq != std::string::npos)
                    envMap[entry.substr(0, eq)] = entry.substr(eq + 1);
            }
            for (const auto& kv : options.env)
                envMap[kv.first] = kv.second;
            std::vector<std::string> envStrings;
            envStrings.reserve(envMap.size());
            for (const auto& kv : envMap)
                envStrings.push_back(kv.first + "=" + kv.second);
            std::vector<char*> envp;
            for (auto& s : envStrings)
                envp.push_back(&s[0]);
            envp.push_back(nullptr);

            auto pathIt = envMap.find("PATH");
            const std::string exe = detail::resolveExecutable(
                options.command, pathIt != envMap.end() ? pathIt->second : std::string("/usr/bin:/bin"));
            if (exe.empty())
                return fail("command not found or not executable: " + options.command);

            std::vector<std::string> argStrings;
            argStrings.push_back(options.command);
            argStrings.insert(argStrings.end(), options.args.begin(), options.args.end());
            std::vector<char*> argv;
            for (auto& s : argStrings)
                argv.push_back(&s[0]);
            argv.push_back(nullptr);

            int in[2] = {-1, -1};
            int out[2] = {-1, -1};
            int err[2] = {-1, -1};
            int status[2] = {-1, -1};
            int devNull = -1;
            auto closeAll = [&]() {
                for (int* fd : {&in[0], &in[1], &out[0], &out[1], &err[0], &err[1], &status[0], &status[1], &devNull})
                    detail::closeNativeHandle(*fd);
            };
            if (!detail::makePipe(in) || !detail::makePipe(out) || !detail::makePipe(status) ||
                (options.stderrMode == StderrMode::Pipe && !detail::makePipe(err)))
            {
                const int e = errno;
                closeAll();
                return fail(std::string("pipe() failed: ") + std::strerror(e));
            }
            if (options.stderrMode == StderrMode::Discard)
            {
                devNull = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
                if (devNull < 0 || !detail::moveAboveStdio(devNull))
                {
                    closeAll();
                    return fail("cannot open /dev/null");
                }
            }
            const char* cwd = options.cwd ? options.cwd->c_str() : nullptr;
            const int stderrFd = options.stderrMode == StderrMode::Pipe ? err[1]
                               : options.stderrMode == StderrMode::Discard ? devNull : -1;

            const pid_t pid = ::fork();
            if (pid < 0)
            {
                const int e = errno;
                closeAll();
                return fail(std::string("fork() failed: ") + std::strerror(e));
            }
            if (pid == 0)
            {
                // Child. All pipe ends are > 2 and close-on-exec; dup2() clears
                // close-on-exec on the copies at 0/1/2.
                int code = 0;
                if (::dup2(in[0], 0) < 0 || ::dup2(out[1], 1) < 0 || (stderrFd >= 0 && ::dup2(stderrFd, 2) < 0))
                    code = errno;
                if (code == 0 && cwd != nullptr && ::chdir(cwd) != 0)
                    code = errno;
                if (code == 0)
                {
                    sigset_t none;
                    sigemptyset(&none);
                    ::sigprocmask(SIG_SETMASK, &none, nullptr);
                    ::execve(exe.c_str(), argv.data(), envp.data());
                    code = errno;
                }
                ssize_t ignored = ::write(status[1], &code, sizeof(code));
                (void)ignored;
                ::_exit(127);
            }

            // Parent
            detail::closeNativeHandle(in[0]);
            detail::closeNativeHandle(out[1]);
            detail::closeNativeHandle(err[1]);
            detail::closeNativeHandle(devNull);
            detail::closeNativeHandle(status[1]);
            int childErrno = 0;
            ssize_t got;
            do
            {
                got = ::read(status[0], &childErrno, sizeof(childErrno));
            } while (got < 0 && errno == EINTR);
            detail::closeNativeHandle(status[0]);
            if (got == static_cast<ssize_t>(sizeof(childErrno)))
            {
                int st = 0;
                while (::waitpid(pid, &st, 0) < 0 && errno == EINTR)
                {
                }
                closeAll();
                return fail(std::string(cwd != nullptr ? "cannot start (chdir/exec) " : "cannot exec ") +
                            options.command + ": " + std::strerror(childErrno));
            }

#  if defined(F_SETNOSIGPIPE)
            ::fcntl(in[1], F_SETNOSIGPIPE, 1);
#  endif
            m_stdin = in[1];
            m_stdout = out[0];
            m_stderr = err[0];
            in[1] = out[0] = err[0] = -1;
            m_pid = pid;
            m_started = true;
            m_stdoutReader.open(m_stdout);
            if (m_stderr >= 0)
                m_stderrReader.open(m_stderr);
            return true;
        }
#endif

#ifdef _WIN32
        template <typename Fail>
        bool startWindows(const ProcessOptions& options, Fail& fail)
        {
            // Resolve the program against the (possibly overridden) PATH.
            std::wstring path;
            bool pathOverridden = false;
            for (const auto& kv : options.env)
            {
                if (detail::iequalsWide(detail::toWide(kv.first), L"PATH"))
                {
                    path = detail::toWide(kv.second);
                    pathOverridden = true;
                }
            }
            if (!pathOverridden)
            {
                const DWORD n = ::GetEnvironmentVariableW(L"PATH", nullptr, 0);
                if (n > 0)
                {
                    path.resize(n);
                    const DWORD len = ::GetEnvironmentVariableW(L"PATH", &path[0], n);
                    path.resize(len);
                }
            }
            const std::wstring exe = detail::resolveExecutable(detail::toWide(options.command), path);
            if (exe.empty())
                return fail("command not found: " + options.command);

            std::wstring commandLine;
            std::wstring application = exe;
            const size_t dot = exe.find_last_of(L'.');
            const std::wstring ext = dot == std::wstring::npos ? std::wstring() : exe.substr(dot);
            std::wstring args;
            for (const auto& a : options.args)
                args += L" " + detail::quoteArgument(detail::toWide(a));
            if (detail::iequalsWide(ext, L".cmd") || detail::iequalsWide(ext, L".bat"))
            {
                wchar_t comspec[MAX_PATH];
                const DWORD n = ::GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH);
                application = (n > 0 && n < MAX_PATH) ? std::wstring(comspec, n) : std::wstring(L"C:\\Windows\\System32\\cmd.exe");
                commandLine = L"cmd.exe /d /s /c \"" + detail::quoteArgument(exe) + args + L"\"";
            }
            else
            {
                commandLine = detail::quoteArgument(exe) + args;
            }

            // Environment block: the parent's plus overrides, sorted by name.
            std::wstring envBlock;
            if (!options.env.empty())
            {
                std::vector<std::wstring> entries;
                wchar_t* block = ::GetEnvironmentStringsW();
                if (block != nullptr)
                {
                    for (const wchar_t* p = block; *p != L'\0'; p += std::wcslen(p) + 1)
                        entries.emplace_back(p);
                    ::FreeEnvironmentStringsW(block);
                }
                for (const auto& kv : options.env)
                {
                    const std::wstring name = detail::toWide(kv.first);
                    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                                 [&name](const std::wstring& e) {
                                                     return detail::iequalsWide(detail::envName(e), name);
                                                 }),
                                  entries.end());
                    entries.push_back(name + L"=" + detail::toWide(kv.second));
                }
                std::sort(entries.begin(), entries.end(), [](const std::wstring& a, const std::wstring& b) {
                    return ::_wcsicmp(detail::envName(a).c_str(), detail::envName(b).c_str()) < 0;
                });
                for (const auto& e : entries)
                {
                    envBlock += e;
                    envBlock.push_back(L'\0');
                }
                envBlock.push_back(L'\0');
            }

            SECURITY_ATTRIBUTES sa;
            sa.nLength = sizeof(sa);
            sa.lpSecurityDescriptor = nullptr;
            sa.bInheritHandle = TRUE;

            HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
            HANDLE errRead = nullptr, childErr = nullptr;
            auto closeAll = [&]() {
                for (HANDLE* h : {&inRead, &inWrite, &outRead, &outWrite, &errRead, &childErr})
                {
                    if (*h != nullptr && *h != INVALID_HANDLE_VALUE)
                        ::CloseHandle(*h);
                    *h = nullptr;
                }
            };
            if (!::CreatePipe(&inRead, &inWrite, &sa, 0) || !::CreatePipe(&outRead, &outWrite, &sa, 0))
            {
                closeAll();
                return fail("CreatePipe() failed");
            }
            ::SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
            ::SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

            StderrMode mode = options.stderrMode;
            if (mode == StderrMode::Inherit)
            {
                const HANDLE parentErr = ::GetStdHandle(STD_ERROR_HANDLE);
                if (parentErr == nullptr || parentErr == INVALID_HANDLE_VALUE ||
                    !::DuplicateHandle(::GetCurrentProcess(), parentErr, ::GetCurrentProcess(), &childErr, 0, TRUE,
                                       DUPLICATE_SAME_ACCESS))
                {
                    childErr = nullptr;
                    mode = StderrMode::Discard;  // no usable stderr (e.g. a GUI parent)
                }
            }
            if (mode == StderrMode::Pipe)
            {
                if (!::CreatePipe(&errRead, &childErr, &sa, 0))
                {
                    closeAll();
                    return fail("CreatePipe() failed");
                }
                ::SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
            }
            else if (mode == StderrMode::Discard)
            {
                childErr = ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                         OPEN_EXISTING, 0, nullptr);
                if (childErr == INVALID_HANDLE_VALUE)
                    childErr = nullptr;
            }

            STARTUPINFOW si;
            std::memset(&si, 0, sizeof(si));
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdInput = inRead;
            si.hStdOutput = outWrite;
            si.hStdError = childErr;
            PROCESS_INFORMATION pi;
            std::memset(&pi, 0, sizeof(pi));

            const std::wstring cwd = options.cwd ? detail::toWide(*options.cwd) : std::wstring();
            std::vector<wchar_t> cmd(commandLine.begin(), commandLine.end());
            cmd.push_back(L'\0');
            DWORD flags = CREATE_NO_WINDOW;
            if (!envBlock.empty())
                flags |= CREATE_UNICODE_ENVIRONMENT;
            const BOOL ok = ::CreateProcessW(application.c_str(), cmd.data(), nullptr, nullptr, TRUE, flags,
                                             envBlock.empty() ? nullptr : &envBlock[0],
                                             options.cwd ? cwd.c_str() : nullptr, &si, &pi);
            if (!ok)
            {
                const DWORD e = ::GetLastError();
                closeAll();
                return fail("CreateProcess() failed for " + options.command + " (error " + std::to_string(e) + ")");
            }
            ::CloseHandle(pi.hThread);
            // The child owns its ends now; closing ours lets reads see EOF when it exits.
            ::CloseHandle(inRead);
            ::CloseHandle(outWrite);
            if (childErr != nullptr)
                ::CloseHandle(childErr);
            inRead = outWrite = childErr = nullptr;

            m_process = pi.hProcess;
            m_pid = pi.dwProcessId;
            m_stdin = inWrite;
            m_stdout = outRead;
            m_stderr = errRead != nullptr ? errRead : INVALID_HANDLE_VALUE;
            m_started = true;
            m_stdoutReader.open(m_stdout);
            if (m_stderr != INVALID_HANDLE_VALUE)
                m_stderrReader.open(m_stderr);
            return true;
        }
#endif
    };

}  // namespace utils
SOCKETSHPP_NS_END
