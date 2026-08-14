/*
 * Xbox Game runtime Library
 *  GDK Component: Networking API -> XNetworking
 *
 * Written by Weather
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

WINE_DEFAULT_DEBUG_CHANNEL(gdkc);

struct x_networking
{
    IXNetworkingImpl2 IXNetworkingImpl2_iface;
    LONG ref;
};

static inline struct x_networking *impl_from_IXNetworkingImpl2( IXNetworkingImpl2 *iface )
{
    return CONTAINING_RECORD( iface, struct x_networking, IXNetworkingImpl2_iface );
}

static HRESULT WINAPI x_networking_QueryInterface( IXNetworkingImpl2 *iface, REFIID iid, void **out )
{
    struct x_networking *impl = impl_from_IXNetworkingImpl2( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown          ) ||
        IsEqualGUID( iid, &IID_IXNetworkingImpl  ) ||
        IsEqualGUID( iid, &IID_IXNetworkingImpl2 ))
    {
        IXNetworkingImpl_AddRef( *out = &impl->IXNetworkingImpl2_iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI x_networking_AddRef( IXNetworkingImpl2 *iface )
{
    struct x_networking *impl = impl_from_IXNetworkingImpl2( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI x_networking_Release( IXNetworkingImpl2 *iface )
{
    struct x_networking *impl = impl_from_IXNetworkingImpl2( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI x_networking_XNetworkingQueryPreferredLocalUdpMultiplayerPort( IXNetworkingImpl2 *iface, UINT16 *preferredLocalUdpMultiplayerPort )
{
    FIXME( "iface %p, preferredLocalUdpMultiplayerPort %p stub!\n", iface, preferredLocalUdpMultiplayerPort );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_networking_XNetworkingQueryPreferredLocalUdpMultiplayerPortAsync( IXNetworkingImpl2 *iface, XAsyncBlock *asyncBlock )
{
    FIXME( "iface %p, asyncBlock %p stub!\n", iface, asyncBlock );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_networking_XNetworkingQueryPreferredLocalUdpMultiplayerPortAsyncResult( IXNetworkingImpl2 *iface, XAsyncBlock *asyncBlock, UINT16 *preferredLocalUdpMultiplayerPort )
{
    FIXME( "iface %p, asyncBlock %p, preferredLocalUdpMultiplayerPort %p stub!\n", iface, asyncBlock, preferredLocalUdpMultiplayerPort );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_networking_XNetworkingRegisterPreferredLocalUdpMultiplayerPortChanged( IXNetworkingImpl2 *iface, XTaskQueueHandle queue, void *context, XNetworkingPreferredLocalUdpMultiplayerPortChangedCallback *callback, XTaskQueueRegistrationToken *token )
{
    FIXME( "iface %p, queue %p, context %p, callback %p, token %p stub!\n", iface, queue, context, callback, token );
    return E_NOTIMPL;
}

static BOOLEAN WINAPI x_networking_XNetworkingUnregisterPreferredLocalUdpMultiplayerPortChanged( IXNetworkingImpl2 *iface, XTaskQueueRegistrationToken token, BOOLEAN wait )
{
    FIXME( "iface %p, token %p, wait %d stub!\n", iface, &token, wait );
    return FALSE;
}

/*
 * XNetworkingQuerySecurityInformationForUrl
 * -----------------------------------------
 *
 * libHttpClient.GDK calls this before every request to learn how the endpoint
 * must be secured: which TLS versions are permitted, and which certificate
 * thumbprints the chain has to contain if the title pins its services.
 *
 * Failing it does not degrade to "no pinning" -- libHttpClient simply never
 * issues the request. Minecraft Bedrock opens no sockets at all while this
 * returns E_NOTIMPL: it obtains its Xbox token, then silently gives up on
 * signing in, because nothing it wanted to send ever left.
 *
 * What is reported here is TLS 1.2 and 1.3 with no pinned thumbprints, which is
 * what a title gets for an endpoint it has not pinned. Real pinning data comes
 * from the sandbox's endpoint policy, which is not available offline; a title
 * that genuinely pins would want its own list, and this would silently accept a
 * chain it should have refused. The alternative is that no request happens at
 * all, and the TLS stack still validates the chain normally underneath.
 */

/* WinHTTP's protocol flags, which is the encoding this field uses. */
#define SECURITY_PROTOCOL_TLS1_2 0x00000800
#define SECURITY_PROTOCOL_TLS1_3 0x00002000

static HRESULT CALLBACK security_info_provider( XAsyncOp op, const XAsyncProviderData *data )
{
    switch (op)
    {
    case XAsyncOp_Begin:
        /* Nothing to look up, so there is nothing to schedule: answer now and
         * let the completion routine run on the caller's queue. */
        IXThreadingImpl_XAsyncComplete( x_threading_impl, data->async, S_OK,
                                        sizeof(XNetworkingSecurityInformation) );
        return S_OK;

    case XAsyncOp_GetResult:
    {
        XNetworkingSecurityInformation *info = data->buffer;

        if (data->bufferSize < sizeof(*info)) return E_NOT_SUFFICIENT_BUFFER;
        info->enabledHttpSecurityProtocolFlags = SECURITY_PROTOCOL_TLS1_2 | SECURITY_PROTOCOL_TLS1_3;
        info->thumbprintCount = 0;
        info->thumbprints = NULL;
        return S_OK;
    }

    default:
        return S_OK;
    }
}

static HRESULT security_info_begin( XAsyncBlock *async )
{
    return IXThreadingImpl_XAsyncBegin( x_threading_impl, async, NULL, security_info_begin,
                                        "XNetworkingQuerySecurityInformationForUrl",
                                        security_info_provider );
}

static HRESULT security_info_result( XAsyncBlock *async, SIZE_T size, SIZE_T *used, UINT8 *buffer,
                                     XNetworkingSecurityInformation **out )
{
    HRESULT hr;

    if (!buffer) return E_POINTER;
    hr = IXThreadingImpl_XAsyncGetResult( x_threading_impl, async, security_info_begin, size, buffer, used );
    /* The structure is laid out at the head of the caller's own buffer. */
    if (SUCCEEDED(hr) && out) *out = (XNetworkingSecurityInformation *)buffer;
    return hr;
}

static HRESULT WINAPI x_networking_XNetworkingQuerySecurityInformationForUrlAsync( IXNetworkingImpl2 *iface, const char *url, XAsyncBlock *asyncBlock )
{
    TRACE( "iface %p, url %s, asyncBlock %p.\n", iface, debugstr_a( url ), asyncBlock );
    return security_info_begin( asyncBlock );
}

static HRESULT WINAPI x_networking_XNetworkingQuerySecurityInformationForUrlAsyncResultSize( IXNetworkingImpl2 *iface, XAsyncBlock *asyncBlock, SIZE_T *securityInformationBufferByteCount )
{
    TRACE( "iface %p, asyncBlock %p, securityInformationBufferByteCount %p.\n", iface, asyncBlock, securityInformationBufferByteCount );

    if (!securityInformationBufferByteCount) return E_POINTER;
    return IXThreadingImpl_XAsyncGetResultSize( x_threading_impl, asyncBlock, securityInformationBufferByteCount );
}

static HRESULT WINAPI x_networking_XNetworkingQuerySecurityInformationForUrlAsyncResult( IXNetworkingImpl2 *iface, XAsyncBlock *asyncBlock, SIZE_T securityInformationBufferByteCount, SIZE_T *securityInformationBufferByteCountUsed, UINT8 *securityInformationBuffer, XNetworkingSecurityInformation **securityInformation )
{
    TRACE( "iface %p, asyncBlock %p, byteCount %Iu, buffer %p.\n", iface, asyncBlock, securityInformationBufferByteCount, securityInformationBuffer );

    return security_info_result( asyncBlock, securityInformationBufferByteCount,
                                 securityInformationBufferByteCountUsed, securityInformationBuffer,
                                 securityInformation );
}

static HRESULT WINAPI x_networking_XNetworkingQuerySecurityInformationForUrlUtf16Async( IXNetworkingImpl2 *iface, const WCHAR *url, XAsyncBlock *asyncBlock )
{
    TRACE( "iface %p, url %s, asyncBlock %p.\n", iface, debugstr_w( url ), asyncBlock );
    return security_info_begin( asyncBlock );
}

static HRESULT WINAPI x_networking_XNetworkingQuerySecurityInformationForUrlUtf16AsyncResultSize( IXNetworkingImpl2 *iface, XAsyncBlock *asyncBlock, SIZE_T *securityInformationBufferByteCount )
{
    TRACE( "iface %p, asyncBlock %p, securityInformationBufferByteCount %p.\n", iface, asyncBlock, securityInformationBufferByteCount );

    if (!securityInformationBufferByteCount) return E_POINTER;
    return IXThreadingImpl_XAsyncGetResultSize( x_threading_impl, asyncBlock, securityInformationBufferByteCount );
}

static HRESULT WINAPI x_networking_XNetworkingQuerySecurityInformationForUrlUtf16AsyncResult( IXNetworkingImpl2 *iface, XAsyncBlock *asyncBlock, SIZE_T securityInformationBufferByteCount, SIZE_T *securityInformationBufferByteCountUsed, UINT8 *securityInformationBuffer, XNetworkingSecurityInformation **securityInformation )
{
    TRACE( "iface %p, asyncBlock %p, byteCount %Iu, buffer %p.\n", iface, asyncBlock, securityInformationBufferByteCount, securityInformationBuffer );

    return security_info_result( asyncBlock, securityInformationBufferByteCount,
                                 securityInformationBufferByteCountUsed, securityInformationBuffer,
                                 securityInformation );
}

static HRESULT WINAPI x_networking_XNetworkingVerifyServerCertificate( IXNetworkingImpl2 *iface, void *requestHandle, const XNetworkingSecurityInformation *securityInformation )
{
    TRACE( "iface %p, requestHandle %p, securityInformation %p.\n", iface, requestHandle, securityInformation );

    /* We report no pinned thumbprints, so there is no extra constraint to check
     * beyond the chain validation the TLS stack has already done. */
    return S_OK;
}

/*
 * Connectivity reporting. Titles call these while bringing their networking
 * subsystem up, and failing the calls does not make them fall back to an
 * offline mode -- it leaves that subsystem half-constructed, and a worker
 * thread started later walks into an object that was never initialised.
 * Asphalt Legends dies exactly that way: a fresh thread enters a NULL
 * CRITICAL_SECTION (access violation in RtlEnterCriticalSection with rcx=0)
 * moments after the registration below is refused.
 *
 * There is no connectivity monitoring behind this yet, so report a plain
 * unmetered internet connection and accept registrations without ever raising
 * a change. That is a truthful description of a host whose connectivity we do
 * not track, and it keeps callers on their normal path.
 */
static HRESULT WINAPI x_networking_XNetworkingGetConnectivityHint( IXNetworkingImpl2 *iface, XNetworkingConnectivityHint *connectivityHint )
{
    FIXME( "iface %p, connectivityHint %p: reporting unmetered internet access.\n", iface, connectivityHint );

    if (!connectivityHint) return E_POINTER;

    connectivityHint->connectivityLevel = XNetworkingConnectivityLevelHint_InternetAccess;
    connectivityHint->connectivityCost = XNetworkingConnectivityCostHint_Unrestricted;
    connectivityHint->ianaInterfaceType = 6; /* IF_TYPE_ETHERNET_CSMACD */
    connectivityHint->networkInitialized = TRUE;
    connectivityHint->approachingDataLimit = FALSE;
    connectivityHint->overDataLimit = FALSE;
    connectivityHint->roaming = FALSE;
    return S_OK;
}

static LONG64 connectivity_hint_token;

struct connectivity_registration
{
    void *context;
    XNetworkingConnectivityHintChangedCallback *callback;
};

/* Deliver the current state once, on the caller's queue. */
static void CALLBACK connectivity_initial_notify( void *context, BOOLEAN canceled )
{
    struct connectivity_registration *reg = context;
    XNetworkingConnectivityHint hint = { 0 };

    if (!canceled)
    {
        IXNetworkingImpl2_XNetworkingGetConnectivityHint( (IXNetworkingImpl2 *)x_networking_impl, &hint );
        reg->callback( reg->context, &hint );
    }
    free( reg );
}

static HRESULT WINAPI x_networking_XNetworkingRegisterConnectivityHintChanged( IXNetworkingImpl2 *iface, XTaskQueueHandle queue, void *context, XNetworkingConnectivityHintChangedCallback *callback, XTaskQueueRegistrationToken *token )
{
    struct connectivity_registration *reg;

    FIXME( "iface %p, queue %p, context %p, callback %p, token %p: accepted, raising the current state once.\n",
           iface, queue, context, callback, token );

    if (!callback || !token) return E_INVALIDARG;

    token->token = InterlockedIncrement64( &connectivity_hint_token );

    /* Registering and then never calling back is not a neutral choice: a title
     * that waits for its first connectivity notification before starting any
     * HTTP work simply never starts. Asphalt Legends sits there polling
     * XNetworkingGetConnectivityHint without ever opening a socket, and reports
     * a network error. Nothing here monitors the link, so no *change* can be
     * reported, but the current state can and should be delivered once. */
    if ((reg = calloc( 1, sizeof(*reg) )))
    {
        XTaskQueueHandle target = queue, owned = NULL;

        /* A NULL queue is not "do not call me back" -- it means dispatch on the
         * process task queue. Asphalt registers a second time with NULL and
         * waits on that one, so skipping it left the caller hanging. */
        if (!target)
        {
            if (!IXThreadingImpl_XTaskQueueGetCurrentProcessTaskQueue( x_threading_impl, &owned ) &&
                FAILED(IXThreadingImpl_XTaskQueueCreate( x_threading_impl,
                                                         XTaskQueueDispatchMode_ThreadPool,
                                                         XTaskQueueDispatchMode_ThreadPool, &owned )))
                owned = NULL;
            target = owned;
        }

        reg->context = context;
        reg->callback = callback;
        if (!target ||
            FAILED(IXThreadingImpl_XTaskQueueSubmitCallback( x_threading_impl, target,
                                                             XTaskQueuePort_Completion, reg,
                                                             connectivity_initial_notify )))
            free( reg );

        /* Submitting took its own reference, so ours can go now. */
        if (owned) IXThreadingImpl_XTaskQueueCloseHandle( x_threading_impl, owned );
    }

    return S_OK;
}

static BOOLEAN WINAPI x_networking_XNetworkingUnregisterConnectivityHintChanged( IXNetworkingImpl2 *iface, XTaskQueueRegistrationToken token, BOOLEAN wait )
{
    TRACE( "iface %p, token %I64u, wait %d.\n", iface, token.token, wait );

    /* Nothing was ever scheduled, so there is nothing to wait for. */
    return TRUE;
}

static HRESULT WINAPI x_networking_XNetworkingQueryConfigurationSetting( IXNetworkingImpl2 *iface, XNetworkingConfigurationSetting configurationSetting, UINT64 *value )
{
    FIXME( "iface %p, configurationSetting %d, value %p stub!\n", iface, configurationSetting, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_networking_XNetworkingSetConfigurationSetting( IXNetworkingImpl2 *iface, XNetworkingConfigurationSetting configurationParameter, UINT64 value )
{
    FIXME( "iface %p, configurationParameter %d, value %llu stub!\n", iface, configurationParameter, value );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_networking_XNetworkingQueryStatistics( IXNetworkingImpl2 *iface, XNetworkingStatisticsType statisticsType, XNetworkingStatisticsBuffer *statisticsBuffer )
{
    FIXME( "iface %p, statisticsType %d, statisticsBuffer %p stub!\n", iface, statisticsType, statisticsBuffer );
    return E_NOTIMPL;
}

static const struct IXNetworkingImpl2Vtbl x_networking_vtbl =
{
    x_networking_QueryInterface,
    x_networking_AddRef,
    x_networking_Release,
    /* IXNetworkingImpl methods */
    x_networking_XNetworkingQueryPreferredLocalUdpMultiplayerPort,
    x_networking_XNetworkingQueryPreferredLocalUdpMultiplayerPortAsync,
    x_networking_XNetworkingQueryPreferredLocalUdpMultiplayerPortAsyncResult,
    x_networking_XNetworkingRegisterPreferredLocalUdpMultiplayerPortChanged,
    x_networking_XNetworkingUnregisterPreferredLocalUdpMultiplayerPortChanged,
    x_networking_XNetworkingQuerySecurityInformationForUrlAsync,
    x_networking_XNetworkingQuerySecurityInformationForUrlAsyncResultSize,
    x_networking_XNetworkingQuerySecurityInformationForUrlAsyncResult,
    x_networking_XNetworkingQuerySecurityInformationForUrlUtf16Async,
    x_networking_XNetworkingQuerySecurityInformationForUrlUtf16AsyncResultSize,
    x_networking_XNetworkingQuerySecurityInformationForUrlUtf16AsyncResult,
    x_networking_XNetworkingVerifyServerCertificate,
    x_networking_XNetworkingGetConnectivityHint,
    x_networking_XNetworkingRegisterConnectivityHintChanged,
    x_networking_XNetworkingUnregisterConnectivityHintChanged,
    /* IXNetworkingImpl2 methods */
    x_networking_XNetworkingQueryConfigurationSetting,
    x_networking_XNetworkingSetConfigurationSetting,
    x_networking_XNetworkingQueryStatistics,
};

static struct x_networking x_networking =
{
    {&x_networking_vtbl},
    0,
};

IXNetworkingImpl *x_networking_impl = (IXNetworkingImpl *)&x_networking.IXNetworkingImpl2_iface;
