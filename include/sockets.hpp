// Copyright Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "SocketsHpp/config.h"

// Socket Tools and common Socket Server
#include "SocketsHpp/net/common/socket_tools.h"
#include "SocketsHpp/net/common/socket_server.h"

// HTTP server and client
#include "SocketsHpp/http/server/http_server.h"
#include "SocketsHpp/http/server/http_file_server.h"
#include "SocketsHpp/http/client/http_client.h"

// NOTE: MCP headers intentionally excluded - they have compile errors
// Users should include them explicitly if needed:
// #include "SocketsHpp/mcp/common/mcp_config.h"
// #include "SocketsHpp/mcp/server/mcp_server.h"
// #include "SocketsHpp/mcp/client/mcp_client.h"

// Utilities
#include "SocketsHpp/utils/base64.h"
