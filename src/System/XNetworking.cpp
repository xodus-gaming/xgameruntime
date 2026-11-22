/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XNetworking
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

#include "../private.h"

#include <atomic>
#include <mutex>
#include <set>

namespace
{

/* Titles call XNetworkingQuerySecurityInformationForUrl(Utf16)Async to learn the TLS
 * configuration of a server before they connect to it. There is no TLS stack here to
 * inspect, so every query succeeds with the TLS 1.2 protocol flag and no thumbprints.
 * The url is only checked for null: it is never parsed and never logged, because it can
 * carry credentials.
 *
 * XAsyncGetResult() rejects a result that is read through a different call than the one
 * that started the operation, so the two variants use different identities. */
const char security_info_identity_utf8 = 0;
const char security_info_identity_utf16 = 0;

const UINT32 tls_1_2_protocol_flag = 0x800;

/* The work callback of XAsync completes the operation with E_UNEXPECTED unless the
 * provider returns E_PENDING, so DoWork completes the call itself and returns E_PENDING. */
HRESULT WINAPI security_info_provider( XAsyncOp op, const XAsyncProviderData *data )
{
    switch ( op )
    {
        case XAsyncOp::Begin:
            return XAsyncSchedule( data->async, 0 );

        case XAsyncOp::DoWork:
            XAsyncComplete( data->async, S_OK, sizeof( XNetworkingSecurityInformation ) );
            return E_PENDING;

        case XAsyncOp::GetResult:
        {
            auto *info = static_cast<XNetworkingSecurityInformation *>( data->buffer );
            info->enabledHttpSecurityProtocolFlags = tls_1_2_protocol_flag;
            info->thumbprintCount = 0;
            info->thumbprints = nullptr;
            return S_OK;
        }

        case XAsyncOp::Cancel:
        case XAsyncOp::Cleanup:
        default:
            return S_OK;
    }
}

HRESULT security_info_begin( XAsyncBlock *asyncBlock, const void *url, const void *identity, const char *name )
{
    if ( !url || !asyncBlock ) return E_INVALIDARG;
    return XAsyncBegin( asyncBlock, nullptr, identity, name, security_info_provider );
}

HRESULT security_info_result_size( XAsyncBlock *asyncBlock, SIZE_T *size )
{
    if ( !asyncBlock || !size ) return E_INVALIDARG;
    return XAsyncGetResultSize( asyncBlock, size );
}

HRESULT security_info_result( XAsyncBlock *asyncBlock, const void *identity, SIZE_T bufferSize, SIZE_T *bufferUsed,
                              UINT8 *buffer, XNetworkingSecurityInformation **securityInformation )
{
    HRESULT hr;

    if ( !asyncBlock || !buffer || !securityInformation ) return E_INVALIDARG;

    hr = XAsyncGetResult( asyncBlock, identity, bufferSize, buffer, bufferUsed );
    if ( SUCCEEDED( hr ) )
        *securityInformation = reinterpret_cast<XNetworkingSecurityInformation *>( buffer );

    return hr;
}

}

class XNetworkingImpl :
    public IXNetworkingImpl2
{
public:
    HRESULT WINAPI QueryInterface( REFIID iid, void **out ) noexcept override
    {
        TRACE( "iface %p, iid %s, out %p.\n", this, debugstr_guid( &iid ), out );

        if ( !out ) return E_POINTER;
        *out = nullptr;

        if ( iid == __uuidof( IXNetworkingImpl2 ) )
        {
            AddRef();
            *out = static_cast<IXNetworkingImpl2 *>( this );
            return S_OK;
        }

        if ( iid == __uuidof( IUnknown ) ||
             iid == __uuidof( IInspectable ) ||
             iid == __uuidof( IAgileObject ) ||
             iid == __uuidof( IXNetworkingImpl ) )
        {
            AddRef();
            *out = static_cast<IXNetworkingImpl *>( this );
            return S_OK;
        }

        FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( &iid ) );
        return E_NOINTERFACE;
    }

    ULONG WINAPI
    AddRef() noexcept override
    {
        ULONG curr = static_cast<ULONG>( ++ref );
        TRACE( "iface %p increasing refcount to %lu.\n", this, curr );
        return curr;
    }

    ULONG WINAPI
    Release() noexcept override
    {
        ULONG curr = static_cast<ULONG>( --ref );
        TRACE( "iface %p decreasing refcount to %lu.\n", this, curr );

        // The object is static and is never deleted.
        return curr;
    }

    HRESULT WINAPI XNetworkingQueryPreferredLocalUdpMultiplayerPort( UINT16 *preferredLocalUdpMultiplayerPort ) override
    {
        FIXME( "preferredLocalUdpMultiplayerPort %p stub!\n", preferredLocalUdpMultiplayerPort );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XNetworkingQueryPreferredLocalUdpMultiplayerPortAsync( XAsyncBlock *asyncBlock ) override
    {
        FIXME( "asyncBlock %p stub!\n", asyncBlock );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XNetworkingQueryPreferredLocalUdpMultiplayerPortAsyncResult( XAsyncBlock *asyncBlock, UINT16 *preferredLocalUdpMultiplayerPort ) override
    {
        FIXME( "asyncBlock %p, preferredLocalUdpMultiplayerPort %p stub!\n", asyncBlock, preferredLocalUdpMultiplayerPort );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XNetworkingRegisterPreferredLocalUdpMultiplayerPortChanged( XTaskQueueHandle queue, void *context, XNetworkingPreferredLocalUdpMultiplayerPortChangedCallback *callback, XTaskQueueRegistrationToken *token ) override
    {
        FIXME( "queue %p, context %p, callback %p, token %p stub!\n", queue, context, callback, token );
        return E_NOTIMPL;
    }

    BOOLEAN WINAPI XNetworkingUnregisterPreferredLocalUdpMultiplayerPortChanged( XTaskQueueRegistrationToken token, BOOLEAN wait ) override
    {
        FIXME( "token %llu, wait %d stub!\n", static_cast<unsigned long long>( token.token ), wait );
        return FALSE;
    }

    HRESULT WINAPI XNetworkingQuerySecurityInformationForUrlAsync( const char *url, XAsyncBlock *asyncBlock ) override
    {
        TRACE( "XNetworkingQuerySecurityInformationForUrlAsync: url %p, asyncBlock %p.\n", url, asyncBlock );
        return security_info_begin( asyncBlock, url, &security_info_identity_utf8, "XNetworkingQuerySecurityInformationForUrlAsync" );
    }

    HRESULT WINAPI XNetworkingQuerySecurityInformationForUrlAsyncResultSize( XAsyncBlock *asyncBlock, SIZE_T *securityInformationBufferByteCount ) override
    {
        TRACE( "XNetworkingQuerySecurityInformationForUrlAsyncResultSize: asyncBlock %p, securityInformationBufferByteCount %p.\n", asyncBlock, securityInformationBufferByteCount );
        return security_info_result_size( asyncBlock, securityInformationBufferByteCount );
    }

    HRESULT WINAPI XNetworkingQuerySecurityInformationForUrlAsyncResult( XAsyncBlock *asyncBlock, SIZE_T securityInformationBufferByteCount, SIZE_T *securityInformationBufferByteCountUsed, UINT8 *securityInformationBuffer, XNetworkingSecurityInformation **securityInformation ) override
    {
        TRACE( "XNetworkingQuerySecurityInformationForUrlAsyncResult: asyncBlock %p, buffer size %llu, used %p, buffer %p, securityInformation %p.\n", asyncBlock, static_cast<unsigned long long>( securityInformationBufferByteCount ), securityInformationBufferByteCountUsed, securityInformationBuffer, securityInformation );
        return security_info_result( asyncBlock, &security_info_identity_utf8, securityInformationBufferByteCount, securityInformationBufferByteCountUsed, securityInformationBuffer, securityInformation );
    }

    HRESULT WINAPI XNetworkingQuerySecurityInformationForUrlUtf16Async( const WCHAR *url, XAsyncBlock *asyncBlock ) override
    {
        TRACE( "XNetworkingQuerySecurityInformationForUrlUtf16Async: url %p, asyncBlock %p.\n", url, asyncBlock );
        return security_info_begin( asyncBlock, url, &security_info_identity_utf16, "XNetworkingQuerySecurityInformationForUrlUtf16Async" );
    }

    HRESULT WINAPI XNetworkingQuerySecurityInformationForUrlUtf16AsyncResultSize( XAsyncBlock *asyncBlock, SIZE_T *securityInformationBufferByteCount ) override
    {
        TRACE( "XNetworkingQuerySecurityInformationForUrlUtf16AsyncResultSize: asyncBlock %p, securityInformationBufferByteCount %p.\n", asyncBlock, securityInformationBufferByteCount );
        return security_info_result_size( asyncBlock, securityInformationBufferByteCount );
    }

    HRESULT WINAPI XNetworkingQuerySecurityInformationForUrlUtf16AsyncResult( XAsyncBlock *asyncBlock, SIZE_T securityInformationBufferByteCount, SIZE_T *securityInformationBufferByteCountUsed, UINT8 *securityInformationBuffer, XNetworkingSecurityInformation **securityInformation ) override
    {
        TRACE( "XNetworkingQuerySecurityInformationForUrlUtf16AsyncResult: asyncBlock %p, buffer size %llu, used %p, buffer %p, securityInformation %p.\n", asyncBlock, static_cast<unsigned long long>( securityInformationBufferByteCount ), securityInformationBufferByteCountUsed, securityInformationBuffer, securityInformation );
        return security_info_result( asyncBlock, &security_info_identity_utf16, securityInformationBufferByteCount, securityInformationBufferByteCountUsed, securityInformationBuffer, securityInformation );
    }

    /* There is no TLS stack to check a certificate against. The security information this
     * library hands out never contains thumbprints, so there are no pins to verify and the
     * call succeeds; pins that were supplied by someone else cannot be checked, so those
     * fail instead of being accepted unchecked. */
    HRESULT WINAPI XNetworkingVerifyServerCertificate( void *requestHandle, const XNetworkingSecurityInformation *securityInformation ) override
    {
        TRACE( "requestHandle %p, securityInformation %p.\n", requestHandle, securityInformation );

        if ( !requestHandle || !securityInformation ) return E_INVALIDARG;

        if ( securityInformation->thumbprintCount )
        {
            FIXME( "certificate pinning is not implemented, returning E_NOTIMPL.\n" );
            return E_NOTIMPL;
        }

        FIXME( "requestHandle %p stub: no certificate verification.\n", requestHandle );
        return S_OK;
    }

    /* The connectivity hint is fixed: the host network is not probed, so the hint never
     * changes and a registered callback is never invoked. */
    HRESULT WINAPI XNetworkingGetConnectivityHint( XNetworkingConnectivityHint *connectivityHint ) override
    {
        TRACE( "connectivityHint %p.\n", connectivityHint );

        if ( !connectivityHint ) return E_INVALIDARG;

        connectivityHint->connectivityLevel = XNetworkingConnectivityLevelHint::InternetAccess;
        connectivityHint->connectivityCost = XNetworkingConnectivityCostHint::Unrestricted;
        connectivityHint->ianaInterfaceType = 0;
        connectivityHint->networkInitialized = TRUE;
        connectivityHint->approachingDataLimit = FALSE;
        connectivityHint->overDataLimit = FALSE;
        connectivityHint->roaming = FALSE;

        return S_OK;
    }

    HRESULT WINAPI XNetworkingRegisterConnectivityHintChanged( XTaskQueueHandle queue, void *context, XNetworkingConnectivityHintChangedCallback *callback, XTaskQueueRegistrationToken *token ) override
    {
        TRACE( "queue %p, context %p, callback %p, token %p.\n", queue, context, callback, token );

        if ( !callback || !token ) return E_INVALIDARG;

        std::lock_guard<std::mutex> lock( hintMutex );
        token->token = nextHintToken++;
        hintTokens.insert( token->token );
        return S_OK;
    }

    BOOLEAN WINAPI XNetworkingUnregisterConnectivityHintChanged( XTaskQueueRegistrationToken token, BOOLEAN wait ) override
    {
        TRACE( "token %llu, wait %d.\n", static_cast<unsigned long long>( token.token ), wait );

        std::lock_guard<std::mutex> lock( hintMutex );
        return hintTokens.erase( token.token ) ? TRUE : FALSE;
    }

    HRESULT WINAPI XNetworkingQueryConfigurationSetting( XNetworkingConfigurationSetting configurationSetting, UINT64 *value ) override
    {
        FIXME( "configurationSetting %u, value %p stub!\n", static_cast<unsigned>( configurationSetting ), value );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XNetworkingSetConfigurationSetting( XNetworkingConfigurationSetting configurationParameter, UINT64 value ) override
    {
        FIXME( "configurationParameter %u, value %llu stub!\n", static_cast<unsigned>( configurationParameter ), static_cast<unsigned long long>( value ) );
        return E_NOTIMPL;
    }

    HRESULT WINAPI XNetworkingQueryStatistics( XNetworkingStatisticsType statisticsType, XNetworkingStatisticsBuffer *statisticsBuffer ) override
    {
        FIXME( "statisticsType %u, statisticsBuffer %p stub!\n", static_cast<unsigned>( statisticsType ), statisticsBuffer );
        return E_NOTIMPL;
    }

private:
    std::atomic_long ref{ 1 };
    std::mutex hintMutex;
    std::set<UINT64> hintTokens;
    UINT64 nextHintToken{ 1 };
};

static XNetworkingImpl g_x_networking;

IXNetworkingImpl *x_networking_impl = static_cast<IXNetworkingImpl *>( &g_x_networking );
