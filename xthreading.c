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

#include <wine/list.h>

WINE_DEFAULT_DEBUG_CHANNEL(gdkc);

/*
 * XTaskQueue is the dispatch substrate the whole GDK async surface sits on:
 * every X*Async call takes an XAsyncBlock whose ->queue decides where the work
 * runs and where the completion callback is delivered. While XTaskQueueCreate
 * was a stub returning E_NOTIMPL, callers got a NULL queue, so no completion
 * could ever be delivered and games waited forever on results that could not
 * arrive (Minecraft stalls at 41% on exactly this, waiting for XUserAddAsync).
 *
 * A queue owns two ports -- Work and Completion -- each with its own dispatch
 * mode. Callbacks submitted to a port are run according to that mode.
 */

struct task_item
{
    struct list entry;
    void *context;
    XTaskQueueCallback *callback;
};

struct task_port
{
    struct task_queue *queue;
    XTaskQueuePort id;
    XTaskQueueDispatchMode mode;
    struct list items;
    HANDLE ready;                   /* signalled while items is non-empty */
    CRITICAL_SECTION serialize_cs;  /* SerializedThreadPool: one at a time */
};

struct task_monitor
{
    struct list entry;
    UINT64 token;
    void *context;
    XTaskQueueMonitorCallback *callback;
};

struct task_queue
{
    struct list entry;      /* in live_queues, so handles can be validated */
    LONG ref;
    BOOL terminated;
    CRITICAL_SECTION cs;
    struct task_port owned_ports[2];
    /* Where work actually goes. Normally the ports above; for a composite
     * queue, the donor queues' ports. Everything else works through these, so
     * a composite genuinely shares its donors' queues rather than quietly
     * running a second set beside them. */
    struct task_port *ports[2];
    /* A composite borrows its ports and must not destroy them. */
    BOOL composite;
    struct list monitors;
    UINT64 next_token;
};

/* A delayed submission outlives the call that created it, so it carries its
 * own reference on the queue. */
struct delayed_item
{
    struct task_queue *queue;
    XTaskQueuePort port;
    void *context;
    XTaskQueueCallback *callback;
    PTP_TIMER timer;
};

static struct task_queue *process_queue;
static CRITICAL_SECTION process_queue_cs;
static CRITICAL_SECTION_DEBUG process_queue_cs_debug =
{
    0, 0, &process_queue_cs,
    { &process_queue_cs_debug.ProcessLocksList, &process_queue_cs_debug.ProcessLocksList },
    0, 0, { (DWORD_PTR)(__FILE__ ": process_queue_cs") }
};
static CRITICAL_SECTION process_queue_cs = { &process_queue_cs_debug, -1, 0, 0, 0, 0 };

/* Every queue this module has handed out and not yet destroyed.
 *
 * A handle is an opaque pointer, and turning one back into a queue used to be a
 * cast. That trusts the caller completely: a title passing a stale or
 * uninitialised handle had its value dereferenced, and
 * XTaskQueueDuplicateHandle then ran InterlockedIncrement straight through it.
 *
 * Titles do exactly that while shutting down -- Expedition 33 passes a handle
 * pointing into its own .text, so the refcount increment wrote to the game's
 * code section and took the process down with an access violation. The real
 * runtime validates handles and answers E_INVALIDARG, so the crash only ever
 * appeared here.
 *
 * Membership is checked rather than assumed. The list holds a handful of
 * entries, so a walk costs nothing next to the work each call goes on to do. */
static struct list live_queues = LIST_INIT( live_queues );
static CRITICAL_SECTION live_queues_cs;
static CRITICAL_SECTION_DEBUG live_queues_cs_debug =
{
    0, 0, &live_queues_cs,
    { &live_queues_cs_debug.ProcessLocksList, &live_queues_cs_debug.ProcessLocksList },
    0, 0, { (DWORD_PTR)(__FILE__ ": live_queues_cs") }
};
static CRITICAL_SECTION live_queues_cs = { &live_queues_cs_debug, -1, 0, 0, 0, 0 };

static void register_queue( struct task_queue *queue )
{
    EnterCriticalSection( &live_queues_cs );
    list_add_tail( &live_queues, &queue->entry );
    LeaveCriticalSection( &live_queues_cs );
}

static void unregister_queue( struct task_queue *queue )
{
    EnterCriticalSection( &live_queues_cs );
    list_remove( &queue->entry );
    LeaveCriticalSection( &live_queues_cs );
}

static struct task_queue *queue_from_handle( XTaskQueueHandle handle )
{
    struct task_queue *queue, *found = NULL;

    if (!handle) return NULL;

    EnterCriticalSection( &live_queues_cs );
    LIST_FOR_EACH_ENTRY( queue, &live_queues, struct task_queue, entry )
    {
        if (queue != (struct task_queue *)handle) continue;
        found = queue;
        break;
    }
    LeaveCriticalSection( &live_queues_cs );

    /* Reported rather than traced: a title handing over a handle this module
     * never issued is the difference between a call doing its job and one
     * failing for a reason nothing else will explain, and WARN is off by
     * default so it would go unseen exactly when it matters. */
    if (!found)
        ERR( "task queue handle %p was not issued by this runtime (caller %p).\n",
             handle, __builtin_return_address(0) );
    return found;
}

static inline XTaskQueueHandle handle_from_queue( struct task_queue *queue )
{
    return (XTaskQueueHandle)queue;
}

/* Ports live inside queues, so a port handle is valid exactly when some live
 * queue owns it. */
static struct task_port *port_from_handle( XTaskQueuePortHandle handle )
{
    struct task_queue *queue;
    struct task_port *found = NULL;
    unsigned int i;

    if (!handle) return NULL;

    EnterCriticalSection( &live_queues_cs );
    LIST_FOR_EACH_ENTRY( queue, &live_queues, struct task_queue, entry )
    {
        for (i = 0; i < ARRAY_SIZE(queue->owned_ports); i++)
        {
            if (&queue->owned_ports[i] != (struct task_port *)handle) continue;
            found = &queue->owned_ports[i];
            break;
        }
        if (found) break;
    }
    LeaveCriticalSection( &live_queues_cs );

    if (!found) ERR( "task queue port handle %p was not issued by this runtime.\n", handle );
    return found;
}

static void task_queue_addref( struct task_queue *queue )
{
    InterlockedIncrement( &queue->ref );
}

static void task_queue_release( struct task_queue *queue )
{
    struct task_monitor *monitor, *monitor_next;
    struct task_item *item, *item_next;
    unsigned int i;

    if (InterlockedDecrement( &queue->ref )) return;

    TRACE( "destroying queue %p.\n", queue );

    /* Off the list before the memory goes, so a handle to it stops validating
     * at exactly the point it stops being usable. */
    unregister_queue( queue );

    /* A composite borrowed its ports; destroying them here would take down the
     * queues it was built over. */
    for (i = 0; !queue->composite && i < ARRAY_SIZE(queue->owned_ports); i++)
    {
        /* Anything still pending is reported as cancelled, which is what the
         * GDK contract promises: every submitted callback runs exactly once,
         * with canceled=TRUE if it never got a chance to do its work. */
        LIST_FOR_EACH_ENTRY_SAFE( item, item_next, &queue->owned_ports[i].items, struct task_item, entry )
        {
            list_remove( &item->entry );
            item->callback( item->context, TRUE );
            free( item );
        }
        CloseHandle( queue->owned_ports[i].ready );
        queue->owned_ports[i].serialize_cs.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection( &queue->owned_ports[i].serialize_cs );
    }

    /* Give back the donor references taken when the composite was created.
     * Taken before the monitors are torn down so the pointers are still
     * readable, and released after this queue is off the live list. */
    if (queue->composite)
    {
        struct task_queue *work = queue->ports[XTaskQueuePort_Work]->queue;
        struct task_queue *completion = queue->ports[XTaskQueuePort_Completion]->queue;

        queue->ports[XTaskQueuePort_Work] = NULL;
        queue->ports[XTaskQueuePort_Completion] = NULL;
        task_queue_release( work );
        task_queue_release( completion );
    }

    LIST_FOR_EACH_ENTRY_SAFE( monitor, monitor_next, &queue->monitors, struct task_monitor, entry )
    {
        list_remove( &monitor->entry );
        free( monitor );
    }

    queue->cs.DebugInfo->Spare[0] = 0;
    DeleteCriticalSection( &queue->cs );
    free( queue );
}

/* Monitors are told when work lands on a port; a Manual-mode host uses this to
 * learn it should go call XTaskQueueDispatch. Callbacks run outside the lock. */
static void task_queue_notify_monitors( struct task_queue *queue, XTaskQueuePort port )
{
    XTaskQueueMonitorCallback *callbacks[16];
    void *contexts[16];
    unsigned int count = 0, i;
    struct task_monitor *monitor;

    EnterCriticalSection( &queue->cs );
    LIST_FOR_EACH_ENTRY( monitor, &queue->monitors, struct task_monitor, entry )
    {
        if (count == ARRAY_SIZE(callbacks)) break;
        callbacks[count] = monitor->callback;
        contexts[count] = monitor->context;
        count++;
    }
    LeaveCriticalSection( &queue->cs );

    for (i = 0; i < count; i++) callbacks[i]( contexts[i], handle_from_queue( queue ), port );
}

static struct task_item *task_port_pop( struct task_port *port )
{
    struct task_queue *queue = port->queue;
    struct task_item *item = NULL;
    struct list *entry;

    EnterCriticalSection( &queue->cs );
    if ((entry = list_head( &port->items )))
    {
        item = LIST_ENTRY( entry, struct task_item, entry );
        list_remove( &item->entry );
    }
    if (list_empty( &port->items )) ResetEvent( port->ready );
    LeaveCriticalSection( &queue->cs );

    return item;
}

static void task_port_run_one( struct task_port *port )
{
    struct task_item *item;

    if (port->mode == XTaskQueueDispatchMode_SerializedThreadPool)
        EnterCriticalSection( &port->serialize_cs );

    if ((item = task_port_pop( port )))
    {
        item->callback( item->context, port->queue->terminated );
        free( item );
    }

    if (port->mode == XTaskQueueDispatchMode_SerializedThreadPool)
        LeaveCriticalSection( &port->serialize_cs );
}

static void CALLBACK task_port_threadpool_cb( PTP_CALLBACK_INSTANCE instance, void *context )
{
    struct task_port *port = context;
    struct task_queue *queue = port->queue;

    task_port_run_one( port );
    task_queue_release( queue );  /* paired with the addref at submit time */
}

static HRESULT task_port_submit_ex( struct task_port *port, void *context,
                                    XTaskQueueCallback *callback, BOOL force )
{
    struct task_queue *queue = port->queue;
    struct task_item *item;

    /* force is for the termination notice, which is by definition queued after
     * the queue has been terminated. */
    if (queue->terminated && !force)
    {
        ERR( "refusing work for terminated queue %p port %d.\n", queue, port->id );
        return E_ABORT;
    }

    /* Immediate never queues: it runs on the submitting thread, so there is
     * nothing to allocate and nothing for a monitor to come collect. */
    if (port->mode == XTaskQueueDispatchMode_Immediate)
    {
        callback( context, FALSE );
        return S_OK;
    }

    if (!(item = calloc( 1, sizeof(*item) ))) return E_OUTOFMEMORY;
    item->context = context;
    item->callback = callback;

    EnterCriticalSection( &queue->cs );
    list_add_tail( &port->items, &item->entry );
    SetEvent( port->ready );
    LeaveCriticalSection( &queue->cs );

    task_queue_notify_monitors( queue, port->id );

    if (port->mode == XTaskQueueDispatchMode_ThreadPool ||
        port->mode == XTaskQueueDispatchMode_SerializedThreadPool)
    {
        task_queue_addref( queue );
        if (!TrySubmitThreadpoolCallback( task_port_threadpool_cb, port, NULL ))
        {
            /* Fall back to running inline rather than losing the callback:
             * a dropped completion is an unresolvable hang for the caller. */
            WARN( "TrySubmitThreadpoolCallback failed, running inline.\n" );
            task_queue_release( queue );
            task_port_run_one( port );
        }
    }

    return S_OK;
}

static HRESULT task_port_submit( struct task_port *port, void *context, XTaskQueueCallback *callback )
{
    return task_port_submit_ex( port, context, callback, FALSE );
}

static void CALLBACK delayed_item_cb( PTP_CALLBACK_INSTANCE instance, void *context, PTP_TIMER timer )
{
    struct delayed_item *delayed = context;
    struct task_queue *queue = delayed->queue;

    task_port_submit( queue->ports[delayed->port], delayed->context, delayed->callback );

    CloseThreadpoolTimer( delayed->timer );
    free( delayed );
    task_queue_release( queue );
}

/* ---------------------------------------------------------------------- */
/*  XAsync                                                                 */
/* ---------------------------------------------------------------------- */

/*
 * XAsyncBlock exposes internal[4] pointers of caller-owned scratch space; we
 * keep a pointer to our own state in internal[0] and never touch the rest.
 */
struct async_state
{
    LONG ref;
    XAsyncBlock *async;
    void *context;
    const void *identity;
    const char *identity_name;
    XAsyncProvider *provider;
    XAsyncWork *work;             /* XAsyncRun only */
    HRESULT status;
    SIZE_T required_size;
    HANDLE completed;             /* manual-reset: set once status is final */
};

/*
 * internal[] is the caller's memory, not ours.
 *
 * Nothing guarantees a title zeroes it, and titles reuse blocks: Beast of
 * Reincarnation cancels stale async blocks while shutting down, and one of them
 * held -1, so XAsyncCancel dereferenced (struct async_state *)-1 and read
 * address 0xFFFFFFFFFFFFFFFF. Keep a signature beside the pointer and only
 * believe the pointer when the signature is there.
 */
#define ASYNC_BLOCK_SIGNATURE ((void *)(ULONG_PTR)0x584f44555341ull)  /* "XODUSA" */

static inline struct async_state *async_state_from_block( XAsyncBlock *async )
{
    if (!async || async->internal[1] != ASYNC_BLOCK_SIGNATURE) return NULL;
    return async->internal[0];
}

/* Stop believing this block: its state is about to go away. */
static inline void async_block_invalidate( XAsyncBlock *async )
{
    if (!async) return;
    async->internal[0] = NULL;
    async->internal[1] = NULL;
}

static void async_state_release( struct async_state *state )
{
    if (InterlockedDecrement( &state->ref )) return;

    if (state->provider)
    {
        XAsyncProviderData data = { state->async, 0, NULL, state->context };
        state->provider( XAsyncOp_Cleanup, &data );
    }
    CloseHandle( state->completed );
    free( state );
}

static HRESULT async_state_create( XAsyncBlock *async, void *context, const void *identity,
                                   const char *identity_name, XAsyncProvider *provider,
                                   struct async_state **out )
{
    struct async_state *state;

    if (!async) return E_INVALIDARG;
    if (async_state_from_block( async )) return E_INVALIDARG;  /* already begun */

    if (!(state = calloc( 1, sizeof(*state) ))) return E_OUTOFMEMORY;
    if (!(state->completed = CreateEventW( NULL, TRUE, FALSE, NULL )))
    {
        free( state );
        return HRESULT_FROM_WIN32( GetLastError() );
    }

    state->ref = 1;
    state->async = async;
    state->context = context;
    state->identity = identity;
    state->identity_name = identity_name;
    state->provider = provider;
    state->status = E_PENDING;

    async->internal[0] = state;
    async->internal[1] = ASYNC_BLOCK_SIGNATURE;
    *out = state;
    return S_OK;
}

/*
 * A null XAsyncBlock.queue is not an error: the GDK falls back to the process
 * task queue, creating a default one on first use. Minecraft Bedrock relies on
 * this -- it never calls XTaskQueueSetCurrentProcessTaskQueue and leaves
 * async->queue null for every XUser call.
 */
/*
 * The process task queue always exists.
 *
 * A title only calls XTaskQueueSetCurrentProcessTaskQueue to *replace* the
 * default; the GDK provides one either way, and both XSAPI and the async engine
 * fall back to it whenever a caller leaves a queue null. Reporting "no process
 * queue" instead is fatal well before anything is dispatched: XblInitialize with
 * a null XblInitArgs.queue answers E_NO_TASK_QUEUE (0x800701AB), which is where
 * Asphalt Legends' Xbox Live setup stops -- so it never signs in, never asks for
 * a token, and never opens a socket.
 */
static struct task_queue *process_queue_get_or_create(void)
{
    struct task_queue *queue;

    EnterCriticalSection( &process_queue_cs );
    if (!(queue = process_queue))
    {
        XTaskQueueHandle handle;

        if (SUCCEEDED(IXThreadingImpl_XTaskQueueCreate( x_threading_impl, XTaskQueueDispatchMode_ThreadPool,
                                                        XTaskQueueDispatchMode_ThreadPool, &handle )))
        {
            TRACE( "created the default process task queue.\n" );
            queue = process_queue = queue_from_handle( handle );
        }
        else ERR( "could not create the default process task queue.\n" );
    }
    LeaveCriticalSection( &process_queue_cs );

    return queue;
}

static struct task_queue *async_resolve_queue( XAsyncBlock *async )
{
    struct task_queue *queue;

    if (!async->queue) return process_queue_get_or_create();
    if ((queue = queue_from_handle( async->queue ))) return queue;

    /* The block names a queue this runtime never issued.
     *
     * Dereferencing it is what used to take the process down -- Expedition 33
     * quit with an access violation writing into its own .text. Refusing is no
     * better, only quieter: the completion is dropped and the title waits for
     * a callback that can never arrive, which is the shutdown that hangs at
     * 100% of one core with every other thread idle.
     *
     * So deliver it on the process queue, which is where a block that names no
     * queue goes anyway. The foreign pointer is never touched, and the caller
     * still gets its callback. */
    ERR( "async block %p names task queue %p, which this runtime never issued; "
         "completing on the process queue instead.\n", async, async->queue );
    return process_queue_get_or_create();
}

/* Deliver the caller's completion routine on the queue's completion port. */
static void CALLBACK async_completion_cb( void *context, BOOLEAN canceled )
{
    struct async_state *state = context;
    XAsyncBlock *async = state->async;

    if (async->callback) async->callback( async );
    async_state_release( state );
}

/* Follow one operation end to end without logging every other one.
 *
 * A title's shutdown waits on libHttpClient's cleanup operations, and the
 * question is only ever whether those get driven: they are named, so match on
 * the name and leave the rest at TRACE. */
static BOOL async_is_watched( const char *name )
{
    return name && strstr( name, "leanup" );  /* Cleanup / cleanup_async */
}

static void async_finish( struct async_state *state, HRESULT result, SIZE_T required_size )
{
    XAsyncBlock *async = state->async;
    struct task_queue *queue;

    if (async_is_watched( state->identity_name ))
        TRACE( "watch finish %p %s hr %#lx\n", async,
             debugstr_a( state->identity_name ), (unsigned long)result );
    TRACE( "async %p finishes: %s hr %#lx\n", async,
           debugstr_a( state->identity_name ), (unsigned long)result );
    state->status = result;
    state->required_size = required_size;
    SetEvent( state->completed );

    if (!async->callback) return;

    if ((queue = async_resolve_queue( async )))
    {
        /* The completion routine holds its own reference so the state cannot
         * be torn down between scheduling and running. */
        InterlockedIncrement( &state->ref );
        if (FAILED(task_port_submit( queue->ports[XTaskQueuePort_Completion], state, async_completion_cb )))
        {
            ERR( "completion port would not take async %p, calling back inline.\n", async );
            async_state_release( state );
            async->callback( async );
        }
    }
    else
    {
        /* No queue at all: the caller still expects to be told. Better to call
         * back inline than to leave them waiting on something that cannot
         * arrive. */
        ERR( "async %p has no task queue, completing inline.\n", async );
        async->callback( async );
    }
}

static void CALLBACK async_dowork_cb( void *context, BOOLEAN canceled )
{
    struct async_state *state = context;
    XAsyncProviderData data = { state->async, 0, NULL, state->context };
    HRESULT hr;

    if (canceled)
    {
        async_finish( state, E_ABORT, 0 );
        async_state_release( state );
        return;
    }

    if (state->work) hr = state->work( state->async );
    else hr = state->provider( XAsyncOp_DoWork, &data );
    if (async_is_watched( state->identity_name ))
        TRACE( "watch dowork %p %s -> %#lx%s\n", state->async,
             debugstr_a( state->identity_name ), (unsigned long)hr,
             hr == E_PENDING ? " (provider will complete it later)" : "" );
    TRACE( "async %p work: %s -> %#lx\n", state->async,
           debugstr_a( state->identity_name ), (unsigned long)hr );

    /* E_PENDING means the provider will call XAsyncComplete itself later. */
    if (hr != E_PENDING) async_finish( state, hr, 0 );

    async_state_release( state );
}

HRESULT xasync_complete_static( XAsyncBlock *async, HRESULT result )
{
    struct async_state *state;
    HRESULT hr;

    if (FAILED(hr = async_state_create( async, NULL, NULL, NULL, NULL, &state ))) return hr;
    async_finish( state, result, 0 );
    return S_OK;
}

HRESULT xasync_peek_status( XAsyncBlock *async )
{
    struct async_state *state = async_state_from_block( async );

    if (!state) return E_INVALIDARG;
    return state->status;
}

/* ---------------------------------------------------------------------- */

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
    struct async_state *state = async_state_from_block( asyncBlock );

    TRACE( "iface %p, asyncBlock %p, wait %d.\n", iface, asyncBlock, wait );

    if (!state) return E_INVALIDARG;
    if (wait) WaitForSingleObject( state->completed, INFINITE );
    return state->status;
}

static HRESULT WINAPI x_threading_XAsyncGetResultSize( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, SIZE_T *bufferSize )
{
    struct async_state *state = async_state_from_block( asyncBlock );

    TRACE( "iface %p, asyncBlock %p, bufferSize %p.\n", iface, asyncBlock, bufferSize );

    if (!state || !bufferSize) return E_INVALIDARG;
    *bufferSize = state->required_size;
    /* Report the operation's own failure: a caller that sized a buffer and got
     * S_OK would go on to ask for a result that was never produced. */
    return state->status;
}

static void WINAPI x_threading_XAsyncCancel( IXThreadingImpl *iface, XAsyncBlock *asyncBlock )
{
    struct async_state *state = async_state_from_block( asyncBlock );

    TRACE( "iface %p, asyncBlock %p.\n", iface, asyncBlock );

    if (!state || state->status != E_PENDING) return;

    if (state->provider)
    {
        XAsyncProviderData data = { asyncBlock, 0, NULL, state->context };
        state->provider( XAsyncOp_Cancel, &data );
    }
    async_finish( state, E_ABORT, 0 );
}

static HRESULT WINAPI x_threading_XAsyncRun( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, XAsyncWork *work )
{
    struct async_state *state;
    HRESULT hr;

    TRACE( "iface %p, asyncBlock %p, work %p.\n", iface, asyncBlock, work );

    if (!work) return E_INVALIDARG;
    if (FAILED(hr = async_state_create( asyncBlock, NULL, NULL, NULL, NULL, &state ))) return hr;
    state->work = work;

    return IXThreadingImpl_XAsyncSchedule( iface, asyncBlock, 0 );
}

static HRESULT WINAPI x_threading_XAsyncBegin( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, void *context, const void *identity, const char *identityName, XAsyncProvider *provider )
{
    XAsyncProviderData data;
    struct async_state *state;
    HRESULT hr;

    TRACE( "iface %p, asyncBlock %p, context %p, identity %p, identityName %s, provider %p.\n",
           iface, asyncBlock, context, identity, debugstr_a( identityName ), provider );

    if (!provider) return E_INVALIDARG;
    if (FAILED(hr = async_state_create( asyncBlock, context, identity, identityName, provider, &state )))
        return hr;

    if (async_is_watched( identityName ))
        TRACE( "watch begin %p %s\n", asyncBlock, debugstr_a( identityName ) );
    TRACE( "async %p begins: %s\n", asyncBlock, debugstr_a( identityName ) );

    data.async = asyncBlock;
    data.bufferSize = 0;
    data.buffer = NULL;
    data.context = context;

    if (FAILED(hr = provider( XAsyncOp_Begin, &data )))
    {
        async_block_invalidate( asyncBlock );
        state->provider = NULL;  /* Begin failed: no Cleanup is owed */
        async_state_release( state );
        return hr;
    }

    return S_OK;
}

static HRESULT WINAPI __PADDING__( IXThreadingImpl *iface )
{
    WARN( "iface %p padding function called! It's unknown what this function does.\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI x_threading_XAsyncSchedule( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, UINT32 delayInMs )
{
    struct async_state *state = async_state_from_block( asyncBlock );
    struct task_queue *queue;
    HRESULT hr;

    TRACE( "iface %p, asyncBlock %p, delayInMs %d.\n", iface, asyncBlock, delayInMs );

    if (!state) return E_INVALIDARG;
    /* Whether the provider asks for work at all is the difference between it
     * waiting on something of its own and us failing to run what it queued. */
    if (async_is_watched( state->identity_name ))
        TRACE( "watch schedule %p %s delay %u\n", asyncBlock,
             debugstr_a( state->identity_name ), delayInMs );
    TRACE( "async %p scheduled: %s delay %u\n", asyncBlock,
           debugstr_a( state->identity_name ), delayInMs );
    if (!(queue = async_resolve_queue( asyncBlock ))) return E_GAMERUNTIME_INVALID_HANDLE;

    InterlockedIncrement( &state->ref );  /* released by async_dowork_cb */

    if (delayInMs)
        hr = IXThreadingImpl_XTaskQueueSubmitDelayedCallback( iface, handle_from_queue( queue ), XTaskQueuePort_Work,
                                                              delayInMs, state, async_dowork_cb );
    else
        hr = task_port_submit( queue->ports[XTaskQueuePort_Work], state, async_dowork_cb );

    if (FAILED(hr))
    {
        /* The work will never run, so nothing else is going to finish this
         * operation -- and XAsyncBegin having succeeded is a promise that the
         * completion routine runs exactly once, however it turns out.
         *
         * Dropping it here is what left titles waiting forever. A queue that
         * has been terminated refuses new work with E_ABORT, which is exactly
         * what happens while shutting down: Expedition 33 terminates its queue,
         * something schedules onto it, the operation vanishes, and the engine
         * spins on its message pump waiting for a callback that can never
         * arrive. async_finish falls back to calling back inline when the queue
         * cannot take it, so the caller is told either way. */
        async_finish( state, hr, 0 );
        async_state_release( state );  /* the reference taken for async_dowork_cb */
    }
    return hr;
}

static void WINAPI x_threading_XAsyncComplete( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, HRESULT result, SIZE_T requiredBufferSize )
{
    struct async_state *state = async_state_from_block( asyncBlock );

    TRACE( "iface %p, asyncBlock %p, result %#lx, requiredBufferSize %Iu.\n",
           iface, asyncBlock, result, requiredBufferSize );

    if (!state)
    {
        /* The caller says an operation finished and we cannot find it. There is
         * nobody left to tell, so the completion is lost and whoever is waiting
         * on it waits forever -- report it rather than returning in silence. */
        ERR( "XAsyncComplete for unknown async block %p (result %#lx); "
             "the completion cannot be delivered.\n", asyncBlock, (unsigned long)result );
        return;
    }
    if (async_is_watched( state->identity_name ))
        TRACE( "watch complete %p %s hr %#lx\n", asyncBlock,
             debugstr_a( state->identity_name ), (unsigned long)result );
    async_finish( state, result, requiredBufferSize );
}

static HRESULT WINAPI x_threading_XAsyncGetResult( IXThreadingImpl *iface, XAsyncBlock *asyncBlock, const void *identity, SIZE_T bufferSize, void *buffer, SIZE_T *bufferUsed )
{
    struct async_state *state = async_state_from_block( asyncBlock );
    XAsyncProviderData data;
    HRESULT hr;

    TRACE( "iface %p asyncBlock %p, identity %p, bufferSize %Iu, buffer %p, bufferUsed %p.\n",
           iface, asyncBlock, identity, bufferSize, buffer, bufferUsed );

    if (!state) return E_INVALIDARG;
    if (state->status == E_PENDING) return E_PENDING;
    if (FAILED(state->status)) return state->status;
    if (identity && state->identity && identity != state->identity) return E_INVALIDARG;

    if (bufferUsed) *bufferUsed = state->required_size;
    if (!state->provider) return state->status;
    if (bufferSize < state->required_size) return E_NOT_SUFFICIENT_BUFFER;

    data.async = asyncBlock;
    data.bufferSize = bufferSize;
    data.buffer = buffer;
    data.context = state->context;
    hr = state->provider( XAsyncOp_GetResult, &data );

    /* Results are consumed once; drop the block's reference now that the
     * caller has the payload. */
    if (SUCCEEDED(hr))
    {
        async_block_invalidate( asyncBlock );
        async_state_release( state );
    }
    return hr;
}

static HRESULT WINAPI x_threading_XTaskQueueCreate( IXThreadingImpl *iface, XTaskQueueDispatchMode workDispatchMode, XTaskQueueDispatchMode completionDispatchMode, XTaskQueueHandle *queue )
{
    XTaskQueueDispatchMode modes[2] = { workDispatchMode, completionDispatchMode };
    struct task_queue *impl;
    unsigned int i;

    TRACE( "iface %p, workDispatchMode %d, completionDispatchMode %d, queue %p.\n",
           iface, workDispatchMode, completionDispatchMode, queue );

    if (!queue) return E_INVALIDARG;
    if (workDispatchMode > XTaskQueueDispatchMode_Immediate ||
        completionDispatchMode > XTaskQueueDispatchMode_Immediate)
        return E_INVALIDARG;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->ref = 1;
    impl->next_token = 1;
    list_init( &impl->monitors );
    InitializeCriticalSectionEx( &impl->cs, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO );
    impl->cs.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": task_queue.cs");

    for (i = 0; i < ARRAY_SIZE(impl->ports); i++)
    {
        impl->ports[i] = &impl->owned_ports[i];
        impl->ports[i]->queue = impl;
        impl->ports[i]->id = i;
        impl->ports[i]->mode = modes[i];
        list_init( &impl->ports[i]->items );
        InitializeCriticalSectionEx( &impl->ports[i]->serialize_cs, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO );
        impl->ports[i]->serialize_cs.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": task_port.serialize_cs");
        if (!(impl->ports[i]->ready = CreateEventW( NULL, TRUE, FALSE, NULL )))
        {
            HRESULT hr = HRESULT_FROM_WIN32( GetLastError() );
            while (i--)
            {
                CloseHandle( impl->owned_ports[i].ready );
                DeleteCriticalSection( &impl->owned_ports[i].serialize_cs );
            }
            DeleteCriticalSection( &impl->cs );
            free( impl );
            return hr;
        }
    }

    register_queue( impl );

    *queue = handle_from_queue( impl );
    TRACE( "created queue %p.\n", impl );
    return S_OK;
}

static HRESULT WINAPI x_threading_XTaskQueueCreateComposite( IXThreadingImpl *iface, XTaskQueuePortHandle workPort, XTaskQueuePortHandle completionPort, XTaskQueueHandle *queue )
{
    struct task_port *work = port_from_handle( workPort ), *completion = port_from_handle( completionPort );
    struct task_queue *impl;

    TRACE( "iface %p, workPort %p, completionPort %p, queue %p.\n", iface, workPort, completionPort, queue );

    if (!work || !completion || !queue) return E_INVALIDARG;

    /* Alias the donor ports rather than standing up new ones.
     *
     * A mode-matched copy looked equivalent but was not: callbacks submitted
     * through the composite landed on ports nobody was pumping. Titles build a
     * composite over the process queue while shutting down and then wait for
     * its termination callback on the queue they are still dispatching --
     * Expedition 33 does exactly this, and hung at exit every time because the
     * callback went to the copy instead. */
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->ref = 1;
    impl->next_token = 1;
    impl->composite = TRUE;
    impl->ports[XTaskQueuePort_Work] = work;
    impl->ports[XTaskQueuePort_Completion] = completion;

    /* Hold the donors open for as long as this queue exists.
     *
     * A composite is built from ports belonging to other queues, and the GDK
     * keeps those queues alive on its behalf. Without that reference, closing a
     * donor frees it while these pointers still name its ports: terminating the
     * composite then sets an event in freed memory and queues its termination
     * notice onto a port nobody owns any more, so the callback the caller is
     * waiting for is never delivered. */
    task_queue_addref( work->queue );
    task_queue_addref( completion->queue );
    list_init( &impl->monitors );
    InitializeCriticalSectionEx( &impl->cs, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO );
    impl->cs.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": task_queue.cs");

    register_queue( impl );

    TRACE( "composite queue %p over work port %p and completion port %p.\n", impl, work, completion );
    *queue = (XTaskQueueHandle)impl;
    return S_OK;
}

static HRESULT WINAPI x_threading_XTaskQueueGetPort( IXThreadingImpl *iface, XTaskQueueHandle queue, XTaskQueuePort port, XTaskQueuePortHandle *portHandle )
{
    struct task_queue *impl = queue_from_handle( queue );

    TRACE( "iface %p, queue %p, port %d, portHandle %p.\n", iface, queue, port, portHandle );

    if (!impl || !portHandle) return E_INVALIDARG;
    if (port > XTaskQueuePort_Completion) return E_INVALIDARG;

    *portHandle = (XTaskQueuePortHandle)impl->ports[port];
    return S_OK;
}

static HRESULT WINAPI x_threading_XTaskQueueDuplicateHandle( IXThreadingImpl *iface, XTaskQueueHandle queueHandle, XTaskQueueHandle *duplicatedHandle )
{
    struct task_queue *impl = queue_from_handle( queueHandle );

    TRACE( "iface %p, queueHandle %p, duplicatedHandle %p.\n", iface, queueHandle, duplicatedHandle );

    if (!impl || !duplicatedHandle) return E_INVALIDARG;

    task_queue_addref( impl );
    *duplicatedHandle = queueHandle;
    return S_OK;
}

static BOOLEAN WINAPI x_threading_XTaskQueueDispatch( IXThreadingImpl *iface, XTaskQueueHandle queue, XTaskQueuePort port, UINT32 timeoutInMs )
{
    struct task_queue *impl = queue_from_handle( queue );
    struct task_item *item;

    TRACE( "iface %p, queue %p, port %d, timeoutInMs %d.\n", iface, queue, port, timeoutInMs );

    if (!impl || port > XTaskQueuePort_Completion) return FALSE;

    if (!(item = task_port_pop( impl->ports[port] )))
    {
        /* Nothing queued. If the queue has been terminated then nothing ever
         * will be, so waiting would be waiting forever -- and titles do pump a
         * terminated queue with an INFINITE timeout, expecting the termination
         * notice and then a prompt FALSE. task_port_pop resets the ready event
         * whenever it empties the list, so the event cannot stand in for this
         * check. */
        if (impl->terminated) return FALSE;
        if (WaitForSingleObject( impl->ports[port]->ready, timeoutInMs ) != WAIT_OBJECT_0) return FALSE;
        if (!(item = task_port_pop( impl->ports[port] ))) return FALSE;
    }

    item->callback( item->context, impl->terminated );
    free( item );
    return TRUE;
}

static void WINAPI x_threading_XTaskQueueCloseHandle( IXThreadingImpl *iface, XTaskQueueHandle queue )
{
    struct task_queue *impl = queue_from_handle( queue );

    TRACE( "iface %p, queue %p.\n", iface, queue );

    if (!impl) return;
    task_queue_release( impl );
}

static HRESULT WINAPI x_threading_XTaskQueueSubmitCallback( IXThreadingImpl *iface, XTaskQueueHandle queue, XTaskQueuePort port, void *callbackContext, XTaskQueueCallback *callback )
{
    struct task_queue *impl = queue_from_handle( queue );

    TRACE( "iface %p, queue %p, port %d, callbackContext %p, callback %p.\n",
           iface, queue, port, callbackContext, callback );

    if (!impl || !callback || port > XTaskQueuePort_Completion) return E_INVALIDARG;
    return task_port_submit( impl->ports[port], callbackContext, callback );
}

static HRESULT WINAPI x_threading_XTaskQueueSubmitDelayedCallback( IXThreadingImpl *iface, XTaskQueueHandle queue, XTaskQueuePort port, UINT32 delayMs, void *callbackContext, XTaskQueueCallback *callback )
{
    struct task_queue *impl = queue_from_handle( queue );
    struct delayed_item *delayed;
    LARGE_INTEGER due;
    FILETIME ft;

    TRACE( "iface %p, queue %p, port %d, delayMs %d, callbackContext %p, callback %p.\n",
           iface, queue, port, delayMs, callbackContext, callback );

    if (!impl || !callback || port > XTaskQueuePort_Completion) return E_INVALIDARG;
    if (!delayMs) return task_port_submit( impl->ports[port], callbackContext, callback );
    if (impl->terminated)
    {
        ERR( "delayed submit refused: queue %p is terminated (delay %ums).\n", impl, delayMs );
        return E_ABORT;
    }

    if (!(delayed = calloc( 1, sizeof(*delayed) ))) return E_OUTOFMEMORY;
    delayed->queue = impl;
    delayed->port = port;
    delayed->context = callbackContext;
    delayed->callback = callback;

    if (!(delayed->timer = CreateThreadpoolTimer( delayed_item_cb, delayed, NULL )))
    {
        free( delayed );
        return HRESULT_FROM_WIN32( GetLastError() );
    }

    task_queue_addref( impl );  /* released by delayed_item_cb */

    due.QuadPart = -((LONGLONG)delayMs * 10000);  /* relative, 100ns units */
    ft.dwLowDateTime = due.u.LowPart;
    ft.dwHighDateTime = due.u.HighPart;
    SetThreadpoolTimer( delayed->timer, &ft, 0, 0 );

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

/*
 * A terminated callback is not an XTaskQueueCallback -- it takes no "canceled"
 * flag -- so it rides the queue behind a trampoline.
 */
struct termination_notice
{
    XTaskQueueTerminatedCallback *callback;
    void *context;
};

static void CALLBACK termination_notice_cb( void *context, BOOLEAN canceled )
{
    struct termination_notice *notice = context;

    TRACE( "delivering termination notice %p (callback %p).\n", notice, notice->callback );
    notice->callback( notice->context );
    free( notice );
}

static HRESULT WINAPI x_threading_XTaskQueueTerminate( IXThreadingImpl *iface, XTaskQueueHandle queue, BOOLEAN wait, void *callbackContext, XTaskQueueTerminatedCallback *callback )
{
    struct task_queue *impl = queue_from_handle( queue );
    BOOL already_terminated = TRUE;
    struct task_item *item;
    unsigned int i;

    TRACE( "iface %p, queue %p, wait %d, callbackContext %p, callback %p.\n",
           iface, queue, wait, callbackContext, callback );

    if (!impl) return E_INVALIDARG;

    TRACE( "terminating queue %p (composite %d, wait %d, callback %p, already %d).\n",
           impl, impl->composite, wait, callback, impl->terminated );

    /* Terminating a queue that is already terminated must not terminate it
     * again. Titles do call this twice on the same queue -- observed on two
     * queues during Expedition 33's shutdown -- and re-running the drain
     * cancels callbacks submitted since the first call, which is work the
     * caller had every reason to expect would run.
     *
     * The notice below is still delivered for each call: a caller waiting on
     * its termination callback has to be told, whichever call it came from. */
    if (!impl->terminated)
    {
        already_terminated = FALSE;
        impl->terminated = TRUE;
    }

    /* Drain both ports, reporting each pending callback as cancelled.
     *
     * Not for a composite: its ports belong to the queues it was built over,
     * and cancelling their pending work would terminate more than the caller
     * asked for. The wake-up still happens, so anyone dispatching notices. */
    for (i = 0; i < ARRAY_SIZE(impl->ports); i++)
    {
        if (!impl->composite && !already_terminated)
        {
            while ((item = task_port_pop( impl->ports[i] )))
            {
                item->callback( item->context, TRUE );
                free( item );
            }
        }
        SetEvent( impl->ports[i]->ready );  /* wake anyone in XTaskQueueDispatch */
    }

    if (callback)
    {
        if (wait)
        {
            /* The caller blocks until termination is complete, so there is
             * nothing to gain by queueing -- and on a Manual port there may be
             * nobody left to dispatch it. */
            callback( callbackContext );
        }
        else
        {
            struct termination_notice *notice;

            /* Delivered through the completion port, not inline: this is what a
             * title is pumping for when it calls XTaskQueueDispatch after
             * terminating. Calling it inline here leaves that dispatch with
             * nothing to find, and it waits forever. */
            if (!(notice = calloc( 1, sizeof(*notice) ))) return E_OUTOFMEMORY;
            notice->callback = callback;
            notice->context = callbackContext;
            TRACE( "queueing termination notice for queue %p on completion port %p (mode %d, owner %p).\n",
                 impl, impl->ports[XTaskQueuePort_Completion],
                 impl->ports[XTaskQueuePort_Completion]->mode,
                 impl->ports[XTaskQueuePort_Completion]->queue );
            task_port_submit_ex( impl->ports[XTaskQueuePort_Completion], notice,
                                 termination_notice_cb, TRUE );
        }
    }
    return S_OK;
}

static HRESULT WINAPI x_threading_XTaskQueueRegisterMonitor( IXThreadingImpl *iface, XTaskQueueHandle queue, void *callbackContext, XTaskQueueMonitorCallback *callback, XTaskQueueRegistrationToken *token )
{
    struct task_queue *impl = queue_from_handle( queue );
    struct task_monitor *monitor;

    TRACE( "iface %p, queue %p, callbackContext %p, callback %p, token %p.\n",
           iface, queue, callbackContext, callback, token );

    if (!impl || !callback || !token) return E_INVALIDARG;
    if (!(monitor = calloc( 1, sizeof(*monitor) ))) return E_OUTOFMEMORY;

    monitor->context = callbackContext;
    monitor->callback = callback;

    EnterCriticalSection( &impl->cs );
    monitor->token = impl->next_token++;
    list_add_tail( &impl->monitors, &monitor->entry );
    LeaveCriticalSection( &impl->cs );

    token->token = monitor->token;
    return S_OK;
}

static void WINAPI x_threading_XTaskQueueUnregisterMonitor( IXThreadingImpl *iface, XTaskQueueHandle queue, XTaskQueueRegistrationToken token )
{
    struct task_queue *impl = queue_from_handle( queue );
    struct task_monitor *monitor, *next;

    TRACE( "iface %p, queue %p, token %I64u.\n", iface, queue, token.token );

    if (!impl) return;

    EnterCriticalSection( &impl->cs );
    LIST_FOR_EACH_ENTRY_SAFE( monitor, next, &impl->monitors, struct task_monitor, entry )
    {
        if (monitor->token != token.token) continue;
        list_remove( &monitor->entry );
        free( monitor );
        break;
    }
    LeaveCriticalSection( &impl->cs );
}

static BOOLEAN WINAPI x_threading_XTaskQueueGetCurrentProcessTaskQueue( IXThreadingImpl *iface, XTaskQueueHandle *queue )
{
    BOOLEAN found;

    struct task_queue *impl;

    TRACE( "iface %p, queue %p.\n", iface, queue );

    if (!queue) return FALSE;

    if (!(impl = process_queue_get_or_create())) return FALSE;
    task_queue_addref( impl );
    *queue = handle_from_queue( impl );
    found = TRUE;

    return found;
}

static void WINAPI x_threading_XTaskQueueSetCurrentProcessTaskQueue( IXThreadingImpl *iface, XTaskQueueHandle queue )
{
    struct task_queue *impl = queue_from_handle( queue ), *old;

    TRACE( "iface %p, queue %p.\n", iface, queue );

    EnterCriticalSection( &process_queue_cs );
    old = process_queue;
    if ((process_queue = impl)) task_queue_addref( impl );
    LeaveCriticalSection( &process_queue_cs );

    if (old) task_queue_release( old );
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
