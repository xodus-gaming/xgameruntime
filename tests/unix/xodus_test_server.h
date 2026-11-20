/*
 * Xbox Game runtime Library
 *  Xodus Interopability Layer -> test server unix-call ABI
 *
 * Shared by xodus_test_server.c (host) and the Wine tests. No Wine headers.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef XGAMERUNTIME_XODUS_TEST_SERVER_H
#define XGAMERUNTIME_XODUS_TEST_SERVER_H

#include <stdint.h>

/* __wine_unix_call_funcs entry indices of xodus_test_server.so. */
enum { XTS_CALL_START = 0, XTS_CALL_STATS = 1, XTS_CALL_STOP = 2 };

/* What the server does with a connection. */
enum
{
    XTS_MODE_SERVE = 0,   /* PING (1) -> PONG (2) with the same body; MSA request (3) -> MSA response (4) = resp */
    XTS_MODE_SILENT = 1,  /* accept, read, never reply */
    XTS_MODE_CLOSE = 2,   /* accept, then close at once */
};

/* in: path (UTF-8), mode, resp/resp_len (MSA response payload; copied). out: status (0 = ok), err. */
struct xts_start_args { const char *path; int32_t mode; const uint8_t *resp; uint32_t resp_len; int32_t status; int32_t err; };

/* out: connections accepted, bytes received from clients, frames answered. */
struct xts_stats_args { uint32_t accepted; uint32_t frames_answered; uint64_t bytes_received; };

struct xts_stop_args { int32_t status; };

#endif
