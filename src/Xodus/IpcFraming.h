/*
 * Xbox Game runtime Library
 *  Xodus Interopability Layer -> IpcFraming
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

#ifndef XGAMERUNTIME_XODUS_IPCFRAMING_H
#define XGAMERUNTIME_XODUS_IPCFRAMING_H

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Xodus
{
    constexpr uint32_t FRAME_MAGIC = 0x58445358;
    constexpr size_t FRAME_HEADER_SIZE = 8;
    constexpr size_t FRAME_MAX_PAYLOAD = 0xFFFF;

    enum FrameType : uint16_t
    {
        FRAME_PING = 1,
        FRAME_PONG = 2,
        FRAME_MSA_TOKEN_REQUEST = 3,
        FRAME_MSA_TOKEN_RESPONSE = 4,
    };

    struct Frame
    {
        uint16_t type = 0;
        std::vector<uint8_t> payload;
    };

    /* Returns header + payload, or an empty vector if len > FRAME_MAX_PAYLOAD. */
    std::vector<uint8_t> Encode( uint16_t type, const uint8_t *payload, size_t len );

    class FrameDecoder
    {
    public:
        void Feed( const uint8_t *p, size_t n );

        /* S_OK: one frame returned and consumed.
         * S_FALSE: no complete frame yet; partial data is kept.
         * HRESULT_FROM_WIN32(ERROR_INVALID_DATA): bad magic; nothing is consumed. */
        HRESULT Next( Frame &f );

        /* Bytes buffered and not yet returned as a frame. */
        size_t Pending() const { return buf.size(); }

    private:
        std::vector<uint8_t> buf;
    };
}

#endif
