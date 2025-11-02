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

SOCKETSHPP_NS_BEGIN
namespace http
{
    namespace server {

        class HttpFileServer : public HttpServer
        {

        protected:
            std::filesystem::path m_documentRoot;  // Document root directory
            bool m_pathTraversalProtection = true; // Path traversal protection enabled by default

            /**
             * Construct the server by initializing the endpoint for serving static files,
             * which show up on the web if the user is on the given host:port. Static
             * files can be seen relative to the folder where the executable was ran.
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

            virtual ~HttpFileServer() = default;

            /**
             * @brief Set the document root directory for serving files
             * @param docRoot Path to document root (defaults to current directory)
             */
            void setDocumentRoot(const std::string& docRoot)
            {
                m_documentRoot = std::filesystem::absolute(docRoot);
            }

            /**
             * @brief Enable or disable path traversal protection
             * @param enabled If true, prevent access to files outside document root
             */
            void setPathTraversalProtection(bool enabled)
            {
                m_pathTraversalProtection = enabled;
            }

            /**
             * Set the HTTP server to serve static files from the root of host:port.
             * Derived HTTP servers should initialize the file endpoint AFTER they
             * initialize their own, otherwise everything will be served like a file
             * @param server should be an instance of this object
             */
            void InitializeFileEndpoint(HttpFileServer& server) { server[root_endpt_] = ServeFile; }

        private:
            /**
             * @brief Validate and normalize file path with security checks
             * @param requestedPath Path requested by client
             * @param resolvedPath Output parameter for the validated absolute path
             * @return true if path is valid and safe, false otherwise
             */
            bool validateFilePath(const std::string& requestedPath, std::filesystem::path& resolvedPath)
            {
                try
                {
                    // Construct full path relative to document root
                    std::filesystem::path fullPath = m_documentRoot / requestedPath;

                    // Normalize path (resolve . and ..)
                    fullPath = std::filesystem::weakly_canonical(fullPath);

                    // Path traversal protection: ensure resolved path is within document root
                    if (m_pathTraversalProtection)
                    {
                        auto docRootCanonical = std::filesystem::weakly_canonical(m_documentRoot);
                        auto pathStr = fullPath.string();
                        auto rootStr = docRootCanonical.string();

                        // Check if fullPath starts with document root
                        bool isWithinRoot = pathStr.size() >= rootStr.size() &&
                            std::equal(rootStr.begin(), rootStr.end(), pathStr.begin(),
                                [](char a, char b) {
                                    return std::tolower(static_cast<unsigned char>(a)) ==
                                           std::tolower(static_cast<unsigned char>(b));
                                });

                        if (!isWithinRoot)
                        {
                            LOG_WARN("Path traversal attempt blocked: %s (resolved to %s, root is %s)",
                                requestedPath.c_str(), fullPath.string().c_str(), docRootCanonical.string().c_str());
                            return false;
                        }
                    }

                    // Check if path exists and is a regular file
                    if (!std::filesystem::exists(fullPath))
                    {
                        return false;
                    }

                    // Don't serve directories directly (unless looking for index.html)
                    if (std::filesystem::is_directory(fullPath))
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

            /**
             * Return whether a file is found whose location is searched for relative to
             * the document root. If the file is valid, fill result with
             * the file data/information required to display it on a webpage
             * @param name of the file to look for,
             * @param resulting file information, necessary for displaying them on a
             * webpage
             * @returns whether a file was found and result filled with display
             * information
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
             * Returns the extension of a file
             * @param name of the file
             * @returns file extension type under HTTP protocol
             */
            std::string GetMimeContentType(const std::string& filename)
            {
                std::string file_ext = filename.substr(filename.find_last_of(".") + 1);
                auto file_type = mime_types_.find(file_ext);
                return (file_type != mime_types_.end()) ? file_type->second : CONTENT_TYPE_TEXT;
            };

            /**
             * Returns the standardized name of a file by removing backslashes, and
             * assuming index.html is the wanted file if a directory is given
             * @param name of the file
             */
            std::string GetFileName(std::string name)
            {
                if (name.back() == '/')
                {
                    auto temp = name.substr(0, name.size() - 1);
                    name = temp;
                }
                // If filename appears to be a directory, serve the hypothetical index.html
                // file there
                if (name.find(".") == std::string::npos)
                    name += "/index.html";

                return name;
            }

            /**
             * Sets the response object with the correct file data based on the requested
             * file address, or return 404 error if a file isn't found
             * @param req is the HTTP request, which we use to figure out the response to
             * send
             * @param resp is the HTTP response we want to send to the frontend, including
             * file data
             */
            HttpRequestCallback ServeFile{
                [&](HttpRequest const& req, HttpResponse& resp) {
                  LOG_INFO("File: %s\n", req.uri.c_str());
                  auto f = GetFileName(req.uri);
                  auto filename = f.c_str() + 1;

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

            // Maps file extensions to their HTTP-compatible mime file type
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
            const std::string root_endpt_ = "/";
        };
    }
}
SOCKETSHPP_NS_END
