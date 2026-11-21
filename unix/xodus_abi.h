/*
 * Xbox Game runtime Library
 *  Xodus Interopability Layer -> unix call ABI
 *
 * Shared by xgameruntime-unix.so, the test server, UnixTransport and the
 * host tests. No Wine headers; fixed-width types only.
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

#ifndef XGAMERUNTIME_XODUS_ABI_H
#define XGAMERUNTIME_XODUS_ABI_H

#include <stdint.h>

/* __wine_unix_call_funcs entry indices. */
enum { XODUS_CALL_CONNECT = 0, XODUS_CALL_SEND = 1, XODUS_CALL_RECV = 2, XODUS_CALL_CLOSE = 3 };

/* One recv call returns at most one maximum frame: 8 + 0xFFFF. */
#define XODUS_RECV_MAX 0x10007u

enum { XODUS_OK = 0, XODUS_TIMEOUT = 1, XODUS_PEER_CLOSED = 2, XODUS_ERROR = 3 };

struct xodus_connect_args { const char *path; uint32_t timeout_ms; int64_t expected_uid; int32_t status; int32_t err; };
struct xodus_send_args    { const uint8_t *buf; uint32_t len; uint32_t timeout_ms; int32_t status; int32_t err; };
struct xodus_recv_args    { uint8_t *buf; uint32_t cap; uint32_t timeout_ms; uint32_t got; int32_t status; int32_t err; };
struct xodus_close_args   { int32_t status; int32_t err; };

#endif
