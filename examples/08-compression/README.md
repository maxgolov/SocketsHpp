# Compression Example (not wired in yet)

This example currently serves a large HTML page (about 3.5 KB of repeated paragraphs)
**without compressing it**: `main.cpp` only uses `HttpServer` and prints a note that
compression integration is not done. The rest of this README explains how to add
compression with the library's `compression.h`.

## Building

From the repository root:

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target compression-server
```

## Running

```bash
./build/examples/08-compression/compression-server
curl -i http://localhost:8080/
```

The server listens on port 8080 on all IPv4 interfaces and answers every path with the
same page, uncompressed.

## Adding compression

`SocketsHpp/http/server/compression.h` provides:

- `CompressionRegistry`: a process-wide map from content-coding name to
  `CompressionStrategy` (compress and decompress callbacks);
- `CompressionMiddleware`: parses `Accept-Encoding` (with q-values), skips small bodies
  (< 1 KB by default) and non-text content types, and compresses with the best
  registered codec, keeping the result only if it is smaller.

The library contains **no gzip/deflate/brotli codec**. For real clients register one
built on zlib, brotli or zstd. For experiments, `compression_simple.h` registers a
toy `rle` codec (and `identity`); on Windows, `compression_windows.h` registers
`mszip`, `xpress` and `lzms` through the Windows Compression API (link `cabinet`;
these are not standard HTTP content-codings, so browsers cannot decode them).

A route that compresses its response:

```cpp
#include <SocketsHpp/http/server/compression.h>
#include <SocketsHpp/http/server/compression_simple.h>
#include <SocketsHpp/http/server/http_server.h>
#include <chrono>
#include <thread>

using namespace SocketsHpp::http::server;

int main()
{
    compression::registerSimpleCompression();  // "rle" + "identity"; register real codecs here

    // Registering your own codec:
    // CompressionRegistry::instance().registerStrategy(
    //     std::make_shared<CompressionStrategy>("gzip", myCompress, myDecompress));

    CompressionMiddleware compression;
    compression.setMinSize(256);

    HttpServer server("compression-demo", 8080);
    server.route("/", [&compression](const HttpRequest& req, HttpResponse& res) -> int {
        std::string body(4096, 'a');
        std::string encoding;
        if (compression.compressResponse(req.get_header_value("Accept-Encoding"),
                                         "text/plain", body, encoding))
        {
            res.set_header("Content-Encoding", encoding);
        }
        res.set_header("Vary", "Accept-Encoding");
        res.set_content(body, "text/plain");
        return 200;
    });

    server.start();
    std::this_thread::sleep_for(std::chrono::minutes(5));
}
```

```bash
curl -i -H "Accept-Encoding: rle" http://localhost:8080/   # Content-Encoding: rle
curl -i http://localhost:8080/                             # uncompressed
```

Register codecs before `start()`: the registry is not synchronized.
`CompressionMiddleware::decompressRequest(contentEncoding, body)` does the reverse for
compressed request bodies, bounded by `setMaxDecompressedSize()` (2 MB by default).
