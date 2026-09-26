/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XAsync, XTaskQueue and XThread
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

struct x_threading
{
    IXThreadingImpl IXThreadingImpl_iface;
    LONG ref;
};

static inline struct x_threading *impl_from_IXThreadingImpl( IXThreadingImpl *iface )
{
    return CONTAINING_RECORD( iface, struct x_threading, IXThreadingImpl_iface );
}

static HRESULT WINAPI x_threading_QueryInterface( IXThreadingImpl *iface, REFIID iid, void **out )
{
    struct x_threading *impl = impl_from_IXThreadingImpl( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown        ) ||
        IsEqualGUID( iid, &IID_IXThreadingImpl ))
    {
        IXThreadingImpl_AddRef( *out = &impl->IXThreadingImpl_iface );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI x_threading_AddRef( IXThreadingImpl *iface )
{
    struct x_threading *impl = impl_from_IXThreadingImpl( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI x_threading_Release( IXThreadingImpl *iface )
{
    struct x_threading *impl = impl_from_IXThreadingImpl( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI x_threading_XAsyncGetStatus( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, BOOLEAN wait )
{
    TRACE( "iface %p, asyncBlock %p, wait %d.\n", iface, asyncBlock, wait );
    if (WaitForSingleObject( asyncBlock->internal[3], wait ? INFINITE : 0 ) == WAIT_TIMEOUT) return E_PENDING;
    return S_OK;
}

static HRESULT WINAPI x_threading_XAsyncGetResultSize( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, SIZE_T *bufferSize )
{
    TRACE( "iface %p, asyncBlock %p, bufferSize %p.\n", iface, asyncBlock, bufferSize );
    *bufferSize = ((XAsyncProviderData *)asyncBlock->internal[0])->bufferSize;
    return S_OK;
}

static void WINAPI x_threading_XAsyncCancel( IXThreadingImpl *iface, XAsyncBlock *asyncBlock )
{
    FIXME( "iface %p, asyncBlock %p stub!\n", iface, asyncBlock );
}

static HRESULT WINAPI run_provider( XAsyncOp op, const XAsyncProviderData *data )
{
    HRESULT hr = S_OK;
    switch (op)
    {
        case XAsyncOp_Begin:
            return IXThreadingImpl_XAsyncSchedule( x_threading_impl, data->async, 0 );
        case XAsyncOp_DoWork:
            hr = ((XAsyncWork *)data->context)( data->async );
            IXThreadingImpl_XAsyncComplete( x_threading_impl, data->async, hr, 0 );
            hr = S_OK;
        case XAsyncOp_GetResult:
        case XAsyncOp_Cancel:
        case XAsyncOp_Cleanup:
            break;
    }
    return hr;
}

static HRESULT WINAPI x_threading_XAsyncRun( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, XAsyncWork *work )
{
    TRACE( "iface %p, asyncBlock %p, work %p.\n", iface, asyncBlock, work );
    return IXThreadingImpl_XAsyncBegin( iface, asyncBlock, work, NULL, "XAsyncRun", run_provider );
}

static HRESULT WINAPI x_threading_XAsyncBegin( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, void *context, const void *identity, const char *identityName, XAsyncProvider *provider )
{
    XAsyncProviderData *data;
    HRESULT hr;

    TRACE( "iface %p, asyncBlock %p, context %p, identity %p, identityName %s, provider %p.\n", iface, asyncBlock, context, identity, identityName, provider );

    if (!(data = calloc( 1, sizeof(*data) ))) return E_OUTOFMEMORY;
    if (!asyncBlock->queue && FAILED(hr = IXThreadingImpl_XTaskQueueGetCurrentProcessTaskQueue( iface, &asyncBlock->queue )))
    {
        free( data );
        return hr;
    }
    asyncBlock->internal[0] = data;
    asyncBlock->internal[1] = provider;
    asyncBlock->internal[2] = (void *)identity;
    asyncBlock->internal[3] = CreateEventA( NULL, TRUE, FALSE, NULL );
    data->async = asyncBlock;
    data->context = context;
    return provider( XAsyncOp_Begin, data );
}

static HRESULT WINAPI __PADDING__( IXThreadingImpl *iface )
{
    WARN( "iface %p padding function called! It's unknown what this function does.\n", iface );
    return E_NOTIMPL;
}

static void CALLBACK async_work_callback( XAsyncBlock *asyncBlock, BOOLEAN canceled )
{
    ((XAsyncProvider *)asyncBlock->internal[1])( canceled ? XAsyncOp_Cancel : XAsyncOp_DoWork, asyncBlock->internal[0] );
}

static HRESULT WINAPI x_threading_XAsyncSchedule( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, UINT32 delayInMs )
{
    TRACE( "iface %p, asyncBlock %p, delayInMs %d.\n", iface, asyncBlock, delayInMs );
    return IXThreadingImpl_XTaskQueueSubmitDelayedCallback( iface, asyncBlock->queue, XTaskQueuePort_Work, delayInMs, asyncBlock, (XTaskQueueCallback *)async_work_callback );
}

static void CALLBACK async_completion_callback( XAsyncBlock *asyncBlock, BOOLEAN canceled )
{
    if (canceled) ((XAsyncProvider *)asyncBlock->internal[1])( XAsyncOp_Cancel, asyncBlock->internal[0] );
    else asyncBlock->callback( asyncBlock );
    ((XAsyncProvider *)asyncBlock->internal[1])( XAsyncOp_Cleanup, asyncBlock->internal[0] );
    SetEvent( asyncBlock->internal[3] );
}

static void WINAPI x_threading_XAsyncComplete( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, HRESULT result, SIZE_T requiredBufferSize )
{
    TRACE( "iface %p, asyncBlock %p, result %#lx, requiredBufferSize %Iu.\n", iface, asyncBlock, result, requiredBufferSize );
    ((XAsyncProviderData *)asyncBlock->internal[0])->bufferSize = requiredBufferSize;
    if (SUCCEEDED(result) && asyncBlock->callback)
        IXThreadingImpl_XTaskQueueSubmitCallback( iface, asyncBlock->queue, XTaskQueuePort_Completion, asyncBlock, (XTaskQueueCallback *)async_completion_callback );
    else
    {
        ((XAsyncProvider *)asyncBlock->internal[1])( XAsyncOp_Cleanup, asyncBlock->internal[0] );
        SetEvent( asyncBlock->internal[3] );
        free( asyncBlock->internal[0] );
    }
}

static HRESULT WINAPI x_threading_XAsyncGetResult( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, const void *identity, SIZE_T bufferSize, void *buffer, SIZE_T *bufferUsed )
{
    HRESULT hr;
    TRACE( "iface %p asyncBlock %p, identity %p, bufferSize %Iu, buffer %p, bufferUsed %p.\n", iface, asyncBlock, identity, bufferSize, buffer, bufferUsed );
    if (bufferSize < ((XAsyncProviderData *)asyncBlock->internal[0])->bufferSize) return HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER );
    ((XAsyncProviderData *)asyncBlock->internal[0])->buffer = buffer;
    if (SUCCEEDED(hr = ((XAsyncProvider *)asyncBlock->internal[1])( XAsyncOp_GetResult, asyncBlock->internal[0] )))
        *bufferUsed = ((XAsyncProviderData *)asyncBlock->internal[0])->bufferSize;
    else *bufferUsed = 0;
    return hr;
}

static HRESULT WINAPI x_threading_XTaskQueueCreate( IXThreadingImpl *iface, XTaskQueueDispatchMode workDispatchMode, XTaskQueueDispatchMode completionDispatchMode, XTaskQueueHandle *queue )
{
    FIXME( "iface %p, workDispatchMode %d, completionDispatchMode %d, queue %p stub!\n", iface, workDispatchMode, completionDispatchMode, queue );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_threading_XTaskQueueCreateComposite( IXThreadingImpl *iface, XTaskQueuePortHandle workPort, XTaskQueuePortHandle completionPort, XTaskQueueHandle *queue )
{
    FIXME( "iface %p, workPort %p, completionPort %p, queue %p stub!\n", iface, workPort, completionPort, queue );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_threading_XTaskQueueGetPort( IXThreadingImpl *iface, XTaskQueueHandle queue, XTaskQueuePort port, XTaskQueuePortHandle *portHandle )
{
    FIXME( "iface %p, queue %p, port %d, portHandle %p stub!\n", iface, queue, port, portHandle );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_threading_XTaskQueueDuplicateHandle( IXThreadingImpl *iface, XTaskQueueHandle queueHandle, XTaskQueueHandle *duplicatedHandle )
{
    FIXME( "iface %p, queueHandle %p, duplicatedHandle %p stub!\n", iface, queueHandle, duplicatedHandle );
    return E_NOTIMPL;
}

static BOOLEAN WINAPI x_threading_XTaskQueueDispatch( IXThreadingImpl *iface, XTaskQueueHandle queue, XTaskQueuePort port, UINT32 timeoutInMs )
{
    FIXME( "iface %p, queue %p, port %d, timeoutInMs %d stub!\n", iface, queue, port, timeoutInMs );
    return FALSE;
}

static void WINAPI x_threading_XTaskQueueCloseHandle( IXThreadingImpl *iface, XTaskQueueHandle queue )
{
    FIXME( "iface %p, queue %p stub!\n", iface, queue );
}

static HRESULT WINAPI x_threading_XTaskQueueSubmitCallback( IXThreadingImpl *iface, XTaskQueueHandle queue, XTaskQueuePort port, void *callbackContext, XTaskQueueCallback *callback )
{
    FIXME( "iface %p, queue %p, port %d, callbackContext %p, callback %p stub!\n", iface, queue, port, callbackContext, callback );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_threading_XTaskQueueSubmitDelayedCallback( IXThreadingImpl *iface, XTaskQueueHandle queue, XTaskQueuePort port, UINT32 delayMs, void *callbackContext, XTaskQueueCallback *callback )
{
    FIXME( "iface %p, queue %p, port %d, delayMs %d, callbackContext %p, callback %p stub!\n", iface, queue, port, delayMs, callbackContext, callback );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_threading_XTaskQueueRegisterWaiter( IXThreadingImpl *iface, XTaskQueueHandle queue, XTaskQueuePort port, HANDLE waitHandle, void *callbackContext, XTaskQueueCallback *callback, XTaskQueueRegistrationToken *token )
{
    FIXME( "iface %p, queue %p, port %d, waitHandle %p, callbackContext %p, callback %p, token %p stub!\n", iface, queue, port, waitHandle, callbackContext, callback, token );
    return E_NOTIMPL;
}

static void WINAPI x_threading_XTaskQueueUnregisterWaiter( IXThreadingImpl *iface, XTaskQueueHandle queue, XTaskQueueRegistrationToken token )
{
    FIXME( "iface %p, queue %p, token %p stub!\n", iface, queue, &token );
}

static HRESULT WINAPI x_threading_XTaskQueueTerminate( IXThreadingImpl *iface, XTaskQueueHandle queue, BOOLEAN wait, void *callbackContext, XTaskQueueTerminatedCallback *callback )
{
    FIXME( "iface %p, queue %p, wait %d, callbackContext %p, callback %p stub!\n", iface, queue, wait, callbackContext, callback );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_threading_XTaskQueueRegisterMonitor( IXThreadingImpl *iface, XTaskQueueHandle queue, void *callbackContext, XTaskQueueMonitorCallback *callback, XTaskQueueRegistrationToken *token )
{
    FIXME( "iface %p, queue %p, callbackContext %p, callback %p, token %p stub!\n", iface, queue, callbackContext, callback, token );
    return E_NOTIMPL;
}

static void WINAPI x_threading_XTaskQueueUnregisterMonitor( IXThreadingImpl *iface, XTaskQueueHandle queue, XTaskQueueRegistrationToken token )
{
    FIXME( "iface %p, queue %p, token %p stub!\n", iface, queue, &token );
}

static BOOLEAN WINAPI x_threading_XTaskQueueGetCurrentProcessTaskQueue( IXThreadingImpl *iface, XTaskQueueHandle *queue )
{
    FIXME( "iface %p, queue %p stub!\n", iface, queue );
    return FALSE;
}

static void WINAPI x_threading_XTaskQueueSetCurrentProcessTaskQueue( IXThreadingImpl *iface, XTaskQueueHandle queue )
{
    FIXME( "iface %p, queue %p stub!\n", iface, queue );
}

static HRESULT WINAPI x_threading_XThreadSetTimeSensitive( IXThreadingImpl *iface, BOOLEAN isTimeSensitiveThread )
{
    TRACE( "iface %p, isTimeSensitiveThread %d.\n", iface, isTimeSensitiveThread );
    if (!TlsSetValue( tlsIndex, (void *)(UINT_PTR)isTimeSensitiveThread )) return HRESULT_FROM_WIN32( GetLastError() );
    return S_OK;
}

static void WINAPI x_threading_XThreadAssertNotTimeSensitive( IXThreadingImpl *iface )
{
    TRACE( "iface %p.\n", iface );
    if (TlsGetValue( tlsIndex )) DebugBreak();
}

static BOOLEAN WINAPI x_threading_XThreadIsTimeSensitive( IXThreadingImpl *iface )
{
    TRACE( "iface %p.\n", iface );
    return TlsGetValue( tlsIndex ) ? 1 : 0;
}

static const struct IXThreadingImplVtbl x_threading_vtbl =
{
    x_threading_QueryInterface,
    x_threading_AddRef,
    x_threading_Release,
    /* IXThreadingImpl methods */
    x_threading_XAsyncGetStatus,
    x_threading_XAsyncGetResultSize,
    x_threading_XAsyncCancel,
    x_threading_XAsyncRun,
    x_threading_XAsyncBegin,
    __PADDING__,
    x_threading_XAsyncSchedule,
    x_threading_XAsyncComplete,
    x_threading_XAsyncGetResult,
    x_threading_XTaskQueueCreate,
    x_threading_XTaskQueueCreateComposite,
    x_threading_XTaskQueueGetPort,
    x_threading_XTaskQueueDuplicateHandle,
    x_threading_XTaskQueueDispatch,
    x_threading_XTaskQueueCloseHandle,
    x_threading_XTaskQueueSubmitCallback,
    x_threading_XTaskQueueSubmitDelayedCallback,
    x_threading_XTaskQueueRegisterWaiter,
    x_threading_XTaskQueueUnregisterWaiter,
    x_threading_XTaskQueueTerminate,
    x_threading_XTaskQueueRegisterMonitor,
    x_threading_XTaskQueueUnregisterMonitor,
    x_threading_XTaskQueueGetCurrentProcessTaskQueue,
    x_threading_XTaskQueueSetCurrentProcessTaskQueue,
    x_threading_XThreadSetTimeSensitive,
    __PADDING__,
    x_threading_XThreadAssertNotTimeSensitive,
    x_threading_XThreadIsTimeSensitive
};

static struct x_threading x_threading =
{
    {&x_threading_vtbl},
    0,
};

IXThreadingImpl *x_threading_impl = &x_threading.IXThreadingImpl_iface;
