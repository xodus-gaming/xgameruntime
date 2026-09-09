/*
 * Copyright (c) 2025 Weather
 *
 * Permission is hereby granted, CoTaskMemFree of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <private/logging.h>
#include <private/statics.h>
#include <private/env.h>

#include <stdio.h>
#include <windows.h>
#include <io.h>

static LPCSTR debugLevels[] =
{
    "err",
    "info",
    "fixme",
    "warn",
    "trace",
};

static Log_Category logLevel = LOG_CATEGORY_FIXME;
static HANDLE logFile = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION cs;

static HRESULT
ParseMessage(
    IN LPCSTR format,
    OPTIONAL IN Log_Category logCategory,
    OPTIONAL IN DWORD threadId,
    OPTIONAL IN LPCSTR module,
    OPTIONAL IN LPCSTR function,
    OPTIONAL IN LPCSTR message,
    OUT LPSTR *formattedString
) {
    LPSTR buffer;
    LPSTR reallocBuffer;
    SIZE_T fmtSize;
    SIZE_T currentPos = 0;
    SIZE_T iterator;

    if ( !format || !formattedString )
        return E_INVALIDARG;

    fmtSize = strlen(format) + 1;

    buffer = (LPSTR)CoTaskMemAlloc( fmtSize * sizeof( CHAR ) );
    if ( !buffer )
        return E_OUTOFMEMORY;
    buffer[0] = '\0';

    for ( iterator = 0; format[iterator] != '\0'; iterator++ )
    {
        if ( format[iterator] == '$' )
        {
            if ( strlen( format + iterator ) >= 5 && strncmp( format + iterator, "$DATE", 5 ) == 0 )
            {
                CHAR dateStr[11];
                SYSTEMTIME st;

                fmtSize -= strlen( "$DATE" );

                GetLocalTime(&st);

                // date format: YYYY-MM-DD (11 bytes)
                snprintf( dateStr, ARRAYSIZE(dateStr), "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay );

                fmtSize += strlen( dateStr );

                reallocBuffer = (LPSTR)CoTaskMemRealloc( buffer, fmtSize );
                if ( !reallocBuffer )
                {
                    CoTaskMemFree( buffer );
                    return E_OUTOFMEMORY;
                }

                buffer = reallocBuffer;

                strncat( buffer, dateStr, 10 );

                currentPos += 10;
                iterator += 4;
                continue;
            }

            if ( strlen( format + iterator ) >= 5 && strncmp( format + iterator, "$TIME", 5 ) == 0 )
            {
                CHAR timeStr[9];
                SYSTEMTIME st;

                fmtSize -= strlen( "$TIME" );

                GetLocalTime(&st);
                // time format: HH:MM:SS (9 bytes)
                snprintf( timeStr, sizeof( timeStr ), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond );
                fmtSize += strlen( timeStr );

                reallocBuffer = (LPSTR)CoTaskMemRealloc( buffer, fmtSize );
                if ( !reallocBuffer )
                {
                    CoTaskMemFree( buffer );
                    return E_OUTOFMEMORY;
                }

                buffer = reallocBuffer;

                strncat( buffer, timeStr, 8 );

                currentPos += 8;
                iterator += 4;
                continue;
            }

            if ( strlen( format + iterator ) >= 8 && strncmp( format + iterator, "$VERSION", 8 ) == 0 )
            {
                auto const versionString = XGAMERUNTIME_VERSION;
                fmtSize -= strlen( "$VERSION" );
                fmtSize += strlen( versionString );

                reallocBuffer = (LPSTR)CoTaskMemRealloc( buffer, fmtSize );
                if ( !reallocBuffer )
                {
                    CoTaskMemFree( buffer );
                    return E_OUTOFMEMORY;
                }

                buffer = reallocBuffer;

                strncat( buffer, versionString, strlen( versionString ) + 1 );

                currentPos += strlen( versionString );
                iterator += 7;
                continue;
            }

            if ( strlen( format + iterator ) >= 13 && strncmp( format + iterator, "$LOG_CATEGORY", 13 ) == 0 )
            {
                CHAR logCategoryStr[8];

                strcpy( logCategoryStr, LogCategoryNames[ (int)logCategory ] );

                fmtSize -= strlen( "$LOG_CATEGORY" );
                fmtSize += strlen( logCategoryStr );

                reallocBuffer = (LPSTR)CoTaskMemRealloc( buffer, fmtSize );
                if ( !reallocBuffer )
                {
                    CoTaskMemFree( buffer );
                    return E_OUTOFMEMORY;
                }

                buffer = reallocBuffer;

                strcat( buffer, logCategoryStr );

                currentPos += strlen( logCategoryStr );
                iterator += 12;
                continue;
            }

            if ( strlen( format + iterator ) >= 7 && strncmp( format + iterator, "$THREAD", 7 ) == 0 )
            {
                CHAR threadString[20];
                sprintf( threadString, "%lu", threadId );
                fmtSize -= strlen( "$THREAD" );
                fmtSize += strlen( threadString );

                reallocBuffer = (LPSTR)CoTaskMemRealloc( buffer, fmtSize );
                if ( !reallocBuffer )
                {
                    CoTaskMemFree( buffer );
                    return E_OUTOFMEMORY;
                }

                buffer = reallocBuffer;

                strcat( buffer, threadString );

                currentPos += strlen( threadString );
                iterator += 6;
                continue;
            }

            if ( strlen( format + iterator ) >= 7 && strncmp( format + iterator, "$MODULE", 7 ) == 0 )
            {
                fmtSize -= strlen( "$MODULE" );
                fmtSize += strlen( module );

                reallocBuffer = (LPSTR)CoTaskMemRealloc( buffer, fmtSize );
                if ( !reallocBuffer )
                {
                    CoTaskMemFree( buffer );
                    return E_OUTOFMEMORY;
                }

                buffer = reallocBuffer;

                strcat( buffer, module );

                currentPos += strlen( module );
                iterator += 6;
                continue;
            }

            if ( strlen( format + iterator ) >= 9 && strncmp( format + iterator, "$FUNCTION", 9 ) == 0 )
            {
                fmtSize -= strlen( "$FUNCTION" );
                fmtSize += strlen( function );

                reallocBuffer = (LPSTR)CoTaskMemRealloc( buffer, fmtSize );
                if ( !reallocBuffer )
                {
                    CoTaskMemFree( buffer );
                    return E_OUTOFMEMORY;
                }

                buffer = reallocBuffer;

                strcat( buffer, function );

                currentPos += strlen( function );
                iterator += 8;
                continue;
            }

            if ( strlen( format + iterator ) >= 8 && strncmp( format + iterator, "$MESSAGE", 8 ) == 0 )
            {
                fmtSize -= strlen( "$MESSAGE" );
                fmtSize += strlen( message );

                reallocBuffer = (LPSTR)CoTaskMemRealloc( buffer, fmtSize );
                if ( !reallocBuffer )
                {
                    CoTaskMemFree( buffer );
                    return E_OUTOFMEMORY;
                }

                buffer = reallocBuffer;

                strcat( buffer, message );

                currentPos += strlen( message );
                iterator += 7;
                continue;
            }
        }

        buffer[currentPos++] = format[iterator];
        buffer[currentPos] = '\0';
    }

    *formattedString = buffer;

    return S_OK;
}

VOID
XGameRuntime_DEBUG(
    IN Log_Category category,
    IN pid_t threadId,
    IN LPCSTR module,
    IN LPCSTR function,
    IN LPCSTR fmt,
    ...
) {
    va_list ap = {};
    va_list ap_copy = {};

    LPSTR parsedMessage;
    LPSTR buffer;
    SIZE_T bufferSize;
    UINT32 iter;

    if ( logLevel < category )
        return;

    va_start( ap, fmt );
    va_copy( ap_copy, ap );
    bufferSize = vsnprintf( NULL, 0, fmt, ap_copy );
    va_end( ap_copy );

    buffer = (LPSTR)CoTaskMemAlloc( bufferSize + 1 );
    if ( !buffer )
    {
        va_end( ap );
        return;
    }

    vsnprintf( buffer, bufferSize + 1, fmt, ap );
    va_end( ap );

    ParseMessage( LOG_FORMAT, category, threadId, module, function, buffer, &parsedMessage );

    EnterCriticalSection( &cs );
    WriteFile( logFile, parsedMessage, (DWORD)lstrlenA( parsedMessage ), NULL, NULL );
    LeaveCriticalSection( &cs );

    CoTaskMemFree( parsedMessage );
    CoTaskMemFree( buffer );
}

VOID
InitializeLogging()
{
    CHAR env_buffer[MAX_ENV_BUFFER];
    UINT32 iter;

    InitializeCriticalSection( &cs );

    if ( SUCCEEDED( xgameruntime_get_env( "XGAMERUNTIME_LOG_LEVEL", env_buffer, sizeof( env_buffer ) ) ) )
    {
        for ( iter = 0; iter < sizeof(LogCategoryNames) / sizeof(LPCSTR); ++iter )
            if ( !strcmp( LogCategoryNames[ iter ], env_buffer ) )
                logLevel = (Log_Category)iter;
    }

    RtlZeroMemory( env_buffer, sizeof( env_buffer ) );

    if ( SUCCEEDED( xgameruntime_get_env( "XGAMERUNTIME_LOG_FILE", env_buffer, sizeof( env_buffer ) ) ) )
    {
        logFile = CreateFileA( env_buffer, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    }

    if ( logFile == INVALID_HANDLE_VALUE || logFile == NULL )
    {
        logFile = GetStdHandle( STD_OUTPUT_HANDLE );
    }
}