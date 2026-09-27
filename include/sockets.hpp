// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file sockets.hpp
/// @brief Umbrella header: includes the socket primitives, generic socket server,
///        HTTP server / file server / client, MCP server and client, and base64 utilities.
/// @note Not included here: net/tcp/tcp.h and net/server/thread_pool_server.h. JWT
///       support in the MCP server additionally requires SOCKETSHPP_HAS_JWT_CPP.

#include "SocketsHpp/config.h"

// Socket Tools and common Socket Server
#include "SocketsHpp/net/common/socket_tools.h"
#include "SocketsHpp/net/common/socket_server.h"

// HTTP server and client
#include "SocketsHpp/http/server/http_server.h"
#include "SocketsHpp/http/server/http_file_server.h"
#include "SocketsHpp/http/client/http_client.h"

// MCP (Model Context Protocol) server and client
#include "SocketsHpp/mcp/common/mcp_config.h"
#include "SocketsHpp/mcp/server/mcp_server.h"
#include "SocketsHpp/mcp/client/mcp_client.h"

// Utilities
#include "SocketsHpp/utils/base64.h"
