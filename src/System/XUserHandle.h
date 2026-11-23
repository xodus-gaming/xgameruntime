/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XUser
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

#ifndef XGAMERUNTIME_XUSER_HANDLE_H
#define XGAMERUNTIME_XUSER_HANDLE_H

#include <windows.h>
#include <atomic>
#include <xuser.h>

struct XUser
{
    UINT32 signature;
    std::atomic<LONG> refs;
    XUserLocalId localId;
};

constexpr UINT32 XUSER_SIGNATURE = 0x52535558; // 'XUSR'

inline bool UserIsValid( XUserHandle h )
{
    return h && h->signature == XUSER_SIGNATURE && h->refs.load() > 0;
}

inline LONG UserAddRef( XUserHandle h )
{
    return h->refs.fetch_add( 1 ) + 1;
}

inline LONG UserRelease( XUserHandle h )
{
    LONG n = h->refs.fetch_sub( 1 ) - 1;
    if (!n)
    {
        h->signature = 0;
        delete h;
    }
    return n;
}

inline INT32 UserCompare( XUserHandle a, XUserHandle b )
{
    if (a == b) return 0;

    bool a_valid = UserIsValid( a ), b_valid = UserIsValid( b );
    if (!a_valid && !b_valid) return 0;
    if (!a_valid) return -1;
    if (!b_valid) return 1;

    if (a->localId.value == b->localId.value) return 0;
    return a->localId.value < b->localId.value ? -1 : 1;
}

#endif
