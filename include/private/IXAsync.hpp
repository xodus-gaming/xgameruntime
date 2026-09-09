/*
 * XAsync C++ Wrapper
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

// TODO: Create XAsyncAction for result-less operations.

#ifndef XGAMERUNTIME_ASYNCWRAPPER_H
#define XGAMERUNTIME_ASYNCWRAPPER_H

#include <async.h>

#include <functional>
#include <mutex>
#include <atomic>

using namespace ABI;
using namespace ABI::XGameRuntime;

typedef HRESULT (WINAPI *async_operation_callback)( IUnknown *invoker, PVOID param, PROPVARIANT *result );

template<typename T>
class XAsync
    : public IXAsync<T>
{
public:
    XAsync() = default;
    virtual ~XAsync() = default;

    /* IUnknown Methods */
    HRESULT WINAPI
    QueryInterface( REFIID iid, void** out ) noexcept override
    {
        TRACE( "iface %p, iid %s, out %p.\n", this, debugstr_guid( &iid ), out );

        if (!out) return E_POINTER;
        *out = nullptr;

        if ( iid == __uuidof( IUnknown ) ||
             iid == __uuidof( IInspectable ) ||
             iid == __uuidof( IAgileObject ) ||
             iid == __uuidof( IXAsync<T> ) )
        {
            AddRef();
            *out = static_cast<IXAsync<T> *>(this);
            return S_OK;
        }

        return E_NOTIMPL;
    }

    ULONG WINAPI
    AddRef() noexcept override
    {
        ULONG curr = static_cast<ULONG>(++ref);
        TRACE( "iface %p increasing refcount to %lu.\n", this, curr );
        return curr;
    }

    ULONG WINAPI
    Release() noexcept override
    {
        ULONG curr = static_cast<ULONG>(--ref);
        TRACE( "iface %p decreasing refcount to %lu.\n", this, curr );

        if ( !curr )
        {
            if ( completed )
            {
                completed->Release();
            }
            delete this;
        }

        return curr;
    }

    /* IInspectable Methods */
    HRESULT WINAPI
    GetIids( ULONG *iid_count, IID **iids ) noexcept override
    {
        TRACE( "iface %p, iid_count %p, iids %p\n", this, iid_count, iids );

        if ( !iid_count || !iids )
            return E_POINTER;

        *iid_count = 2;
        IID* allocated = static_cast<IID*>( CoTaskMemAlloc( sizeof(IID) * (*iid_count) ) );

        if ( !allocated )
            return E_OUTOFMEMORY;

        allocated[0] = __uuidof( IXAsync<T> );

        *iids = allocated;
        return S_OK;
    }

    HRESULT WINAPI
    GetRuntimeClassName( HSTRING *class_name ) noexcept override
    {
        TRACE( "iface %p, class_name %p\n", this, class_name );
        return WindowsCreateString( (LPCWSTR)L"Windows.Foundation.IAsyncAction`1<IInspectable>", 48, class_name );
    }

    HRESULT WINAPI
    GetTrustLevel( TrustLevel *trust_level ) noexcept override
    {
        FIXME( "iface %p, trust_level %p stub!\n", this, trust_level );
        return E_NOTIMPL;
    }

    /* IAsyncOperation<TResult> methods */
    HRESULT WINAPI
    put_Completed( IXAsyncOperationCompletedHandlerImpl *routine ) noexcept override
    {
        TRACE( "iface %p, routine %p\n", this, routine );

        // Keep the routine alive until invokation.
        routine->AddRef();
        completed = routine;

        return S_OK;
    }

    HRESULT WINAPI
    get_Completed( IXAsyncOperationCompletedHandlerImpl **routine ) noexcept override
    {
        TRACE( "iface %p, routine %p\n", this, routine );

        if ( completed )
        {
            completed->AddRef();
            *routine = completed;
        }

        return S_OK;
    }

    HRESULT WINAPI
    GetResults( BOOLEAN wait, T *results ) override
    {
        HRESULT hr;
        PROPVARIANT result;

        TRACE( "iface %p, results %p\n", this, results );

        hr = XAsyncGetStatus( &block, wait );
        if ( FAILED( hr ) ) return hr;

        hr = XAsyncGetResult( &block, (PVOID)Create, sizeof(PROPVARIANT *), &result, nullptr );
        if ( FAILED( hr ) ) return hr;

        if ( result.vt == VT_UNKNOWN )
            *results = static_cast<T>(result.punkVal);

        PropVariantClear( &result );
        return S_OK;
    }

    /* Internal methods */
    static HRESULT WINAPI
    Create( IUnknown *invoker, PVOID context, XTaskQueueHandle queue, async_operation_callback work,
                IXAsync<T> **out )
    {
        HRESULT hr = S_OK;;
        XAsync<T> *impl = new XAsync<T>();
        AsyncContext *asyncctx = new AsyncContext();

        CallbackThunk completioncb = CallbackThunk( [&]( XAsyncBlock* async )
        {
            impl->AddRef();
            async_completion_callback( impl );
            impl->Release();
            if ( !out )
                impl->Release(); //self destructing operation.
        } );

        TRACE( "invoker %p, context %p, queue %p, work %p, out %p\n", invoker, context, queue, work, out );

        impl->block.queue = queue;
        impl->block.context = static_cast<PVOID>( &completioncb );
        impl->block.callback = CallbackThunk::Callback;

        invoker->AddRef();
        asyncctx->invoker = invoker;
        asyncctx->context = context;
        asyncctx->work = work;
        PropVariantInit( &asyncctx->result );

        hr = XAsyncBegin( &impl->block, asyncctx, (PVOID)Create, __FUNCTION__, async_worker_callback );
        if ( FAILED( hr ) )
        {
            PropVariantClear( &asyncctx->result );
            delete asyncctx;
            impl->Release();
            return hr;
        }

        if ( out )
            *out = impl;

        return hr;
    }

private:
    struct AsyncContext
    {
        HRESULT hr;
        PROPVARIANT result;
        async_operation_callback work;
        IUnknown *invoker;
        PVOID context;
    };

    class CallbackThunk
    {
    public:
        CallbackThunk() = default;

        explicit CallbackThunk( std::function<void(XAsyncBlock*)> func )
            : _func(func)
        {
        }

        static void CALLBACK Callback( XAsyncBlock* async )
        {
            const CallbackThunk* pthis = static_cast<CallbackThunk*>(async->context);
            pthis->_func( async );
        }

    private:
        std::function<void(XAsyncBlock*)> _func;
    };

    static void CALLBACK
    async_completion_callback( IUnknown* iface )
    {
        XAsync *impl = static_cast<XAsync *>( iface );
        if ( impl->completed )
        {
            {
                const std::lock_guard<std::mutex> lock( impl->lock );
                impl->completed->Invoke( &impl->block );
                impl->completed->Release();
                impl->completed = nullptr;
            }
        }
    }

    static HRESULT CALLBACK
    async_worker_callback( XAsyncOp opCode, const XAsyncProviderData* data )
    {
        AsyncContext* ctx = static_cast<AsyncContext*>(data->context);
        HRESULT hr = S_OK;

        switch (opCode)
        {
        case XAsyncOp::Begin:
            hr = XAsyncSchedule( data->async, 0 );
            break;

        case XAsyncOp::Cancel:
            // Cancellation not supported yet.
            break;

        case XAsyncOp::Cleanup:
            ctx->invoker->Release();
            PropVariantClear( &ctx->result );
            delete ctx;
            break;

        case XAsyncOp::GetResult:
            // xgameruntime functions should do their own propvariant size allocation.
            PropVariantCopy( static_cast<PROPVARIANT*>(data->buffer), &ctx->result );
            break;

        case XAsyncOp::DoWork:
            hr = ctx->work( ctx->invoker, ctx->context, &ctx->result );
            XAsyncComplete( data->async, hr, sizeof(PROPVARIANT *) );
            break;
        }

        return hr;
    }

    std::atomic_long ref{ 1 };
    std::mutex lock;
    IXAsyncOperationCompletedHandlerImpl *completed = nullptr;
    XAsyncBlock block{};
};

#endif
