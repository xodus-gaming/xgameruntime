/*
 * Xbox Game runtime Library
 *  Xodus Interopability Layer -> socket path and peer checks
 *
 * Pure functions, header-only, no Wine headers.
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

#ifndef XGAMERUNTIME_XODUS_PATH_H
#define XGAMERUNTIME_XODUS_PATH_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>

/* Copy "<dir><suffix>" into out. False if it does not fit (never truncates). */
static inline bool xodus_path_join( const char *dir, const char *suffix, char *out, size_t out_size )
{
    size_t dlen = strlen( dir ), slen = strlen( suffix );

    if (!out || dlen + slen + 1 > out_size) return false;
    memcpy( out, dir, dlen );
    memcpy( out + dlen, suffix, slen + 1 );
    return true;
}

/* macOS: the service binds "/tmp/" + "/xodus.sock", the same file as /tmp/xodus.sock. */
static inline bool xodus_default_path_darwin( char *out, size_t out_size )
{
    return xodus_path_join( "/tmp", "/xodus.sock", out, out_size );
}

/* Elsewhere: $XDG_RUNTIME_DIR/xodus.sock. Unset, empty or relative is a failure. */
static inline bool xodus_default_path_xdg( const char *xdg_runtime_dir, char *out, size_t out_size )
{
    if (!xdg_runtime_dir || xdg_runtime_dir[0] != '/') return false;
    return xodus_path_join( xdg_runtime_dir, "/xodus.sock", out, out_size );
}

static inline bool xodus_default_path( const char *xdg_runtime_dir, char *out, size_t out_size )
{
#ifdef __APPLE__
    (void)xdg_runtime_dir;
    return xodus_default_path_darwin( out, out_size );
#else
    return xodus_default_path_xdg( xdg_runtime_dir, out, out_size );
#endif
}

static inline bool xodus_peer_ok( uid_t peer_uid, uid_t euid )
{
    return peer_uid == euid;
}

#endif
