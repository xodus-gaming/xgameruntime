/*
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

#include "private.h"
#include "util.h"
#include <winhttp.h>

WINE_DEFAULT_DEBUG_CHANNEL(xgameruntime);

static const WCHAR USER_AGENT[] = L"curl/1.0";

static HRESULT http_request_once( const WCHAR *method, const WCHAR *url, char *data, const WCHAR *headers, const WCHAR **accept, UCHAR **buffer, SIZE_T *bufferSize );

HRESULT http_request( const WCHAR *method, const WCHAR *url, char *data, const WCHAR *headers, const WCHAR **accept, UCHAR **buffer, SIZE_T *bufferSize )
{
    HRESULT hr = E_FAIL;
    for (int attempt = 0; attempt < 3; attempt++)
    {
        if (attempt)
        {
            WARN( "request %s %s failed with hr %#lx, retrying.\n", debugstr_w( method ), debugstr_w( url ), hr );
            Sleep( 500 * attempt );
        }
        *buffer = NULL;
        *bufferSize = 0;
        if (SUCCEEDED(hr = http_request_once( method, url, data, headers, accept, buffer, bufferSize ))) break;
        if (hr == E_FAIL) break;
    }
    return hr;
}

static HRESULT http_request_once( const WCHAR *method, const WCHAR *url, char *data, const WCHAR *headers, const WCHAR **accept, UCHAR **buffer, SIZE_T *bufferSize )
{
    URL_COMPONENTS uc = { .dwStructSize = sizeof(URL_COMPONENTS), .dwHostNameLength = -1, .dwUrlPathLength = -1 };
    HINTERNET connection = NULL, request = NULL, session = NULL;
    DWORD size = sizeof( DWORD ), status;
    WCHAR *hostName = NULL;
    UCHAR *tmpBuffer = NULL;
    HRESULT hr = S_OK;

    TRACE( "method %s, url %s, data %s, headers %s, accept %p, buffer %p, bufferSize %p.\n",
           debugstr_w( method ), debugstr_w( url ), debugstr_a( data ), debugstr_w( headers ), accept, buffer, bufferSize );

    if (!WinHttpCrackUrl( url, 0, 0, &uc )) goto error;
    if (!(hostName = calloc( uc.dwHostNameLength + 1, sizeof(WCHAR) )))
    {
        hr = E_OUTOFMEMORY;
        goto cleanup;
    }
    memcpy( hostName, uc.lpszHostName, uc.dwHostNameLength * sizeof(WCHAR) );

    if (!(session = WinHttpOpen( USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 ))) goto error;
    if (!(connection = WinHttpConnect( session, hostName, INTERNET_DEFAULT_HTTPS_PORT, 0 ))) goto error;
    if (!(request = WinHttpOpenRequest( connection, method, uc.lpszUrlPath, NULL, WINHTTP_NO_REFERER, accept, WINHTTP_FLAG_SECURE ))) goto error;
    if (!WinHttpSendRequest( request, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, headers ? -1 : 0, data, (data ? strlen( data ) : 0), (data ? strlen( data ) : 0), 0 )) goto error;
    if (!WinHttpReceiveResponse( request, NULL )) goto error;
    if (!WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX )) goto error;
    if (status != 200)
    {
        hr = E_FAIL;
        goto cleanup;
    }

    /* buffer response data */
    *bufferSize = 0;
    *buffer = NULL;
    tmpBuffer = NULL;
    do
    {
        if (!WinHttpQueryDataAvailable( request, &size )) goto error;
        if (!size) break;
        if (!(tmpBuffer = realloc( *buffer, *bufferSize + size )))
        {
            hr = E_OUTOFMEMORY;
            goto cleanup;
        }
        *buffer = tmpBuffer;

        if (!WinHttpReadData( request, *buffer + *bufferSize, size, &size )) goto error;
        *bufferSize += size;
    }
    while (size);
    goto cleanup;

error:
    hr = HRESULT_FROM_WIN32( GetLastError() );
cleanup:
    if (connection) WinHttpCloseHandle( connection );
    if (request) WinHttpCloseHandle( request );
    if (session) WinHttpCloseHandle( session );
    if (hostName) free( hostName );
    if (SUCCEEDED(hr)) return hr;
    if (*buffer) free( *buffer );
    *bufferSize = 0;
    *buffer = NULL;
    return hr;
}

HRESULT http_request_raw( const WCHAR *method, const WCHAR *url, const WCHAR *headers, const void *body, DWORD bodySize, DWORD *status, BYTE **buffer, SIZE_T *bufferSize )
{
    URL_COMPONENTS uc = { .dwStructSize = sizeof(URL_COMPONENTS), .dwHostNameLength = -1, .dwUrlPathLength = -1 };
    HINTERNET connection = NULL, request = NULL, session = NULL;
    DWORD size = sizeof(DWORD);
    WCHAR *hostName = NULL;
    BYTE *tmpBuffer;
    HRESULT hr = S_OK;

    TRACE( "method %s, url %s, bodySize %lu.\n", debugstr_w( method ), debugstr_w( url ), bodySize );

    *buffer = NULL;
    *bufferSize = 0;
    *status = 0;
    if (!WinHttpCrackUrl( url, 0, 0, &uc )) goto error;
    if (!(hostName = calloc( uc.dwHostNameLength + 1, sizeof(WCHAR) )))
    {
        hr = E_OUTOFMEMORY;
        goto cleanup;
    }
    memcpy( hostName, uc.lpszHostName, uc.dwHostNameLength * sizeof(WCHAR) );

    if (!(session = WinHttpOpen( USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 ))) goto error;
    if (!(connection = WinHttpConnect( session, hostName, uc.nPort, 0 ))) goto error;
    if (!(request = WinHttpOpenRequest( connection, method, uc.lpszUrlPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0 ))) goto error;
    if (!WinHttpSendRequest( request, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, headers ? -1 : 0, (void *)body, bodySize, bodySize, 0 )) goto error;
    if (!WinHttpReceiveResponse( request, NULL )) goto error;
    if (!WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, status, &size, WINHTTP_NO_HEADER_INDEX )) goto error;

    do
    {
        if (!WinHttpQueryDataAvailable( request, &size )) goto error;
        if (!size) break;
        if (!(tmpBuffer = realloc( *buffer, *bufferSize + size + 1 )))
        {
            hr = E_OUTOFMEMORY;
            goto cleanup;
        }
        *buffer = tmpBuffer;
        if (!WinHttpReadData( request, *buffer + *bufferSize, size, &size )) goto error;
        *bufferSize += size;
        (*buffer)[*bufferSize] = 0;
    }
    while (size);
    goto cleanup;

error:
    hr = HRESULT_FROM_WIN32( GetLastError() );
cleanup:
    if (request) WinHttpCloseHandle( request );
    if (connection) WinHttpCloseHandle( connection );
    if (session) WinHttpCloseHandle( session );
    free( hostName );
    if (FAILED(hr))
    {
        free( *buffer );
        *buffer = NULL;
        *bufferSize = 0;
    }
    TRACE( "status %lu, size %Iu, hr %#lx.\n", *status, *bufferSize, hr );
    return hr;
}

#define encode_base64_(sfx,type,alph)                                                                                       \
HRESULT encode_base64##sfx( const UINT32 dataSize, const BYTE *data, const UINT32 base64Size, type *base64, BOOLEAN pad )   \
{                                                                                                                           \
    static const char alphabet[64] = alph;                                                                                  \
    const BYTE *inp = data;                                                                                                 \
    type *out = base64;                                                                                                     \
                                                                                                                            \
    TRACE( "dataSize %u, data %p, base64Size %u, base64 %p, pad %d.\n", dataSize, data, base64Size, base64, pad );          \
                                                                                                                            \
    if ((dataSize * 8 + 5) / 6 + (pad ? (4 - (dataSize % 4)) % 4 : 0) > base64Size)                                         \
        return HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER );                                                             \
                                                                                                                            \
    for (UINT32 i = 0; i < dataSize / 3; i++)                                                                               \
    {                                                                                                                       \
        /* first 6 bits of byte 0 */                                                                                        \
        *out++ = alphabet[ (inp[0] >> 2) & 0x3f ];                                                                          \
        /* last 2 bits of byte 0, first 4 bits of byte 1 */                                                                 \
        *out++ = alphabet[ ((inp[0] << 4) & 0x30) | ((inp[1] >> 4) & 0x0f) ];                                               \
        /* last 4 bits of byte 1, first 2 bits of byte 2 */                                                                 \
        *out++ = alphabet[ ((inp[1] << 2) & 0x3c) | ((inp[2] >> 6) & 0x03) ];                                               \
        /* last 6 bits of byte 2 */                                                                                         \
        *out++ = alphabet[ inp[2] & 0x3f ];                                                                                 \
        inp += 3;                                                                                                           \
    }                                                                                                                       \
                                                                                                                            \
    switch (dataSize % 3)                                                                                                   \
    {                                                                                                                       \
        case 1:                                                                                                             \
            /* first 6 bits of byte 0 */                                                                                    \
            *out++ = alphabet[ (inp[0] >> 2) & 0x3f ];                                                                      \
            /* last 2 bits of byte 0, rest 0 */                                                                             \
            *out++ = alphabet[ ((inp[0] << 4) & 0x30) ];                                                                    \
            /* padding */                                                                                                   \
            if (pad)                                                                                                        \
            {                                                                                                               \
                *out++ = '=';                                                                                               \
                *out++ = '=';                                                                                               \
            }                                                                                                               \
            break;                                                                                                          \
        case 2:                                                                                                             \
            /* first 6 bits of byte 0 */                                                                                    \
            *out++ = alphabet[ (inp[0] >> 2) & 0x3f ];                                                                      \
            /* last 2 bits of byte 0, first 4 bits of byte 1 */                                                             \
            *out++ = alphabet[ ((inp[0] << 4) & 0x30) | ((inp[1] >> 4) & 0x0f) ];                                           \
            /* last 4 bits of byte 1, rest 0 */                                                                             \
            *out++ = alphabet[ ((inp[1] << 2) & 0x3c) ];                                                                    \
            /* padding */                                                                                                   \
            if (pad) *out++ = '=';                                                                                          \
            break;                                                                                                          \
    }                                                                                                                       \
    return S_OK;                                                                                                            \
}

encode_base64_(_utf16,WCHAR,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/")
encode_base64_(_url,char,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")
encode_base64_(,char,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/")

#define get_json_(sfx,typ,ret)                                                                  \
HRESULT get_json_##sfx( IJsonObject *object, const WCHAR *key, ret value )                      \
{                                                                                               \
    HSTRING_HEADER hdr;                                                                         \
    HSTRING str;                                                                                \
    HRESULT hr;                                                                                 \
                                                                                                \
    if (FAILED(hr = WindowsCreateStringReference( key, wcslen( key ), &hdr, &str ))) return hr; \
    return IJsonObject_GetNamed##typ( object, str, value );                                     \
}

get_json_(object,Object,IJsonObject**)
get_json_(array,Array,IJsonArray**)
get_json_(boolean,Boolean,boolean*)
get_json_(value,Value,IJsonValue**)
get_json_(string,String,HSTRING*)
get_json_(number,Number,DOUBLE*)
