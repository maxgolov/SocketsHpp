# HTTP Server with Compression

This example demonstrates automatic content compression with Accept-Encoding negotiation.

## Features

- Compression registry (pluggable algorithms)
- Accept-Encoding header parsing
- Quality value negotiation (q=0.8)
- Content-Type based compression
- Minimum size threshold
- Platform-specific compression (Windows API on Windows)
- Test compression (SimpleRLE, Identity)

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

On Windows, this will use the Windows Compression API (MSZIP, XPRESS, LZMS).

## Running

```bash
./compression-server
```

The server will listen on `http://localhost:8080`.

## Testing

### Without compression
```bash
curl http://localhost:8080/
```

### With RLE compression
```bash
curl -H "Accept-Encoding: rle" http://localhost:8080/
```

### Windows compression (Windows only)
```bash
curl -H "Accept-Encoding: mszip" http://localhost:8080/
curl -H "Accept-Encoding: xpress" http://localhost:8080/
```

### JSON API with compression
```bash
curl -H "Accept-Encoding: rle" http://localhost:8080/api/data
```

### Small response (not compressed due to threshold)
```bash
curl http://localhost:8080/small
```

## Supported Algorithms

**All platforms:**
- `rle` - Run-Length Encoding (simple test algorithm)
- `identity` - No compression (passthrough)

**Windows only:**
- `mszip` - Microsoft DEFLATE variant
- `xpress` - Fast LZ77-based compression
- `xpress-huff` - XPRESS with Huffman encoding
- `lzms` - High-ratio Lempel-Ziv-Markov compression

## Adding Custom Compression

You can register your own compression algorithms (zlib, brotli, zstd, etc.):

```cpp
auto gzipStrategy = std::make_shared<CompressionStrategy>(
    "gzip",
    [](const std::vector<uint8_t>& input, int level) {
        // Your gzip compression implementation
        return compressed;
    },
    [](const std::vector<uint8_t>& input) {
        // Your gzip decompression implementation
        return decompressed;
    }
);

CompressionRegistry::instance().registerStrategy(gzipStrategy);
```
