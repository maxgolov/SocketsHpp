// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file multipart.h
/// @brief multipart/form-data parser (RFC 7578 / RFC 2046) for HttpRequest bodies.
///
/// @code
///   server.route("/upload", [](const HttpRequest& req, HttpResponse& res) {
///       auto form = multipart::parse(req);           // boundary from Content-Type
///       if (!form)
///       {
///           res.set_content(multipart::errorMessage(form.error));
///           return 400;
///       }
///       for (const auto& part : form.parts)
///       {
///           if (part.isFile())
///           {
///               save(part.filename, part.data);       // binary-safe string_view
///           }
///       }
///       return 204;
///   });
/// @endcode
///
/// The parser works on a complete, already received body (HttpServer buffers request
/// bodies up to HttpServer::setMaxRequestContentSize()). It never throws on malformed
/// input: it returns an Error. Part bodies are std::string_view slices of the parsed
/// buffer - no copies - so they are only valid while that buffer (e.g.
/// HttpRequest::content) is alive and unchanged.

#include <SocketsHpp/config.h>
#include <SocketsHpp/http/server/http_server.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

SOCKETSHPP_NS_BEGIN
namespace http
{
    namespace server
    {
        /// @brief multipart/form-data parsing (parse(), boundaryFromContentType()).
        namespace multipart
        {
            /// @brief Limits applied while parsing (defaults suit typical HTML forms).
            struct Limits
            {
                size_t maxParts = 100;           ///< More parts give Error::TooManyParts.
                size_t maxHeaderSize = 8192;     ///< Maximum header block of one part, in bytes.
                size_t maxHeadersPerPart = 32;   ///< Maximum header fields of one part.
            };

            /// @brief Why parsing failed.
            enum class Error
            {
                None,                ///< Success.
                NotMultipart,        ///< Content-Type is missing or not a multipart type.
                MissingBoundary,     ///< Content-Type has no boundary parameter.
                InvalidBoundary,     ///< Boundary is empty, longer than 70 bytes or contains CR/LF/NUL.
                NoOpeningDelimiter,  ///< The body contains no "--boundary" line.
                MalformedDelimiter,  ///< A delimiter line is followed by something other than CRLF or "--".
                HeaderTooLarge,      ///< A part's header block exceeds Limits::maxHeaderSize.
                TooManyHeaders,      ///< A part has more than Limits::maxHeadersPerPart fields.
                MalformedHeader,     ///< A part header line is not "Name: value".
                TooManyParts,        ///< More than Limits::maxParts parts.
                Truncated            ///< The body ends before the closing "--boundary--".
            };

            /// @brief Human-readable description of @p error.
            /// @param error Error code.
            /// @return Static string.
            inline const char* errorMessage(Error error) noexcept
            {
                switch (error)
                {
                case Error::None:
                    return "ok";
                case Error::NotMultipart:
                    return "Content-Type is not multipart";
                case Error::MissingBoundary:
                    return "multipart boundary missing";
                case Error::InvalidBoundary:
                    return "invalid multipart boundary";
                case Error::NoOpeningDelimiter:
                    return "multipart body has no opening boundary";
                case Error::MalformedDelimiter:
                    return "malformed multipart boundary line";
                case Error::HeaderTooLarge:
                    return "multipart part headers too large";
                case Error::TooManyHeaders:
                    return "too many multipart part headers";
                case Error::MalformedHeader:
                    return "malformed multipart part header";
                case Error::TooManyParts:
                    return "too many multipart parts";
                case Error::Truncated:
                    return "multipart body is truncated";
                }
                return "unknown multipart error";
            }

            /// @brief One part of a multipart body.
            struct Part
            {
                /// @brief Part header fields keyed by normalized name (see
                ///        HttpRequest::normalize_header_name()); repeated fields are joined with ", ".
                std::map<std::string, std::string> headers;
                std::string name;              ///< Content-Disposition "name" (form field name); may be empty.
                /// @brief Content-Disposition "filename" (RFC 5987 "filename*" takes
                ///        precedence and is percent-decoded). Not sanitized: never use it
                ///        as a filesystem path as is.
                std::string filename;
                bool hasFilename = false;      ///< A filename parameter was present (even if empty).
                std::string contentType;       ///< Part Content-Type; empty if absent (RFC 7578: then text/plain).
                std::string_view data;         ///< Body bytes (binary-safe view into the parsed buffer).

                /// @brief Whether this part is a file upload (has a filename parameter).
                bool isFile() const { return hasFilename; }

                /// @brief Copy of the body bytes.
                std::string body() const { return std::string(data); }

                /// @brief Header value by name (any case), or "" if absent.
                /// @param headerName Header field name.
                std::string header(const std::string& headerName) const
                {
                    auto it = headers.find(HttpRequest::normalize_header_name(headerName));
                    return (it != headers.end()) ? it->second : std::string();
                }
            };

            /// @brief Result of parse(): the parts, or an error (then parts is empty).
            struct Result
            {
                Error error = Error::None;  ///< Error::None on success.
                std::vector<Part> parts;    ///< Parts in body order.

                /// @brief true on success.
                bool ok() const { return error == Error::None; }
                /// @brief true on success.
                explicit operator bool() const { return ok(); }

                /// @brief First part whose form field name is @p fieldName, or nullptr.
                /// @param fieldName Field name.
                const Part* find(std::string_view fieldName) const
                {
                    for (const Part& part : parts)
                    {
                        if (part.name == fieldName)
                        {
                            return &part;
                        }
                    }
                    return nullptr;
                }
            };

            /// @brief Internal helpers of the multipart parser.
            namespace impl
            {
                /// @brief ASCII lower-case copy.
                inline std::string lower(std::string_view s)
                {
                    std::string out(s);
                    for (char& c : out)
                    {
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                    return out;
                }

                /// @brief @p s without leading and trailing spaces / tabs.
                inline std::string_view trim(std::string_view s)
                {
                    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
                    {
                        s.remove_prefix(1);
                    }
                    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                    {
                        s.remove_suffix(1);
                    }
                    return s;
                }

                /// @brief Split a header value "main; a=b; c=\"d\"" into the main value and
                ///        parameters (names lower-cased; quoted-strings unescaped).
                ///        Unparseable parameters are skipped.
                inline void parseParams(std::string_view value, std::string& mainValue,
                    std::vector<std::pair<std::string, std::string>>& params)
                {
                    size_t i = 0;
                    const size_t n = value.size();
                    size_t semi = value.find(';');
                    mainValue = std::string(trim(value.substr(0, semi)));
                    if (semi == std::string_view::npos)
                    {
                        return;
                    }
                    i = semi + 1;
                    while (i < n)
                    {
                        while (i < n && (value[i] == ' ' || value[i] == '\t' || value[i] == ';'))
                        {
                            ++i;
                        }
                        size_t nameStart = i;
                        while (i < n && value[i] != '=' && value[i] != ';')
                        {
                            ++i;
                        }
                        std::string name = lower(trim(value.substr(nameStart, i - nameStart)));
                        if (i >= n || value[i] != '=')
                        {
                            continue;  // parameter without value: skip
                        }
                        ++i;  // '='
                        while (i < n && (value[i] == ' ' || value[i] == '\t'))
                        {
                            ++i;
                        }
                        std::string paramValue;
                        if (i < n && value[i] == '"')
                        {
                            ++i;
                            while (i < n && value[i] != '"')
                            {
                                if (value[i] == '\\' && i + 1 < n)
                                {
                                    ++i;
                                }
                                paramValue += value[i];
                                ++i;
                            }
                            if (i < n)
                            {
                                ++i;  // closing quote
                            }
                            while (i < n && value[i] != ';')
                            {
                                ++i;  // ignore garbage after the quoted-string
                            }
                        }
                        else
                        {
                            size_t valueStart = i;
                            while (i < n && value[i] != ';')
                            {
                                ++i;
                            }
                            paramValue = std::string(trim(value.substr(valueStart, i - valueStart)));
                        }
                        if (!name.empty())
                        {
                            params.emplace_back(std::move(name), std::move(paramValue));
                        }
                    }
                }

                /// @brief Decode an RFC 5987 ext-value ("UTF-8''%E2%82%AC.txt"); returns
                ///        false if it is malformed.
                inline bool decodeExtValue(std::string_view value, std::string& out)
                {
                    size_t first = value.find('\'');
                    size_t second = (first == std::string_view::npos) ? first : value.find('\'', first + 1);
                    if (second == std::string_view::npos)
                    {
                        return false;
                    }
                    std::string_view encoded = value.substr(second + 1);
                    std::string decoded;
                    for (size_t i = 0; i < encoded.size(); ++i)
                    {
                        if (encoded[i] == '%')
                        {
                            if (i + 2 >= encoded.size() || !std::isxdigit(static_cast<unsigned char>(encoded[i + 1])) ||
                                !std::isxdigit(static_cast<unsigned char>(encoded[i + 2])))
                            {
                                return false;
                            }
                            auto hex = [](char c) {
                                return (c >= '0' && c <= '9') ? c - '0' : (std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
                            };
                            decoded += static_cast<char>(hex(encoded[i + 1]) * 16 + hex(encoded[i + 2]));
                            i += 2;
                        }
                        else
                        {
                            decoded += encoded[i];
                        }
                    }
                    out = std::move(decoded);
                    return true;
                }

                /// @brief Whether @p c may appear in a header field name (RFC 9110 tchar).
                inline bool isTokenChar(unsigned char c)
                {
                    return std::isalnum(c) || std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(c)) != std::string_view::npos;
                }

                /// @brief Parse the header block @p block of one part into @p part.
                inline Error parsePartHeaders(std::string_view block, const Limits& limits, Part& part)
                {
                    size_t count = 0;
                    std::string lastName;
                    size_t pos = 0;
                    while (pos < block.size())
                    {
                        size_t eol = block.find("\r\n", pos);
                        if (eol == std::string_view::npos)
                        {
                            eol = block.size();
                        }
                        std::string_view line = block.substr(pos, eol - pos);
                        pos = eol + 2;
                        if (line.empty())
                        {
                            continue;
                        }
                        if (line.front() == ' ' || line.front() == '\t')
                        {
                            // obs-fold continuation of the previous field
                            if (lastName.empty())
                            {
                                return Error::MalformedHeader;
                            }
                            part.headers[lastName] += " " + std::string(trim(line));
                            continue;
                        }
                        size_t colon = line.find(':');
                        if (colon == std::string_view::npos || colon == 0)
                        {
                            return Error::MalformedHeader;
                        }
                        for (size_t k = 0; k < colon; ++k)
                        {
                            if (!isTokenChar(static_cast<unsigned char>(line[k])))
                            {
                                return Error::MalformedHeader;
                            }
                        }
                        if (++count > limits.maxHeadersPerPart)
                        {
                            return Error::TooManyHeaders;
                        }
                        lastName = HttpRequest::normalize_header_name(std::string(line.substr(0, colon)));
                        std::string fieldValue(trim(line.substr(colon + 1)));
                        auto it = part.headers.find(lastName);
                        if (it == part.headers.end())
                        {
                            part.headers.emplace(lastName, std::move(fieldValue));
                        }
                        else
                        {
                            it->second += ", " + fieldValue;
                        }
                    }

                    auto disposition = part.headers.find("Content-Disposition");
                    if (disposition != part.headers.end())
                    {
                        std::string type;
                        std::vector<std::pair<std::string, std::string>> params;
                        parseParams(disposition->second, type, params);
                        bool haveExtFilename = false;
                        for (auto& param : params)
                        {
                            if (param.first == "name")
                            {
                                part.name = param.second;
                            }
                            else if (param.first == "filename" && !haveExtFilename)
                            {
                                part.filename = param.second;
                                part.hasFilename = true;
                            }
                            else if (param.first == "filename*")
                            {
                                std::string decoded;
                                if (decodeExtValue(param.second, decoded))
                                {
                                    part.filename = std::move(decoded);
                                    part.hasFilename = true;
                                    haveExtFilename = true;
                                }
                            }
                        }
                    }
                    auto contentType = part.headers.find("Content-Type");
                    if (contentType != part.headers.end())
                    {
                        part.contentType = contentType->second;
                    }
                    return Error::None;
                }
            }  // namespace impl

            /// @brief Extract the boundary parameter from a multipart Content-Type value,
            ///        e.g. `multipart/form-data; boundary="----abc"` gives "----abc".
            /// @param contentType Content-Type header value.
            /// @return The boundary, or std::nullopt if the media type is not a
            ///         multipart type, there is no boundary, or it is invalid (empty, more
            ///         than 70 bytes, or containing CR, LF or NUL).
            inline std::optional<std::string> boundaryFromContentType(std::string_view contentType)
            {
                std::string mediaType;
                std::vector<std::pair<std::string, std::string>> params;
                impl::parseParams(contentType, mediaType, params);
                if (impl::lower(mediaType).compare(0, 10, "multipart/") != 0)
                {
                    return std::nullopt;
                }
                for (auto& param : params)
                {
                    if (param.first == "boundary")
                    {
                        const std::string& b = param.second;
                        if (b.empty() || b.size() > 70 || b.find_first_of(std::string("\r\n\0", 3)) != std::string::npos)
                        {
                            return std::nullopt;
                        }
                        return b;
                    }
                }
                return std::nullopt;
            }

            /// @brief Parse a multipart body delimited by @p boundary.
            ///
            /// Follows RFC 2046 section 5.1.1: the preamble before the first
            /// "--boundary" line and the epilogue after the closing "--boundary--" are
            /// ignored; transport padding (spaces / tabs) after a delimiter is allowed;
            /// line breaks must be CRLF except that a bare LF is tolerated right after a
            /// delimiter. Part bodies are binary-safe views into @p body.
            /// @param body Complete multipart body (e.g. HttpRequest::content); must outlive
            ///        the result's Part::data views.
            /// @param boundary Boundary without the leading "--" (see boundaryFromContentType()).
            /// @param limits Parsing limits.
            /// @return Parts, or an error with no parts. Never throws for malformed input
            ///         (only std::bad_alloc is possible).
            inline Result parse(std::string_view body, std::string_view boundary, const Limits& limits = Limits())
            {
                Result result;
                if (boundary.empty() || boundary.size() > 70 ||
                    boundary.find_first_of(std::string_view("\r\n\0", 3)) != std::string_view::npos)
                {
                    result.error = Error::InvalidBoundary;
                    return result;
                }
                const std::string delimiter = "--" + std::string(boundary);
                const std::string crlfDelimiter = "\r\n" + delimiter;

                size_t pos;
                if (body.substr(0, delimiter.size()) == delimiter)
                {
                    pos = delimiter.size();
                }
                else
                {
                    size_t found = body.find(crlfDelimiter);
                    if (found == std::string_view::npos)
                    {
                        result.error = Error::NoOpeningDelimiter;
                        return result;
                    }
                    pos = found + crlfDelimiter.size();
                }

                auto fail = [&result](Error error) {
                    result.parts.clear();
                    result.error = error;
                    return result;
                };

                for (;;)
                {
                    // pos is just past a "--boundary" delimiter.
                    if (body.substr(pos, 2) == "--")
                    {
                        return result;  // close delimiter; the epilogue is ignored
                    }
                    while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t'))
                    {
                        ++pos;  // transport padding
                    }
                    if (pos >= body.size())
                    {
                        return fail(Error::Truncated);
                    }
                    if (body.substr(pos, 2) == "\r\n")
                    {
                        pos += 2;
                    }
                    else if (body[pos] == '\n')
                    {
                        pos += 1;
                    }
                    else if (body[pos] == '\r' && pos + 1 >= body.size())
                    {
                        return fail(Error::Truncated);
                    }
                    else
                    {
                        return fail(Error::MalformedDelimiter);
                    }

                    if (result.parts.size() >= limits.maxParts)
                    {
                        return fail(Error::TooManyParts);
                    }

                    Part part;
                    size_t bodyStart;
                    if (body.substr(pos, 2) == "\r\n")
                    {
                        bodyStart = pos + 2;  // no part headers
                    }
                    else
                    {
                        const size_t window = (std::min)(body.size() - pos, limits.maxHeaderSize + 4);
                        size_t headersEnd = body.substr(pos, window).find("\r\n\r\n");
                        if (headersEnd == std::string_view::npos)
                        {
                            return fail((body.size() - pos > limits.maxHeaderSize) ? Error::HeaderTooLarge
                                                                                   : Error::Truncated);
                        }
                        if (headersEnd > limits.maxHeaderSize)
                        {
                            return fail(Error::HeaderTooLarge);
                        }
                        Error headerError = impl::parsePartHeaders(body.substr(pos, headersEnd), limits, part);
                        if (headerError != Error::None)
                        {
                            return fail(headerError);
                        }
                        bodyStart = pos + headersEnd + 4;
                    }

                    size_t next = body.find(crlfDelimiter, bodyStart);
                    if (next == std::string_view::npos)
                    {
                        return fail(Error::Truncated);
                    }
                    part.data = body.substr(bodyStart, next - bodyStart);
                    result.parts.push_back(std::move(part));
                    pos = next + crlfDelimiter.size();
                }
            }

            /// @brief Parse a request's multipart body, taking the boundary from its
            ///        Content-Type header.
            /// @param request Request whose content is parsed; Part::data views point into
            ///        request.content, so the request must outlive the result.
            /// @param limits Parsing limits.
            /// @return Parts, or Error::NotMultipart / MissingBoundary / InvalidBoundary /
            ///         a parse error.
            inline Result parse(const HttpRequest& request, const Limits& limits = Limits())
            {
                Result result;
                const std::string contentType = request.get_header_value("Content-Type");
                std::string mediaType;
                std::vector<std::pair<std::string, std::string>> params;
                impl::parseParams(contentType, mediaType, params);
                if (impl::lower(mediaType).compare(0, 10, "multipart/") != 0)
                {
                    result.error = Error::NotMultipart;
                    return result;
                }
                bool hasBoundaryParam = false;
                for (auto const& param : params)
                {
                    hasBoundaryParam |= (param.first == "boundary");
                }
                if (!hasBoundaryParam)
                {
                    result.error = Error::MissingBoundary;
                    return result;
                }
                auto boundary = boundaryFromContentType(contentType);
                if (!boundary)
                {
                    result.error = Error::InvalidBoundary;
                    return result;
                }
                return parse(std::string_view(request.content), *boundary, limits);
            }
        }  // namespace multipart
    }  // namespace server
}  // namespace http
SOCKETSHPP_NS_END
