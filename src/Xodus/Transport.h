/*
 * Xbox Game runtime Library
 *  Xodus Interopability Layer -> Transport
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

#ifndef XGAMERUNTIME_XODUS_TRANSPORT_H
#define XGAMERUNTIME_XODUS_TRANSPORT_H

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace Xodus
{
    /* Byte stream to the xodus service. Each call returns an HRESULT per design.md "Errors". */
    class Transport
    {
    public:
        virtual HRESULT Connect( uint32_t timeout_ms ) = 0;
        virtual HRESULT Send( const uint8_t *p, size_t n, uint32_t timeout_ms ) = 0;
        virtual HRESULT Recv( uint8_t *buf, size_t cap, size_t &got, uint32_t timeout_ms ) = 0;
        virtual void Close() = 0;
        virtual ~Transport() = default;
    };
}

#endif
