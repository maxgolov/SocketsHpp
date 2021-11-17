// Copyright The OpenTelemetry Authors; Max Golovanov.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "./macros.h"

#ifndef SOCKETSHPP_NS
#define SOCKETSHPP_NS SocketsHpp
#endif

#ifndef SOCKETSHPP_NS_BEGIN
#define SOCKETSHPP_NS_BEGIN namespace SocketsHpp {
#define SOCKETSHPP_NS_END }
#endif

#ifndef SOCKETSHPP_SERVER_NS_BEGIN
#define SOCKETSHPP_SERVER_NS_BEGIN namespace SocketsHpp { namespace Server {
#define SOCKETSHPP_SERVER_NS_END } }
#endif

#ifdef HAVE_HTTP_DEBUG
#  ifdef LOG_TRACE
#    undef LOG_TRACE
#    define LOG_TRACE(x, ...) printf(x "\n", __VA_ARGS__)
#  endif
#endif
