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

struct XTaskQueuePortObject
{
    IUnknown IUnknown_iface;
    LONG ref;
    LONG queued;
    XTaskQueueDispatchMode mode;
    TP_WORK **queue;
    SIZE_T capacity;
    SIZE_T length;
    HANDLE ready;
    HANDLE terminating;
    HANDLE terminated;
};

static inline XTaskQueuePortHandle port_impl_from_IUnknown( IUnknown *iface )
{
    return CONTAINING_RECORD( iface, struct XTaskQueuePortObject, IUnknown_iface );
}

static ULONG WINAPI port_AddRef( IUnknown *iface )
{
    XTaskQueuePortHandle impl = port_impl_from_IUnknown( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI port_Release( IUnknown *iface )
{
    XTaskQueuePortHandle impl = port_impl_from_IUnknown( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    if (!ref) free( impl );
    return ref;
}

static const struct IUnknownVtbl port_vtbl =
{
    NULL,
    port_AddRef,
    port_Release,
};

struct XTaskQueueObject
{
    IUnknown IUnknown_iface;
    LONG ref;
    XTaskQueuePortHandle work;
    XTaskQueuePortHandle completion;
};

static inline XTaskQueueHandle queue_impl_from_IUnknown( IUnknown *iface )
{
    return CONTAINING_RECORD( iface, struct XTaskQueueObject, IUnknown_iface );
}

static ULONG WINAPI queue_AddRef( IUnknown *iface )
{
    XTaskQueueHandle impl = queue_impl_from_IUnknown( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI queue_Release( IUnknown *iface )
{
    XTaskQueueHandle impl = queue_impl_from_IUnknown( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    if (!ref)
    {
        IUnknown_Release( &impl->work->IUnknown_iface );
        IUnknown_Release( &impl->completion->IUnknown_iface );
        free( impl );
    }
    return ref;
}

static const struct IUnknownVtbl queue_vtbl =
{
    NULL,
    queue_AddRef,
    queue_Release,
};

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
    FIXME( "iface %p, asyncBlock %p, wait %d stub!\n", iface, asyncBlock, wait );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_threading_XAsyncGetResultSize( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, SIZE_T *bufferSize )
{
    FIXME( "iface %p, asyncBlock %p, bufferSize %p stub!\n", iface, asyncBlock, bufferSize );
    return E_NOTIMPL;
}

static void WINAPI x_threading_XAsyncCancel( IXThreadingImpl *iface, XAsyncBlock *asyncBlock )
{
    FIXME( "iface %p, asyncBlock %p stub!\n", iface, asyncBlock );
}

static HRESULT WINAPI x_threading_XAsyncRun( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, XAsyncWork *work )
{
    FIXME( "iface %p, asyncBlock %p, work %p stub!\n", iface, asyncBlock, work );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_threading_XAsyncBegin( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, void *context, const void *identity, const char *identityName, XAsyncProvider *provider )
{
    FIXME( "iface %p, asyncBlock %p, context %p, identity %p, identityName %s, provider %p stub!\n", iface, asyncBlock, context, identity, identityName, provider );
    return E_NOTIMPL;
}

static HRESULT WINAPI __PADDING__( IXThreadingImpl *iface )
{
    WARN( "iface %p padding function called! It's unknown what this function does.\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_threading_XAsyncSchedule( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, UINT32 delayInMs )
{
    FIXME( "iface %p, asyncBlock %p, delayInMs %d stub!\n", iface, asyncBlock, delayInMs );
    return E_NOTIMPL;
}

static void WINAPI x_threading_XAsyncComplete( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, HRESULT result, SIZE_T requiredBufferSize )
{
    FIXME( "iface %p, asyncBlock %p, result %#lx, requiredBufferSize %Iu stub!\n", iface, asyncBlock, result, requiredBufferSize );
}

static HRESULT WINAPI x_threading_XAsyncGetResult( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, const void *identity, SIZE_T bufferSize, void *buffer, SIZE_T *bufferUsed )
{
    FIXME( "iface %p asyncBlock %p, identity %p, bufferSize %Iu, buffer %p, bufferUsed %p stub!\n", iface, asyncBlock, identity, bufferSize, buffer, bufferUsed );
    return E_NOTIMPL;
}

static void CALLBACK serialized_worker( TP_CALLBACK_INSTANCE *instance, XTaskQueuePortHandle port, TP_WORK *work )
{

}

static HRESULT create_port( XTaskQueueDispatchMode mode, XTaskQueuePortHandle *port )
{
    if (!(*port = calloc( 1, sizeof(**port) ))) return E_OUTOFMEMORY;
    (*port)->IUnknown_iface.lpVtbl = &port_vtbl;
    (*port)->ref = 1;
    (*port)->mode = mode;
    (*port)->terminating = CreateEventA( NULL, TRUE, FALSE, NULL );
    (*port)->terminated = CreateEventA( NULL, TRUE, FALSE, NULL );

    if (mode == XTaskQueueDispatchMode_SerializedThreadPool || mode == XTaskQueueDispatchMode_Manual)
    {
        (*port)->capacity = 32;
        if (!((*port)->queue = calloc( 32, sizeof(*(*port)->queue) )))
        {
            free( port );
            return E_OUTOFMEMORY;
        }
    }

    if (mode == XTaskQueueDispatchMode_SerializedThreadPool)
    {
        SubmitThreadpoolWork( CreateThreadpoolWork( (PTP_WORK_CALLBACK)serialized_worker, *port, NULL ) );
        (*port)->ready = CreateEventA( NULL, FALSE, FALSE, NULL );
    }

    return S_OK;
}

static HRESULT WINAPI x_threading_XTaskQueueCreate( IXThreadingImpl *iface, XTaskQueueDispatchMode workDispatchMode, XTaskQueueDispatchMode completionDispatchMode, XTaskQueueHandle *queue )
{
    HRESULT hr;
    TRACE( "iface %p, workDispatchMode %d, completionDispatchMode %d, queue %p.\n", iface, workDispatchMode, completionDispatchMode, queue );
    if (!(*queue = calloc( 1, sizeof(**queue) ))) return E_OUTOFMEMORY;
    (*queue)->IUnknown_iface.lpVtbl = &queue_vtbl;
    (*queue)->ref = 1;
    if (FAILED(hr = create_port( workDispatchMode, &(*queue)->work )))
    {
        IUnknown_Release( &(*queue)->IUnknown_iface );
        return hr;
    }
    if (FAILED(hr = create_port( completionDispatchMode, &(*queue)->completion )))
    {
        IUnknown_Release( &(*queue)->IUnknown_iface );
        return hr;
    }
    return S_OK;
}

static HRESULT WINAPI x_threading_XTaskQueueCreateComposite( IXThreadingImpl *iface, XTaskQueuePortHandle workPort, XTaskQueuePortHandle completionPort, XTaskQueueHandle *queue )
{
    TRACE( "iface %p, workPort %p, completionPort %p, queue %p.\n", iface, workPort, completionPort, queue );
    if (!(*queue = calloc( 1, sizeof(**queue) ))) return E_OUTOFMEMORY;
    (*queue)->ref = 1;
    (*queue)->work = workPort;
    (*queue)->completion = completionPort;
    IUnknown_AddRef( &workPort->IUnknown_iface );
    IUnknown_AddRef( &completionPort->IUnknown_iface );
    return S_OK;
}

static HRESULT WINAPI x_threading_XTaskQueueGetPort( IXThreadingImpl *iface, XTaskQueueHandle queue, XTaskQueuePort port, XTaskQueuePortHandle *portHandle )
{
    TRACE( "iface %p, queue %p, port %d, portHandle %p.\n", iface, queue, port, portHandle );
    switch (port)
    {
        case XTaskQueuePort_Work:
            *portHandle = queue->work;
            return S_OK;
        case XTaskQueuePort_Completion:
            *portHandle = queue->completion;
            return S_OK;
    }
    return E_INVALIDARG;
}

static HRESULT WINAPI x_threading_XTaskQueueDuplicateHandle( IXThreadingImpl *iface, XTaskQueueHandle queueHandle, XTaskQueueHandle *duplicatedHandle )
{
    TRACE( "iface %p, queueHandle %p, duplicatedHandle %p.\n", iface, queueHandle, duplicatedHandle );
    IUnknown_AddRef( &queueHandle->IUnknown_iface );
    *duplicatedHandle = queueHandle;
    return S_OK;
}

static BOOLEAN WINAPI x_threading_XTaskQueueDispatch( IXThreadingImpl *iface, XTaskQueueHandle queue, XTaskQueuePort port, UINT32 timeoutInMs )
{
    TRACE( "iface %p, queue %p, port %d, timeoutInMs %d.\n", iface, queue, port, timeoutInMs );
    return FALSE;
}

static void WINAPI x_threading_XTaskQueueCloseHandle( IXThreadingImpl *iface, XTaskQueueHandle queue )
{
    TRACE( "iface %p, queue %p.\n", iface, queue );
    IUnknown_Release( &queue->IUnknown_iface );
}

struct dispatch_context
{
    XTaskQueuePortHandle port;
    XTaskQueueCallback *callback;
    void *callbackContext;
    UINT32 delay;
};

static void CALLBACK dispatch_handler( TP_CALLBACK_INSTANCE *instance, struct dispatch_context *context, TP_WORK *work )
{
    Sleep( context->delay );
    context->callback( context->callbackContext, !WaitForSingleObject( context->port->terminating, 0 ) );
    if (!InterlockedDecrement( &context->port->queued ) && !WaitForSingleObject( context->port->terminating, 0 ))
        SetEvent( context->port->terminated );
}

static HRESULT WINAPI x_threading_XTaskQueueSubmitCallback( IXThreadingImpl *iface, XTaskQueueHandle queue, XTaskQueuePort port, void *callbackContext, XTaskQueueCallback *callback )
{
    return IXThreadingImpl_XTaskQueueSubmitDelayedCallback( iface, queue, port, 0, callbackContext, callback );
}

static HRESULT WINAPI x_threading_XTaskQueueSubmitDelayedCallback( IXThreadingImpl *iface, XTaskQueueHandle queue, XTaskQueuePort port, UINT32 delayMs, void *callbackContext, XTaskQueueCallback *callback )
{
    struct dispatch_context *work_context;
    XTaskQueuePortHandle handle;
    PTP_WORK newQueue;
    TP_WORK *work;

    FIXME( "iface %p, queue %p, port %d, delayMs %d, callbackContext %p, callback %p semi-stub!\n", iface, queue, port, delayMs, callbackContext, callback );

    switch (port)
    {
        case XTaskQueuePort_Work:
            handle = queue->work;
            break;
        case XTaskQueuePort_Completion:
            handle = queue->completion;
            break;
        default:
            return E_INVALIDARG;
    }

    if (handle->mode == XTaskQueueDispatchMode_Immediate)
    {
        InterlockedIncrement( &handle->queued );
        Sleep( delayMs );
        callback( callbackContext, !WaitForSingleObject( handle->terminating, 0 ));
        if (!InterlockedDecrement( &handle->queued ) && !WaitForSingleObject( handle->terminating, 0 ))
            SetEvent( handle->terminated );
        return S_OK;
    }

    if (!(work_context = calloc( 1, sizeof(*work_context) ))) return E_OUTOFMEMORY;
    work_context->port = handle;
    work_context->callback = callback;
    work_context->callbackContext = callbackContext;
    work_context->delay = delayMs;
    if (!(work = CreateThreadpoolWork( (PTP_WORK_CALLBACK)dispatch_handler, work_context, NULL )))
    {
        free( work_context );
        return HRESULT_FROM_WIN32( GetLastError() );
    }

    if (handle->mode == XTaskQueueDispatchMode_ThreadPool)
    {
        InterlockedIncrement( &handle->queued );
        SubmitThreadpoolWork( work );
        return S_OK;
    }

    if (handle->capacity == handle->length)
    {
        handle->capacity *= 1.5;
        if (!(newQueue = realloc( handle->queue, handle->capacity * sizeof(*handle->queue) )))
        {
            IUnknown_Release( &handle->IUnknown_iface );
            free( work_context );
            return E_OUTOFMEMORY;
        }
    }

    handle->queue[handle->length++] = work;
    return S_OK;
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
    TRACE( "iface %p, queue %p, wait %d, callbackContext %p, callback %p.\n", iface, queue, wait, callbackContext, callback );

    SetEvent( queue->work->terminating );
    SetEvent( queue->completion->terminating );
    if (wait)
    {
        if (queue->work->queued) WaitForSingleObject( queue->work->terminated, INFINITE );
        if (queue->completion->queued) WaitForSingleObject( queue->completion->terminated, INFINITE );
    }

    IUnknown_Release( &queue->IUnknown_iface );
    return S_OK;
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
    TRACE( "iface %p, queue %p.\n", iface, queue );
    if (!processQueue) return FALSE;
    IUnknown_AddRef( &processQueue->IUnknown_iface );
    *queue = processQueue;
    return TRUE;
}

static void WINAPI x_threading_XTaskQueueSetCurrentProcessTaskQueue( IXThreadingImpl *iface, XTaskQueueHandle queue )
{
    TRACE( "iface %p, queue %p.\n", iface, queue );
    if (processQueue) IUnknown_Release( &processQueue->IUnknown_iface );
    if (queue) IUnknown_AddRef( &queue->IUnknown_iface );
    processQueue = queue;
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
