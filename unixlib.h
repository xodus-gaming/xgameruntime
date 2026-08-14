/*
 * Xbox Game runtime Library
 *  Unix side: talking to xodus-service
 *
 * Copyright 2026 Olivia Ryan
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

#ifndef __WINE_XGAMERUNTIME_UNIXLIB_H
#define __WINE_XGAMERUNTIME_UNIXLIB_H

#include <windef.h>
#include <winternl.h>
#include <wine/unixlib.h>

/*
 * xodus-service holds the signed-in account: it owns the keyring entries and
 * does the MSA/Xbox Live round trips. It listens on a mode-0600 AF_UNIX socket
 * at $XDG_RUNTIME_DIR/xodus.sock.
 *
 * That socket cannot be reached from the PE side. Wine's ws2_32 only maps the
 * INET families onto host sockets (server/sock.c), so AF_UNIX is simply not
 * available to a Windows-side caller, and moving the service to TCP loopback
 * would hand every local process the user's Xbox tokens. Hence a unixlib: the
 * PE side builds and parses the message, and this is only the transport.
 *
 * Wire format, little-endian throughout:
 *
 *     u32 magic ("XSDX")  u16 message_type  u16 body_size  body
 *
 * The service replies with the same framing and message_type + 1, which is why
 * request types are odd. An empty body means the service failed to handle the
 * message; its own log has the reason.
 */

#define XODUS_XML_MAGIC        0x58445358
#define XODUS_MSG_PING         1
#define XODUS_MSG_MSA_TOKEN    3
#define XODUS_MSG_XSTS_TOKEN   5

/* The PE side includes winerror.h, so it cannot also pull in ntstatus.h; name
 * the one status it has to tell apart. */
#define XODUS_STATUS_NO_SERVICE  ((NTSTATUS)0xC0000236)  /* STATUS_CONNECTION_REFUSED */
#define XODUS_STATUS_MORE_ROOM   ((NTSTATUS)0xC0000023)  /* STATUS_BUFFER_TOO_SMALL */

struct service_call_params
{
    UINT16 message_type;
    const char *request;    /* UTF-8 XML, NUL-terminated */
    char *reply;            /* caller-allocated, NUL-terminated on return */
    UINT32 reply_size;      /* in: capacity in bytes; out: bytes written */
};

enum xgameruntime_funcs
{
    unix_service_call,
    unix_funcs_count
};

#endif /* __WINE_XGAMERUNTIME_UNIXLIB_H */
