// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SocketsHpp/config.h>
#include <SocketsHpp/http/server/http_server.h>

#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <cwctype>

SOCKETSHPP_NS_BEGIN
namespace http
{
    namespace server {

        /**
         * @brief HTTP server that can serve static files from a document root.
         *
         * Usage:
         * @code
         *   HttpFileServer server("127.0.0.1", 8080, "./public");
         *   server.route("/api/", apiHandler);         // your own routes, any order
         *   server.InitializeFileEndpoint(server);     // serve files for everything else
         *   server.start();
         * @endcode
         *
         * Requests are stripped of their query string and fragment, percent-decoded
         * (malformed escapes or NUL bytes: 400) and resolved inside the document root;
         * paths escaping the root, absolute paths and non-regular files are refused
         * (404). A path containing no '.' at all is treated as a directory and maps to
         * its index.html. The Content-Type is chosen from the (case-sensitive) file
         * extension, defaulting to text/plain. Files are read into memory in full, so
         * this is meant for small static assets.
         *
         * @note The file endpoint answers every request that reaches it (200, 400 or
         *       404), so other routes registered on "/" after it never run.
         */
        class HttpFileServer : public HttpServer
        {

        protected:
            std::filesystem::path m_documentRoot;  ///< Absolute document root directory.
            bool m_pathTraversalProtection = true; ///< Refuse paths resolving outside m_documentRoot (default on).

        public:
            /**
             * @brief Create the server and bind a listening socket on all IPv4
             *        interfaces (port 0 = ephemeral, see getListeningPort()). Requests
             *        are served after start(); files are served only once
             *        InitializeFileEndpoint() has been called.
             * @note @p host is not a bind address: it only forms the "Server" header
             *       ("host:port"). Use addListeningPort(host, port) to bind a specific
             *       address in addition.
             * @param host Name used in the "Server" response header
             * @param port Port to listen on (all IPv4 interfaces)
             * @param docRoot Directory to serve files from (default: current directory);
             *        made absolute against the current working directory now.
             * @throws std::runtime_error if the port cannot be bound or listened on.
             */
            HttpFileServer(const std::string& host = "127.0.0.1", int port = 3333, const std::string& docRoot = ".")
                : HttpServer()
                , m_documentRoot(std::filesystem::absolute(docRoot))
            {
                std::ostringstream os;
                os << host << ":" << port;
                setServerName(os.str());
                addListeningPort(port);
            };

            /// @brief Stop the server (joining the reactor) before the file endpoint is destroyed.
            virtual ~HttpFileServer()
            {
                // Stop the reactor before ServeFile/mime_types_ are destroyed:
                // ~HttpServer runs only after this class's members are gone.
                stop();
            }

            /**
             * @brief Set the document root directory for serving files
             * @param docRoot Path to document root; a relative path is made absolute
             *        against the current working directory now.
             * @note Not synchronized with request handling: call before start().
             */
            void setDocumentRoot(const std::string& docRoot)
            {
                m_documentRoot = std::filesystem::absolute(docRoot);
            }

            /**
             * @brief Enable or disable path traversal protection
             * @param enabled If true, prevent access to files outside document root
             * @warning Disabling allows ".." segments and symlinks to reach any regular
             *          file the process can read. Call before start().
             */
            void setPathTraversalProtection(bool enabled)
            {
                m_pathTraversalProtection = enabled;
            }

            /**
             * @brief Serve static files for every request not claimed by a more specific
             *        route. Routes are matched longest-prefix first, so this "/" endpoint
             *        can be registered before or after your own routes.
             * @param server should be this object (the route is added to @p server but
             *        uses this object's handler and document root)
             * @note Call once, before start().
             */
            void InitializeFileEndpoint(HttpFileServer& server) { server[root_endpt_] = ServeFile; }

        protected:
            /**
             * @brief Decode a percent-encoded URI path (no '+' => ' ' translation).
             * @param encoded Raw path from the request target (query already removed)
             * @param decoded Output
             * @return false on malformed escapes or if the result contains a NUL byte
             */
            static bool decodeUriPath(const std::string& encoded, std::string& decoded)
            {
                decoded.clear();
                decoded.reserve(encoded.size());
                auto hexValue = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                for (size_t i = 0; i < encoded.size(); ++i)
                {
                    char c = encoded[i];
                    if (c == '%')
                    {
                        if (i + 2 >= encoded.size())
                        {
                            return false;
                        }
                        int hi = hexValue(encoded[i + 1]);
                        int lo = hexValue(encoded[i + 2]);
                        if (hi < 0 || lo < 0)
                        {
                            return false;
                        }
                        c = static_cast<char>((hi << 4) | lo);
                        i += 2;
                    }
                    if (c == '\0')
                    {
                        return false;
                    }
                    decoded += c;
                }
                return true;
            }

            /**
             * @brief Component-wise check that @p path lies inside @p root.
             *
             * Both paths must already be absolute and normalized. A plain string
             * prefix test is wrong ("/srv/www" would contain "/srv/www-private");
             * comparing path elements is not. Comparison is case-sensitive except
             * on Windows, whose file systems are case-insensitive.
             */
            static bool isPathWithinRoot(const std::filesystem::path& path, const std::filesystem::path& root)
            {
                auto rootIt = root.begin();
                auto rootEnd = root.end();
                auto pathIt = path.begin();
                auto pathEnd = path.end();
                for (; rootIt != rootEnd; ++rootIt, ++pathIt)
                {
                    if (rootIt->empty())
                    {
                        // Trailing separator on the root ("/srv/www/") yields an empty
                        // final element; it must not require a matching element.
                        auto next = rootIt;
                        if (++next == rootEnd)
                        {
                            break;
                        }
                    }
                    if (pathIt == pathEnd)
                    {
                        return false;
                    }
#ifdef _WIN32
                    const std::wstring a = rootIt->wstring();
                    const std::wstring b = pathIt->wstring();
                    if (a.size() != b.size() ||
                        !std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) {
                            return ::towlower(x) == ::towlower(y);
                        }))
                    {
                        return false;
                    }
#else
                    if (rootIt->native() != pathIt->native())
                    {
                        return false;
                    }
#endif
                }
                return true;
            }

            /**
             * @brief Validate and normalize file path with security checks
             * @param requestedPath Decoded path requested by client, relative to the document root
             * @param resolvedPath Output parameter for the validated absolute path
             * @return true if path names a regular file inside the document root
             */
            bool validateFilePath(const std::string& requestedPath, std::filesystem::path& resolvedPath)
            {
                try
                {
                    if (requestedPath.find('\0') != std::string::npos)
                    {
                        return false;
                    }

                    // Always resolve relative to the document root: drop leading
                    // separators so "//etc/passwd" cannot become an absolute path.
                    std::string relative = requestedPath;
                    relative.erase(0, relative.find_first_not_of("/\\"));
                    std::filesystem::path requested(relative);
                    if (requested.has_root_name() || requested.has_root_directory())
                    {
                        return false;  // e.g. "C:/..." on Windows
                    }

                    // Resolve ".", ".." and symlinks
                    auto docRootCanonical = std::filesystem::weakly_canonical(m_documentRoot).lexically_normal();
                    std::filesystem::path fullPath =
                        std::filesystem::weakly_canonical(docRootCanonical / requested).lexically_normal();

                    // Path traversal protection: ensure resolved path is within document root
                    if (m_pathTraversalProtection && !isPathWithinRoot(fullPath, docRootCanonical))
                    {
                        LOG_WARN("Path traversal attempt blocked: %s (resolved to %s, root is %s)",
                            requestedPath.c_str(), fullPath.string().c_str(), docRootCanonical.string().c_str());
                        return false;
                    }

                    // Only regular files are served (no directories, devices, FIFOs, sockets)
                    if (!std::filesystem::is_regular_file(fullPath))
                    {
                        return false;
                    }

                    resolvedPath = fullPath;
                    return true;
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    LOG_ERROR("Filesystem error validating path '%s': %s", requestedPath.c_str(), e.what());
                    return false;
                }
            }

        private:
            /**
             * @brief Read a file below the document root into memory.
             * @param fileNameUrl Decoded path relative to the document root
             * @param result Receives the whole file content
             * @return true if the path passed validateFilePath() and the file was opened
             */
            bool FileGetSuccess(const std::string& fileNameUrl, std::vector<char>& result)
            {
                std::filesystem::path resolvedPath;
                if (!validateFilePath(fileNameUrl, resolvedPath))
                {
                    return false;
                }

                std::streampos size;
                std::ifstream file(resolvedPath, std::ios::in | std::ios::binary | std::ios::ate);
                if (file.is_open())
                {
                    size = file.tellg();
                    if (size)
                    {
                        result.resize(size);
                        file.seekg(0, std::ios::beg);
                        file.read(result.data(), size);
                    }
                    file.close();
                    return true;
                }
                return false;
            };

            /**
             * @brief Map a file name's extension (text after the last '.') to a MIME type.
             * @param filename File name or path
             * @return The MIME type, or text/plain if the extension is unknown
             */
            std::string GetMimeContentType(const std::string& filename)
            {
                std::string file_ext = filename.substr(filename.find_last_of(".") + 1);
                auto file_type = mime_types_.find(file_ext);
                return (file_type != mime_types_.end()) ? file_type->second : CONTENT_TYPE_TEXT;
            };

            /**
             * @brief Drop one trailing '/' and, if the path contains no '.', treat it as
             *        a directory and append "/index.html".
             * @param name Decoded request path
             * @return The file path to look up
             */
            std::string GetFileName(std::string name)
            {
                if (!name.empty() && name.back() == '/')
                {
                    name.pop_back();
                }
                // If filename appears to be a directory, serve the hypothetical index.html
                // file there
                if (name.find(".") == std::string::npos)
                    name += "/index.html";

                return name;
            }

            /**
             * @brief Route handler serving the file named by the request URI: 200 with
             *        the file content, 400 for a malformed path, 404 otherwise.
             */
            HttpRequestCallback ServeFile{
                [&](HttpRequest const& req, HttpResponse& resp) {
                  LOG_INFO("File: %s\n", req.uri.c_str());
                  // Strip query/fragment and percent-decode the path before lookup
                  std::string rawPath = req.uri.substr(0, req.uri.find_first_of("?#"));
                  std::string decodedPath;
                  if (!decodeUriPath(rawPath, decodedPath))
                  {
                    resp.headers[CONTENT_TYPE] = CONTENT_TYPE_TEXT;
                    resp.code = 400;
                    resp.message = HttpServer::getDefaultResponseMessage(resp.code);
                    resp.body = resp.message;
                    return 400;
                  }
                  auto f = GetFileName(decodedPath);
                  std::string filename = f;
                  filename.erase(0, filename.find_first_not_of('/'));

                  std::vector<char> content;
                  if (FileGetSuccess(filename, content))
                  {
                    resp.headers[CONTENT_TYPE] = GetMimeContentType(filename);
                    resp.body = std::string(content.data(), content.size());
                    resp.code = 200;
                    resp.message = HttpServer::getDefaultResponseMessage(resp.code);
                    return resp.code;
                  }
                  // Two additional 'special' return codes possible here:
                  // 0    - proceed to next handler
                  // -1   - immediately terminate and close connection
                  resp.headers[CONTENT_TYPE] = CONTENT_TYPE_TEXT;
                  resp.code = 404;
                  resp.message = HttpServer::getDefaultResponseMessage(resp.code);
                  resp.body = resp.message;
                  return 404;
                } };

            /// @brief Maps file extensions (lower case, matched case-sensitively) to MIME types.
            const std::unordered_map<std::string, std::string> mime_types_ = {
                // Text
                {"css", "text/css"},
                {"htm", "text/html"},
                {"html", "text/html"},
                {"txt", "text/plain"},
                {"csv", "text/csv"},
                {"xml", "text/xml"},
                // Scripts
                {"js", "text/javascript"},
                {"mjs", "text/javascript"},
                // Images
                {"png", "image/png"},
                {"jpg", "image/jpeg"},
                {"jpeg", "image/jpeg"},
                {"gif", "image/gif"},
                {"svg", "image/svg+xml"},
                {"ico", "image/x-icon"},
                {"webp", "image/webp"},
                // Data
                {"json", "application/json"},
                // Fonts
                {"woff", "font/woff"},
                {"woff2", "font/woff2"},
                {"ttf", "font/ttf"},
                {"otf", "font/otf"},
                // Media
                {"mp4", "video/mp4"},
                {"webm", "video/webm"},
                {"mp3", "audio/mpeg"},
                {"wav", "audio/wav"},
                // Documents
                {"pdf", "application/pdf"},
                {"zip", "application/zip"},
                {"tar", "application/x-tar"},
                {"gz", "application/gzip"},
            };
            const std::string root_endpt_ = "/";  ///< Route prefix used by InitializeFileEndpoint().
        };
    }
}
SOCKETSHPP_NS_END
