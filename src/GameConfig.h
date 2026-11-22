/*
 * Xbox Game runtime Library
 *  MicrosoftGame.config reader
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

#ifndef XGAMERUNTIME_GAMECONFIG_H
#define XGAMERUNTIME_GAMECONFIG_H

#include <windows.h>

#include <cstddef>
#include <string>

namespace GameConfig
{
    /* Parses the TitleId (hex) out of MicrosoftGame.config XML. */
    HRESULT ParseTitleId( const char *xml, size_t len, UINT32 *titleId );

    /* Parses the MSAAppId (plain string, surrounding whitespace trimmed) out of
     * MicrosoftGame.config XML. Missing or empty is E_GAMERUNTIME_GAMECONFIG_BAD_FORMAT;
     * appId is untouched on failure. */
    HRESULT ParseMsaAppId( const char *xml, size_t len, std::string &appId );

    /* Looks for MicrosoftGame.config in startDir and each parent directory.
     * E_GAME_MISSING_GAME_CONFIG when there is none. */
    HRESULT FindConfigFile( const std::wstring &startDir, std::wstring &path );

    /* Selects where the config comes from. config null = default lookup from
     * the executable's directory; otherwise it is the XML itself (isInline) or
     * a UTF-8 file path. Always clears the cached result. */
    void SetSource( const char *config, bool isInline );

    /* Reads and parses the config on first use, then returns the cached value. */
    HRESULT GetTitleId( UINT32 *titleId );

    /* Same source, cache and error semantics as GetTitleId; appId is untouched on failure. */
    HRESULT GetMsaAppId( std::string &appId );
}

#endif
