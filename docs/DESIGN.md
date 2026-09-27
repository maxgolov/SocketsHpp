# SocketsHpp Design

This page explains how SocketsHpp is built, which established designs it follows, and
where it could go. For what is implemented see [FEATURES.md](FEATURES.md); for usage
see the [README](../README.md).

## Which design it resembles

The core is the classic **Reactor** pattern with an optional **Half-Sync/Half-Async**
split (both from Schmidt et al., *Pattern-Oriented Software Architecture, Volume 2:
Patterns for Concurrent and Networked Objects*). One event thread per server handles
sockets through epoll, kqueue or `WSAEventSelect` and dispatches callbacks. Blocking
handlers can be moved to a worker pool that hands the result back to the reactor.

- **Closest single analogy: [Mongoose](https://github.com/cesanta/mongoose).** It's an
  embeddable, single-threaded event loop with callback handlers and everything in one
  small code base.
- **The routing API** (a prefix plus a lambda that returns a status code) feels like
  [cpp-httplib](https://github.com/yhirose/cpp-httplib) or a minimal Express.
- **Runtime shape: Node.js without JavaScript.** One event loop does the I/O while a
  worker pool runs blocking work, the way libuv's thread pool does.
- **MCP layering follows the official SDKs:** JSON-RPC dispatch sits on top of separate
  transports (Streamable HTTP and STDIO).

## How the pieces fit

```
             ┌──────────────────────────── one server ────────────────────────────┐
 sockets ──► │ Reactor thread (epoll / kqueue / WSAEventSelect)                   │
             │   accept, read, parse HTTP, write, stream, timeouts                │
             │        │ dispatch                          ▲ "writable" re-arm     │
             │        ▼                                   │                       │
             │   handler runs inline ──────────────┐      │                       │
             │        or, with enableThreadPool():  │      │                       │
             │   BS::thread_pool worker ── result ──┴──────┘                       │
             └────────────────────────────────────────────────────────────────────┘
 MCPServer = JSON-RPC dispatch (methods, sessions, cancellation) on top of HttpServer
             (Streamable HTTP) or a STDIO loop.
```

- `net::utils::Reactor` owns the readiness loop and calls back into
  `SocketServer` (accept / readable / writable / closed).
- `HttpServer` is a `SocketServer` with a per-connection state machine (reading,
  processing, sending, streaming) and routing.
- With the thread pool enabled, a request is handed to a worker; the worker fills the
  response and re-arms the socket, so all socket I/O stays on the reactor thread. An
  async token per connection detects connections that closed or were reused while a
  worker was busy.
- Everything is opt-in and header-only: code that does not include MCP never pulls in
  nlohmann/json, and code that does not call `enableThreadPool()` never starts a thread
  besides the reactor.

## Is this an obsolete design?

No. The reactor is the most widely deployed architecture for network servers, not a
legacy one:

- **nginx, Redis, HAProxy and memcached** run event loops over epoll/kqueue.
- **Node.js (libuv)** is exactly "reactor plus a worker pool for blocking work".
- **Netty (Java), Python's asyncio, Rust's Tokio and Boost.Asio on Linux** are built on
  a readiness-based reactor underneath, whatever API they expose on top.

What has changed since the POSA2 book is mostly **how the code on top of the reactor is
written** (futures, coroutines) and **how many reactors run at once**, not the reactor
itself. For an embeddable server whose job is a handful of local or proxied connections
(tools, MCP, admin/diagnostic endpoints), a single reactor with callbacks is a
deliberate choice: it has no hidden threads, is easy to reason about and debug, and
compiles fast.

Its known trade-offs:

- One reactor thread per server: CPU-heavy or blocking handlers must go to the thread
  pool, or they stall every connection.
- Callbacks instead of `co_await`: multi-step asynchronous handlers are written as
  explicit state or run on the pool.
- Readiness-based I/O on Windows via `WSAEventSelect` limits a reactor to 64 sockets;
  IOCP would remove that limit but is completion-based, a different model.

## More modern patterns that could be applied later

In rough order of how well they fit a lean, embeddable library:

| Pattern | What it would give | Cost / fit |
|---------|--------------------|------------|
| **C++20 coroutines on top of the reactor** (as Boost.Asio awaitables, cppcoro, libunifex do) | Handlers written as straight-line `co_await` code instead of callbacks or pool offloading | Needs C++20 (the library targets C++17); can be added as an optional layer without changing the reactor. The most natural next step. |
| **Multi-reactor / thread-per-core** (one reactor per core with `SO_REUSEPORT`, as nginx workers, Seastar, Envoy do) | Scale across cores without locks on the hot path | Small change in principle (N reactors sharing a listening port), but only useful for high-throughput servers, which is outside this library's niche. |
| **Proactor / completion-based I/O** (Windows IOCP, Linux io_uring) | No 64-socket limit on Windows; fewer syscalls on Linux | A different I/O model and a second back end to maintain; the biggest change on this list. |
| **Senders/receivers** (`std::execution`, C++26) | A standard, composable async model shared with other libraries | Only once compilers and the standard library ship it; would be an API layer, like coroutines. |

The guiding rule stays the same whichever of these is adopted: one small reactor at
the bottom, opt-in layers on top, and nothing that forces a thread, an allocator or a
dependency on code that does not use it.

## References

- D. Schmidt, M. Stal, H. Rohnert, F. Buschmann, *Pattern-Oriented Software
  Architecture, Volume 2* (Wiley, 2000): Reactor, Proactor, Half-Sync/Half-Async,
  Leader/Followers.
- D. Schmidt, [Reactor: An Object Behavioral Pattern for Demultiplexing and
  Dispatching Handles for Synchronous Events](https://www.dre.vanderbilt.edu/~schmidt/PDF/reactor-siemens.pdf).
- [libuv design overview](https://docs.libuv.org/en/v1.x/design.html).
