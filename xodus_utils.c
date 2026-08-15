/*
 * Copyright 2026 Paweł Lidwin
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
#include "xodus_utils.h"

WINE_DEFAULT_DEBUG_CHANNEL(gdkc);

static BOOL pipe_write_full( HANDLE h, const void *buf, DWORD len )
{
    const BYTE *ptr = buf;
    DWORD written;

    while (len)
    {
        if (!WriteFile( h, ptr, len, &written, NULL ) || !written) return FALSE;
        ptr += written;
        len -= written;
    }
    return TRUE;
}

static BOOL pipe_read_full( HANDLE h, void *buf, DWORD len )
{
    BYTE *ptr = buf;
    DWORD got;

    while (len)
    {
        if (!ReadFile( h, ptr, len, &got, NULL ) || !got) return FALSE;
        ptr += got;
        len -= got;
    }
    return TRUE;
}

HRESULT shim_start( struct shim_channel *shim, const WCHAR *envVar )
{
    HANDLE childStdin = INVALID_HANDLE_VALUE, childStdout = INVALID_HANDLE_VALUE, childStderr = INVALID_HANDLE_VALUE, parentStdError;
    SECURITY_ATTRIBUTES sa = { .nLength = sizeof(sa), .lpSecurityDescriptor = NULL, .bInheritHandle = TRUE };
    STARTUPINFOW si = { .cb = sizeof(si), .dwFlags = STARTF_USESTDHANDLES };
    WCHAR *proxyPath = NULL;
    PROCESS_INFORMATION pi;
    HRESULT hr = S_OK;
    DWORD pathSize;

    TRACE( "shim %p, envVar %s.\n", shim, debugstr_w( envVar ) );

    InitializeCriticalSection( &shim->lock );
    parentStdError = GetStdHandle( STD_ERROR_HANDLE );
    if (parentStdError && parentStdError != INVALID_HANDLE_VALUE)
        DuplicateHandle( GetCurrentProcess(), parentStdError, GetCurrentProcess(), &childStderr, 0, TRUE, DUPLICATE_SAME_ACCESS );

    pathSize = GetEnvironmentVariableW( envVar, NULL, 0 );
    if (pathSize == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) goto error;
    if (!(proxyPath = calloc( pathSize + 2, sizeof(WCHAR) ))) return E_OUTOFMEMORY;
    proxyPath[0] = L'\"';
    if (!GetEnvironmentVariableW( envVar, proxyPath + 1, pathSize )) goto error;
    wcscat( proxyPath, L"\"" );

    if (!CreatePipe( &childStdin, &shim->toShim, &sa, 0 )) goto error;
    if (!CreatePipe( &shim->fromShim, &childStdout, &sa, 0 )) goto error;
    SetHandleInformation( shim->toShim, HANDLE_FLAG_INHERIT, 0 );
    SetHandleInformation( shim->fromShim, HANDLE_FLAG_INHERIT, 0 );

    si.hStdInput = childStdin;
    si.hStdOutput = childStdout;
    si.hStdError = childStderr;

    if (!CreateProcessW( NULL, proxyPath, NULL, NULL, TRUE,
                          CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW,
                          NULL, NULL, &si, &pi )) goto error;

    CloseHandle( pi.hThread );
    shim->process = pi.hProcess;
    shim->pid = pi.dwProcessId;
    shim->active = TRUE;
    goto cleanup;

error:
    hr = HRESULT_FROM_WIN32( GetLastError() );
cleanup:
    if (proxyPath) free( proxyPath );
    if (childStdin != INVALID_HANDLE_VALUE) CloseHandle( childStdin );
    if (childStdout != INVALID_HANDLE_VALUE) CloseHandle( childStdout );
    if (childStderr != INVALID_HANDLE_VALUE) CloseHandle( childStderr );
    return hr;
}

void shim_stop( struct shim_channel *shim )
{
    DeleteCriticalSection( &shim->lock );
    if (!shim->active) return;

    TRACE( "shim %p.\n", shim );

    CloseHandle( shim->toShim );
    GenerateConsoleCtrlEvent( CTRL_BREAK_EVENT, shim->pid );
    if (WaitForSingleObject( shim->process, 1000 ) == WAIT_TIMEOUT)
        TerminateProcess( shim->process, 1 );
    CloseHandle( shim->fromShim );
    CloseHandle( shim->process );
    shim->active = FALSE;
}

HRESULT shim_call( struct shim_channel *shim, UINT16 type, const char *payload, UINT16 payloadLen,
                    UINT16 *respType, char **resp, UINT16 *respLen )
{
    struct shim_header hdr = { XML_MAGIC, type, payloadLen };
    struct shim_header rhdr;
    HRESULT hr = S_OK;

    TRACE( "shim %p, type %u, payload %p, payloadLen %u.\n", shim, type, payload, payloadLen );

    EnterCriticalSection( &shim->lock );

    if (!shim->active)
    {
        hr = E_ABORT;
        goto done;
    }

    if (!pipe_write_full( shim->toShim, &hdr, sizeof(hdr) ) ||
        (payloadLen && !pipe_write_full( shim->toShim, payload, payloadLen )))
    {
        shim->active = FALSE;
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }

    if (!pipe_read_full( shim->fromShim, &rhdr, sizeof(rhdr) ) || rhdr.magic != XML_MAGIC)
    {
        shim->active = FALSE;
        hr = E_FAIL;
        goto done;
    }

    if (!(*resp = calloc( 1, (SIZE_T)rhdr.length + 1 )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    if (rhdr.length && !pipe_read_full( shim->fromShim, *resp, rhdr.length ))
    {
        free( *resp );
        *resp = NULL;
        shim->active = FALSE;
        hr = E_FAIL;
        goto done;
    }
    *respType = rhdr.type;
    *respLen = rhdr.length;

done:
    LeaveCriticalSection( &shim->lock );
    return hr;
}
