# Compression Example

Compresses HTTP responses with the library's `CompressionMiddleware`
(`<SocketsHpp/http/server/compression.h>`). The server serves a large, repetitive text
page at `/`; a client that sends `Accept-Encoding: rle` gets it run-length encoded
(`Content-Encoding: rle`), any other client gets it uncompressed.

`HttpServer` does not compress anything by itself: handlers call
`CompressionMiddleware::compressResponse()` and set `Content-Encoding` and `Vary`.

## Building

From the repository root:

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --target compression-server
```

## Running

```bash
./build/examples/08-compression/compression-server          # port 8080
./build/examples/08-compression/compression-server 9000     # another port
```

The server listens on all IPv4 interfaces. Press Ctrl+C to stop it.

## Testing

```bash
curl -si -H "Accept-Encoding: rle" http://localhost:8080/ -o /dev/null -D -
# HTTP/1.1 200 OK
# Content-Encoding: rle
# Content-Length: 376
# Content-Type: text/plain
# Vary: Accept-Encoding

curl -si http://localhost:8080/ -o /dev/null -D -      # no Content-Encoding, Content-Length: 3310
curl -si -H "Accept-Encoding: gzip" http://localhost:8080/ -o /dev/null -D -   # gzip is not registered: uncompressed
curl -si -H "Accept-Encoding: rle" http://localhost:8080/small   # below the minimum size: uncompressed
```

## Codecs

`CompressionRegistry` maps content-coding names to `CompressionStrategy` objects
(compress / decompress callbacks). The library contains **no gzip, deflate or brotli
codec**; for real clients register one built on zlib, brotli or zstd:

```cpp
CompressionRegistry::instance().registerStrategy(
    std::make_shared<CompressionStrategy>("gzip", myCompress, myDecompress));
```

For experiments, `compression_simple.h` registers a toy `rle` codec (and `identity`)
with `compression::registerSimpleCompression()`; this example uses it, which is why
the page consists of long runs of the same character. On Windows,
`compression_windows.h` registers `mszip`, `xpress` and `lzms` through the Windows
Compression API (link `cabinet`); these are not standard HTTP content-codings, so
browsers cannot decode them either.

Register codecs before `start()`: the registry is not synchronized.

## How it works

```cpp
CompressionMiddleware compression;   // text, JSON, XML and JavaScript bodies >= 1 KB
compression.setMinSize(256);

int sendCompressed(CompressionMiddleware& compression, const HttpRequest& req, HttpResponse& res,
                   std::string body, const std::string& contentType)
{
    std::string encoding;
    if (compression.compressResponse(req.get_header_value("Accept-Encoding"), contentType, body, encoding))
    {
        res.set_header("Content-Encoding", encoding);
    }
    res.set_header("Vary", "Accept-Encoding");
    res.set_content(body, contentType);
    return 200;
}
```

`compressResponse()` compresses only if the body is at least the minimum size, the
content type is compressible (not an image, video, archive, ...) and the client's
`Accept-Encoding` (q-values honoured) names a registered codec. It keeps the result
only if it is smaller, and returns the chosen encoding in its last argument.
`Vary: Accept-Encoding` tells caches that the response depends on that header.

`CompressionMiddleware::decompressRequest(contentEncoding, body)` does the reverse for
compressed request bodies, bounded by `setMaxDecompressedSize()` (the server's maximum
body size by default).
