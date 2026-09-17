/*
 * XThreading Tests
 *  From https://github.com/microsoft/libHttpClient
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


#include "../test_fixtures.h"

#include <thread>

#include <windows.h>
#include <xasync.h>
#include <xasyncprovider.h>
#include <xgameruntimeinit.h>

#define EXPECT_QUEUE_EMPTY(q) { EXPECT_TRUE(XTaskQueueIsEmpty(q, XTaskQueuePort::Completion)); EXPECT_TRUE(XTaskQueueIsEmpty(q, XTaskQueuePort::Work)); }

#define E_ILLEGAL_METHOD_CALL                              _HRESULT_TYPEDEF_(0x8000000E)

std::atomic<uint32_t> s_AsyncLibGlobalStateCount{0};

template <typename T>
class AutoRef
{
public:
    AutoRef(T* t)
    {
        Ref = t;
        Ref->AddRef();
    }

    ~AutoRef()
    {
        Ref->Release();
    }

    T* Ref;
};

class CompletionThunk
{
public:
    CompletionThunk(std::function<void(XAsyncBlock*)> func)
        : _func(func)
    {
    }

    static void CALLBACK Callback(XAsyncBlock* async)
    {
        const CompletionThunk* pthis = static_cast<CompletionThunk*>(async->context);
        pthis->_func(async);
    }

private:
    std::function<void(XAsyncBlock*)> _func;
};

class WorkThunk
{
public:
    WorkThunk(std::function<HRESULT(XAsyncBlock*)> func)
        : _func(func)
    {
    }

    static HRESULT CALLBACK Callback(XAsyncBlock* async)
    {
        const WorkThunk* pthis = static_cast<WorkThunk*>(async->context);
        return pthis->_func(async);
    }

private:
    std::function<HRESULT(XAsyncBlock*)> _func;
};


template <class T, class R>
class CallbackThunk
{
public:
    CallbackThunk(std::function<R(T)> func)
        : _func(func)
    {
    }

    static R Callback(void * context, T data)
    {
        const CallbackThunk<T, R>* pthis = static_cast<CallbackThunk<T, R>*>(context);
        return pthis->_func(data);
    }

private:

    std::function<R(T)> _func;
};

template <class T>
class CallbackThunk<T, void>
{
public:
    CallbackThunk(std::function<void(T)> func)
        : _func(func)
    {
    }

    static void Callback(void * context, T data)
    {
        const CallbackThunk<T, void>* pthis = static_cast<CallbackThunk<T, void>*>(context);
        pthis->_func(data);
    }

private:

    std::function<void(T)> _func;
};

template <>
class CallbackThunk<void, void>
{
public:
    CallbackThunk(std::function<void()> func)
        : _func(func)
    {
    }

    static void CALLBACK Callback(void * context, BOOLEAN)
    {
        const CallbackThunk<void, void>* pthis = static_cast<CallbackThunk<void, void>*>(context);
        pthis->_func();
    }

private:

    std::function<void()> _func;
};

template <class H, class C>
class AutoHandleWrapper
{
public:

    AutoHandleWrapper()
        : _handle(nullptr)
    {
    }

    AutoHandleWrapper(H h)
        : _handle(h)
    {
    }

    ~AutoHandleWrapper()
    {
        Close();
    }

    H Handle() const
    {
        return _handle;
    }

    void Close()
    {
        if (_handle != nullptr)
        {
            _closer(_handle);
            _handle = nullptr;
        }
    }

    AutoHandleWrapper& operator=(H h)
    {
        Close();
        _handle = h;
        return *this;
    }

    H* operator&()
    {
        return &_handle;
    }

    operator H() const
    {
        return _handle;
    }

    operator bool() const
    {
        return _handle != nullptr;
    }

    H Release()
    {
        H h = _handle;
        _handle = nullptr;
        return h;
    }

private:

    H _handle;
    C _closer;

};

struct QueueHandleCloser
{
    void operator ()(XTaskQueueHandle h)
    {
        XTaskQueueCloseHandle(h);
    }
};

typedef  AutoHandleWrapper<XTaskQueueHandle, QueueHandleCloser> AutoQueueHandle;

struct HandleCloser
{
    void operator()(HANDLE h)
    {
        CloseHandle(h);
    }
};

typedef AutoHandleWrapper<HANDLE, HandleCloser> AutoHandle;

class PumpedTaskQueue
{
public:
    XTaskQueueHandle queue = nullptr;

    PumpedTaskQueue()
    {
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

        XTaskQueueRegistrationToken token;
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueRegisterMonitor(queue, this, [](void* cxt, XTaskQueueHandle, XTaskQueuePort port)
        {
            PumpedTaskQueue* pthis = (PumpedTaskQueue*)cxt;
            if (port == XTaskQueuePort::Work)
            {
                pthis->Signal(pthis->workData);
            }
            else
            {
                pthis->Signal(pthis->completionData);
            }
        }, &token));

        workThread.reset(new std::thread([this] { WorkThreadProc(); }));
        completionThread.reset(new std::thread([this] { CompletionThreadProc(); }));
    }

    ~PumpedTaskQueue()
    {
        Shutdown(workData);
        Shutdown(completionData);

        workThread->join();
        completionThread->join();

        if (queue != nullptr)
        {
            XTaskQueueCloseHandle(queue);
        }
    }

private:

    struct NotifyData
    {
        std::mutex lock;
        std::condition_variable cv;
        bool notify = false;
    };

    bool WaitForNotify(NotifyData& notifyData)
    {
        std::unique_lock<std::mutex> l(notifyData.lock);

        while (true)
        {
            if (shutdown)
            {
                return false;
            }

            if (notifyData.notify)
            {
                notifyData.notify = false;
                return true;
            }

            notifyData.cv.wait(l);
        }
    }

    void Signal(NotifyData& notifyData)
    {
        std::unique_lock<std::mutex> l(notifyData.lock);
        notifyData.notify = true;
        notifyData.cv.notify_all();
    }

    void Shutdown(NotifyData& notifyData)
    {
        std::unique_lock<std::mutex> l(notifyData.lock);
        shutdown = true;
        notifyData.cv.notify_all();
    }

    void WorkThreadProc()
    {
        while (WaitForNotify(workData))
        {
            XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0);
        }
    }

    void CompletionThreadProc()
    {
        while (WaitForNotify(completionData))
        {
            XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0);
        }
    }

    std::unique_ptr<std::thread> workThread;
    std::unique_ptr<std::thread> completionThread;
    NotifyData workData;
    NotifyData completionData;
    bool shutdown = false;
};

struct XTaskQueueTestHooks
{
    virtual ~XTaskQueueTestHooks() = default;

    virtual void PendingEntriesRemovedDuringTermination(
        XTaskQueuePort port)
    {
        UNREFERENCED_PARAMETER(port);
    }

    virtual void NoNextPendingCallbackFound(
        XTaskQueuePort port,
        uint64_t dueTime)
    {
        UNREFERENCED_PARAMETER(port);
        UNREFERENCED_PARAMETER(dueTime);
    }

    virtual void NextPendingCallbackScheduled(
        XTaskQueuePort port,
        uint64_t lastDueTime,
        uint64_t nextDueTime)
    {
        UNREFERENCED_PARAMETER(port);
        UNREFERENCED_PARAMETER(lastDueTime);
        UNREFERENCED_PARAMETER(nextDueTime);
    }
};

class XThreadingTests : public XGameRuntimeTests
{
protected:
    XTaskQueueHandle queue{};

    struct FactorialCallData
    {
        DWORD value = 0;
        DWORD result = 0;
        DWORD iterationWait = 0;
        DWORD workThread = 0;

        // Fixed-capacity lock-free opcode log for concurrent append
        static constexpr SIZE_T MAX_OPCODES = 16;
        std::array<std::atomic<XAsyncOp>, MAX_OPCODES> opCodesArray{};
        std::atomic<SIZE_T> opCodesCount{0};

        std::atomic<int> inWork = 0;
        std::atomic<int> refs = 0;
        std::atomic<bool> canceled = false;

        void AddRef() { refs++; }
        void Release() { if (--refs == 0) delete this; }

        // Thread-safe append operation
        void RecordOp(XAsyncOp op)
        {
            size_t idx = opCodesCount.fetch_add(1, std::memory_order_relaxed);
            if (idx < MAX_OPCODES)
            {
                opCodesArray[idx].store(op, std::memory_order_release);
            }
            // Silently drop if overflow (test will fail on verification anyway)
        }

        // Snapshot current opcodes into a vector for verification
        std::vector<XAsyncOp> GetOpCodes() const
        {
            size_t count = opCodesCount.load(std::memory_order_acquire);
            count = (count < MAX_OPCODES) ? count : MAX_OPCODES;
            std::vector<XAsyncOp> result;
            result.reserve(count);
            for (size_t i = 0; i < count; i++)
            {
                result.push_back(opCodesArray[i].load(std::memory_order_acquire));
            }
            return result;
        }
    };

    static PCWSTR OpName(XAsyncOp op)
    {
        switch (op)
        {
        case XAsyncOp::Begin:
            return L"Begin";

        case XAsyncOp::GetResult:
            return L"GetResult";

        case XAsyncOp::Cleanup:
            return L"Cleanup";

        case XAsyncOp::DoWork:
            return L"DoWork";

        case XAsyncOp::Cancel:
            return L"Cancel";

        default:
            return L"Unknown";
        }
    }

    static HRESULT CALLBACK FactorialWorkerSimple(XAsyncOp opCode, const XAsyncProviderData* data)
    {
        FactorialCallData* d = (FactorialCallData*)data->context;
        HRESULT hr = S_OK;
        d->AddRef();

        d->RecordOp(opCode);

        switch (opCode)
        {
        case XAsyncOp::Begin:
            d->AddRef();
            break;

        case XAsyncOp::Cancel:
            d->canceled = true;
            break;

        case XAsyncOp::Cleanup:
            EXPECT_TRUE(d->inWork == 0);
            d->Release();
            break;

        case XAsyncOp::GetResult:
            CopyMemory(data->buffer, &d->result, sizeof(DWORD));
            break;

        case XAsyncOp::DoWork:
            d->inWork++;
            d->workThread = GetCurrentThreadId();
            d->result = 1;
            DWORD value = d->value;
            while (value)
            {
                d->result *= value;
                value--;
            }

            if (d->iterationWait != 0)
            {
                Sleep(d->iterationWait);
            }

            d->inWork--;
            XAsyncComplete(data->async, d->canceled ? E_ABORT : S_OK, sizeof(DWORD));
            break;
        }

        d->Release();
        return hr;
    }

    static HRESULT CALLBACK FactorialWorkerDistributed(XAsyncOp opCode, const XAsyncProviderData* data)
    {
        FactorialCallData* d = (FactorialCallData*)data->context;
        HRESULT hr = S_OK;
        d->AddRef();

        d->RecordOp(opCode);

        switch (opCode)
        {
        case XAsyncOp::Begin:
            d->AddRef();
            break;

        case XAsyncOp::Cancel:
            d->canceled = true;
            break;

        case XAsyncOp::Cleanup:
            EXPECT_TRUE(d->inWork == 0);
            d->Release();
            break;

        case XAsyncOp::GetResult:
            CopyMemory(data->buffer, &d->result, sizeof(DWORD));
            break;

        case XAsyncOp::DoWork:
            d->inWork++;
            d->workThread = GetCurrentThreadId();
            if (d->result == 0) d->result = 1;
            if (d->value != 0)
            {
                if (d->canceled)
                {
                    d->inWork--;
                    hr = E_ABORT;
                    break;
                }

                d->result *= d->value;
                d->value--;

                hr = XAsyncSchedule(data->async, d->iterationWait);
                d->inWork--;

                if (SUCCEEDED(hr))
                {
                    hr = E_PENDING;
                }
                break;
            }

            d->inWork--;
            XAsyncComplete(data->async, S_OK, sizeof(DWORD));
            break;
        }

        d->Release();
        return hr;
    }

    static HRESULT CALLBACK FactorialWorkerDistributedWithSchedule(XAsyncOp opCode, const XAsyncProviderData* data)
    {
        if (opCode == XAsyncOp::Begin)
        {
            // Must run the ctor for the newly allocated memory, and the initial
            // value has already been copied in here so we must rescue it.
            FactorialCallData* d = (FactorialCallData*)data->context;
            DWORD value = d->value;
            d = new(data->context) FactorialCallData;
            d->value = value;

            // leak a ref on this guy so we don't try to free it. We need
            // to do two addrefs because a new object starts with refcount
            // of zero.  The factorial async process will addref/release so
            // we need two to "leak" it (not really leaked; the memory is
            // owned by the async logic)

            d->AddRef();
            d->AddRef();
        }

        HRESULT hr = FactorialWorkerDistributed(opCode, data);

        if (SUCCEEDED(hr) && opCode == XAsyncOp::Begin)
        {
            hr = XAsyncSchedule(data->async, 0);
        }

        return hr;
    }

    static HRESULT FactorialAsync(FactorialCallData* data, XAsyncBlock* async)
    {
        HRESULT hr = XAsyncBegin(async, data, (PVOID)FactorialAsync, __FUNCTION__, FactorialWorkerSimple);
        if (SUCCEEDED(hr))
        {
            hr = XAsyncSchedule(async, 0);
        }
        return hr;
    }

    static HRESULT FactorialDistributedAsync(FactorialCallData* data, XAsyncBlock* async)
    {
        HRESULT hr = XAsyncBegin(async, data, (PVOID)FactorialAsync, __FUNCTION__, FactorialWorkerDistributed);
        if (SUCCEEDED(hr))
        {
            hr = XAsyncSchedule(async, 0);
        }
        return hr;
    }

    // Since XAsyncBeginAlloc is an undocumented function, testing it is not necessary.
    /**
    static HRESULT FactorialAllocateAsync(DWORD value, XAsyncBlock* async)
    {
        HRESULT hr = XAsyncBeginAlloc(async, FactorialAsync, __FUNCTION__, FactorialWorkerDistributedWithSchedule,
                                      sizeof(FactorialCallData), sizeof(DWORD), &value);
        return hr;
    }
    */

    static HRESULT FactorialResult(XAsyncBlock* async, _Out_writes_(1) DWORD* result)
    {
        size_t written;
        HRESULT hr = XAsyncGetResult(async, (PVOID)FactorialAsync, sizeof(DWORD), result, &written);
        if (SUCCEEDED(hr))
        {
            EXPECT_EQ(sizeof(DWORD), written);
        }
        return hr;
    }

    static void VerifyOps(const std::vector<XAsyncOp>& opsActual, const std::vector<XAsyncOp>& opsExpected)
    {
        size_t size = opsActual.size();
        EXPECT_EQ(size, opsExpected.size());

        for (size_t i = 0; i < size; i++)
        {
            EXPECT_EQ(opsActual[i], opsExpected[i]);
        }
    }

    static void VerifyHasOp(const std::vector<XAsyncOp>& opsActual, XAsyncOp expected)
    {
        bool found = false;

        for (auto op : opsActual)
        {
            if (op == expected)
            {
                found = true;
                break;
            }
        }

        EXPECT_TRUE(found);
    }

    static void SetUpTestSuite()
    {
        XGameRuntimeTests::SetUpTestSuite();
    }

    static void TearDownTestSuite()
    {
        XGameRuntimeTests::TearDownTestSuite();
    }

    void _VerifyRegisterWithAutoReset(XTaskQueueHandle queue)
    {
        HANDLE workEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        HANDLE completionEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        struct Context
        {
            HANDLE signaled;
            uint32_t count;
        };

        Context workContext;
        workContext.signaled = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        workContext.count = 0;

        Context completionContext;
        completionContext.signaled = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        completionContext.count = 0;

        auto cb = [](void* cxt, BOOLEAN)
        {
            Context* c = (Context*)cxt;
            c->count++;
            SetEvent(c->signaled);
        };

        XTaskQueueRegistrationToken workToken = {};
        XTaskQueueRegistrationToken completionToken = {};

        EXPECT_HRESULT_SUCCEEDED(XTaskQueueRegisterWaiter(queue, XTaskQueuePort::Work, workEvent, &workContext, cb, &workToken));
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueRegisterWaiter(queue, XTaskQueuePort::Completion, completionEvent, &completionContext,
                                                  cb, &completionToken));

        for (uint32_t idx = 1; idx <= 5; idx++)
        {
            SetEvent(workEvent);
            EXPECT_EQ((DWORD)WAIT_OBJECT_0, WaitForSingleObject(workContext.signaled, 1000));
            EXPECT_EQ((DWORD)WAIT_TIMEOUT, WaitForSingleObject(workContext.signaled, 100));
            EXPECT_EQ(idx, workContext.count);
        }

        for (uint32_t idx = 1; idx <= 5; idx++)
        {
            SetEvent(completionEvent);
            EXPECT_EQ((DWORD)WAIT_OBJECT_0, WaitForSingleObject(completionContext.signaled, 1000));
            EXPECT_EQ((DWORD)WAIT_TIMEOUT, WaitForSingleObject(completionContext.signaled, 100));
            EXPECT_EQ(idx, completionContext.count);
        }

        EXPECT_EQ(5u, workContext.count);
        EXPECT_EQ(5u, completionContext.count);

        XTaskQueueUnregisterWaiter(queue, workToken);
        XTaskQueueUnregisterWaiter(queue, completionToken);

        CloseHandle(workContext.signaled);
        CloseHandle(completionContext.signaled);
        CloseHandle(workEvent);
        CloseHandle(completionEvent);
    }

    void _VerifyQueueTermination(XTaskQueueHandle queue, bool wait, bool serialized, bool empty)
    {
        struct Data
        {
            std::atomic<uint32_t> workCount = {};
            std::atomic<uint32_t> completionCount = {};
            bool serialized;
            XTaskQueueHandle queue;
            XTaskQueueCallback* completionCallback;
        };

        auto workCb = [](void* cxt, BOOLEAN cancel)
        {
            Data* data = (Data*)cxt;

            if (data->serialized && !cancel)
            {
                // The very first work item may come in when we first start
                // the pump threads.  Sleep so we have a chance to enter
                // the termination code.
                EXPECT_EQ(0u, data->workCount);
                Sleep(1000);
            }

            data->workCount++;
            EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitCallback(data->queue, XTaskQueuePort::Completion, data,
                                                      data->completionCallback));
        };

        auto completionCb = [](void* cxt, BOOLEAN cancel)
        {
            Data* data = (Data*)cxt;
            EXPECT_TRUE(!data->serialized || cancel);
            data->completionCount++;
        };

        std::vector<HANDLE> events;

        Data data;
        data.serialized = serialized;
        data.queue = queue;
        data.completionCallback = completionCb;

        uint32_t normalCount = 0;
        uint32_t futureCount = 0;
        uint32_t eventCount = 0;

        if (!empty)
        {
            normalCount = 5;
            futureCount = 5;
            eventCount = 5;

            for (uint32_t idx = 0; idx < normalCount; idx++)
            {
                EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitCallback(queue, XTaskQueuePort::Work, &data, workCb));
            }

            for (uint32_t idx = 0; idx < futureCount; idx++)
            {
                EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 10000, &data, workCb));
            }

            for (uint32_t idx = 0; idx < eventCount; idx++)
            {
                HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                EXPECT_TRUE(evt != nullptr);
                events.push_back(evt);
                XTaskQueueRegistrationToken token;

                EXPECT_HRESULT_SUCCEEDED(XTaskQueueRegisterWaiter(queue, XTaskQueuePort::Work, evt, &data, workCb, &token));
            }
        }

        if (wait)
        {
            EXPECT_HRESULT_SUCCEEDED(XTaskQueueTerminate(queue, true, nullptr, nullptr));
        }
        else
        {
            HANDLE evt = CreateEvent(nullptr, TRUE, FALSE, nullptr);
            EXPECT_TRUE(evt != nullptr);

            auto termCb = [](void* cxt)
            {
                HANDLE h = (HANDLE)cxt;
                SetEvent(h);
            };

            EXPECT_HRESULT_SUCCEEDED(XTaskQueueTerminate(queue, false, evt, termCb));
            EXPECT_EQ((DWORD)WAIT_OBJECT_0, WaitForSingleObject(evt, 5000));
            CloseHandle(evt);
        }

        EXPECT_EQ(
            E_ABORT,
            XTaskQueueSubmitCallback(queue, XTaskQueuePort::Work, &data, workCb));

        EXPECT_EQ(
            E_ABORT,
            XTaskQueueSubmitCallback(queue, XTaskQueuePort::Completion, &data, completionCb));

        for (auto h : events)
        {
            CloseHandle(h);
        }

        uint32_t expectedCount = normalCount + futureCount + eventCount;
        if (!wait)
        {
            UINT64 ticks = GetTickCount64();
            while ((data.workCount.load() != expectedCount || data.completionCount.load() != expectedCount)
                && GetTickCount64() - ticks < 5000)
            {
                Sleep(10);
            }
        }
        EXPECT_EQ(expectedCount, data.workCount.load());
        EXPECT_EQ(expectedCount, data.completionCount.load());
    }

    static void NextStep(void* context, uint32_t expectedStep, PCWSTR stepName)
    {
        uint32_t* step = static_cast<uint32_t*>(context);
        EXPECT_EQ(expectedStep, *step);
        (*step)++;
    }

public:
    XThreadingTests()
    {
        EXPECT_HRESULT_SUCCEEDED(
            XTaskQueueCreate( XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue ));
    }

    ~XThreadingTests() override
    {
        EXPECT_EQ(s_AsyncLibGlobalStateCount, (DWORD)0);
        XTaskQueueTerminate(queue, true, nullptr, nullptr);
        XTaskQueueCloseHandle(queue);
    }
};

TEST_F(XThreadingTests, VerifySimpleAsyncCall)
{
    XAsyncBlock async = {};
    auto data = AutoRef<FactorialCallData>(new FactorialCallData{});
    DWORD result;
    std::vector<XAsyncOp> ops;

    CompletionThunk cb([&](XAsyncBlock* async)
    {
        EXPECT_HRESULT_SUCCEEDED(FactorialResult(async, &result));
    });

    async.context = &cb;
    async.callback = CompletionThunk::Callback;
    async.queue = queue;

    data.Ref->value = 5;

    EXPECT_HRESULT_SUCCEEDED(FactorialAsync(data.Ref, &async));
    EXPECT_HRESULT_SUCCEEDED(XAsyncGetStatus(&async, true));

    EXPECT_EQ(data.Ref->result, result);
    EXPECT_EQ(data.Ref->result, (DWORD)120);

    ops.push_back(XAsyncOp::Begin);
    ops.push_back(XAsyncOp::DoWork);
    ops.push_back(XAsyncOp::GetResult);
    ops.push_back(XAsyncOp::Cleanup);

    // Drain the queue before verifying opcodes to ensure cleanup has been recorded
    UINT64 drainTicks = GetTickCount64();
    while ((!XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion) || !XTaskQueueIsEmpty(queue, XTaskQueuePort::Work))
        && GetTickCount64() - drainTicks < 2000)
    {
        Sleep(10);
    }

    VerifyOps(data.Ref->GetOpCodes(), ops);
    EXPECT_QUEUE_EMPTY(queue);
}

TEST_F(XThreadingTests, VerifyMultipleCalls)
{
    const DWORD count = 10;
    XAsyncBlock async[count];
    FactorialCallData* data[count];
    DWORD completionCount = 0;

    for (int i = 0; i < count; i++)
    {
        data[i] = new FactorialCallData{};
    }

    CompletionThunk cb([&](XAsyncBlock* async)
    {
        DWORD result;
        EXPECT_HRESULT_SUCCEEDED(FactorialResult(async, &result));
        InterlockedIncrement(&completionCount);
    });

    ZeroMemory(async, sizeof(async));

    for (int idx = 0; idx < count; idx++)
    {
        async[idx].context = &cb;
        async[idx].callback = CompletionThunk::Callback;
        async[idx].queue = queue;
        data[idx]->value = 5 * (idx + 1);

        EXPECT_HRESULT_SUCCEEDED(FactorialAsync(data[idx], &async[idx]));
    }

    UINT64 ticks = GetTickCount64();
    while(completionCount != count && GetTickCount64() - ticks < 5000)
    {
        while(XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 100)) { }
    }

    EXPECT_EQ(count, completionCount);

    // Drain the queue before verifying it's empty to ensure all cleanup has been recorded
    UINT64 drainTicks = GetTickCount64();
    while ((!XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion) || !XTaskQueueIsEmpty(queue, XTaskQueuePort::Work))
        && GetTickCount64() - drainTicks < 2000)
    {
        Sleep(10);
    }

    // Note: FactorialCallData array elements were cleaned up by FactorialResult.
    EXPECT_QUEUE_EMPTY(queue);
}

TEST_F(XThreadingTests, VerifyDistributedAsyncCall)
{
    XAsyncBlock async = {};
    auto data = AutoRef<FactorialCallData>(new FactorialCallData{});
    DWORD result;
    std::vector<XAsyncOp> ops;

    CompletionThunk cb([&](XAsyncBlock* async)
    {
        EXPECT_HRESULT_SUCCEEDED(FactorialResult(async, &result));
    });

    async.context = &cb;
    async.callback = CompletionThunk::Callback;
    async.queue = queue;

    data.Ref->iterationWait = 100;
    data.Ref->value = 5;
    const DWORD initialValue = data.Ref->value;

    UINT64 ticks = GetTickCount64();
    EXPECT_HRESULT_SUCCEEDED(FactorialDistributedAsync(data.Ref, &async));
    EXPECT_HRESULT_SUCCEEDED(XAsyncGetStatus(&async, true));
    ticks = GetTickCount64() - ticks;
    XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0);

    EXPECT_EQ(data.Ref->result, result);
    EXPECT_EQ(data.Ref->result, (DWORD)120);

    // Iteration wait should have paused between each iteration (allow one interval of timer slack).
    const UINT64 expectedMinTicks = (static_cast<UINT64>(data.Ref->iterationWait) * initialValue) - data.Ref->
        iterationWait;
    EXPECT_GE(ticks, expectedMinTicks);

    ops.push_back(XAsyncOp::Begin);
    ops.push_back(XAsyncOp::DoWork);
    ops.push_back(XAsyncOp::DoWork);
    ops.push_back(XAsyncOp::DoWork);
    ops.push_back(XAsyncOp::DoWork);
    ops.push_back(XAsyncOp::DoWork);
    ops.push_back(XAsyncOp::DoWork);
    ops.push_back(XAsyncOp::GetResult);
    ops.push_back(XAsyncOp::Cleanup);

    // Drain the queue before verifying opcodes to ensure cleanup has been recorded
    UINT64 drainTicks = GetTickCount64();
    while ((!XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion) || !XTaskQueueIsEmpty(queue, XTaskQueuePort::Work))
        && GetTickCount64() - drainTicks < 2000)
    {
        Sleep(10);
    }

    VerifyOps(data.Ref->GetOpCodes(), ops);
    EXPECT_QUEUE_EMPTY(queue);
}

TEST_F(XThreadingTests, VerifyAsyncBlockReuse)
{
    // Specifically allow stack garbage here.
    XAsyncBlock async;

    auto data = AutoRef<FactorialCallData>(new FactorialCallData{});

    DWORD result;

    CompletionThunk cb([&](XAsyncBlock* async)
    {
        EXPECT_HRESULT_SUCCEEDED(FactorialResult(async, &result));
    });

    async.context = &cb;
    async.callback = CompletionThunk::Callback;
    async.queue = queue;

    data.Ref->value = 5;
    EXPECT_HRESULT_SUCCEEDED(FactorialAsync(data.Ref, &async));
    EXPECT_HRESULT_SUCCEEDED(XAsyncGetStatus(&async, true));

    EXPECT_EQ(result, (DWORD)120);

    // Now reuse the async block -- that should be fine.
    // Don't configure a callback so we can leave the
    // block open.

    async.callback = nullptr;
    data.Ref->value = 6;
    EXPECT_HRESULT_SUCCEEDED(FactorialAsync(data.Ref, &async));

    // It shoould NOT be fine to try to reuse it again before
    // we've pulled results.

    EXPECT_EQ(E_INVALIDARG, FactorialAsync(data.Ref, &async));

    EXPECT_HRESULT_SUCCEEDED(XAsyncGetStatus(&async, true));
    EXPECT_HRESULT_SUCCEEDED(FactorialResult(&async, &result));

    EXPECT_EQ(result, (DWORD)720);
}

TEST_F(XThreadingTests, VerifyCancellation)
{
    XAsyncBlock async = {};
    auto data = AutoRef<FactorialCallData>(new FactorialCallData{});
    HRESULT hrCallback = E_UNEXPECTED;

    CompletionThunk cb([&](XAsyncBlock* async)
    {
        hrCallback = XAsyncGetStatus(async, false);
    });

    async.context = &cb;
    async.callback = CompletionThunk::Callback;
    async.queue = queue;

    data.Ref->iterationWait = 100;
    data.Ref->value = 5;

    EXPECT_HRESULT_SUCCEEDED(FactorialDistributedAsync(data.Ref, &async));
    Sleep(100);
    EXPECT_EQ(XAsyncGetStatus(&async, false), E_PENDING);

    XAsyncCancel(&async);
    EXPECT_EQ(XAsyncGetStatus(&async, true), E_ABORT);
    EXPECT_EQ(E_ABORT, hrCallback);

    // Drain the queue before verifying opcodes to ensure cleanup has been recorded
    UINT64 drainTicks = GetTickCount64();
    while ((!XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion) || !XTaskQueueIsEmpty(queue, XTaskQueuePort::Work))
        && GetTickCount64() - drainTicks < 2000)
    {
        Sleep(10);
    }

    auto opCodes = data.Ref->GetOpCodes();
    VerifyHasOp(opCodes, XAsyncOp::Cancel);
    VerifyHasOp(opCodes, XAsyncOp::Cleanup);
    EXPECT_QUEUE_EMPTY(queue);
}

TEST_F(XThreadingTests, VerifyCleanupWaitsForWork)
{
    XAsyncBlock async = {};
    auto data = AutoRef<FactorialCallData>(new FactorialCallData{});
    DWORD result;

    CompletionThunk cb([&](XAsyncBlock* async)
    {
        EXPECT_EQ(FactorialResult(async, &result), E_ABORT);
    });

    async.context = &cb;
    async.callback = CompletionThunk::Callback;
    async.queue = queue;

    data.Ref->iterationWait = 500;
    data.Ref->value = 5;

    EXPECT_HRESULT_SUCCEEDED(FactorialAsync(data.Ref, &async));

    while (XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 50))
    {
    }
    EXPECT_EQ(XAsyncGetStatus(&async, false), E_PENDING);

    XAsyncCancel(&async);

    EXPECT_EQ(XAsyncGetStatus(&async, true), E_ABORT);

    // Drain the queue before verifying opcodes to ensure cleanup has been recorded
    UINT64 drainTicks = GetTickCount64();
    while ((!XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion) || !XTaskQueueIsEmpty(queue, XTaskQueuePort::Work))
        && GetTickCount64() - drainTicks < 2000)
    {
        Sleep(10);
    }

    auto opCodes = data.Ref->GetOpCodes();
    VerifyHasOp(opCodes, XAsyncOp::Cancel);
    VerifyHasOp(opCodes, XAsyncOp::Cleanup);
    VerifyHasOp(opCodes, XAsyncOp::DoWork);
    EXPECT_QUEUE_EMPTY(queue);
}

TEST_F(XThreadingTests, VerifyCleanupWaitsForWorkDistributed)
{
    XAsyncBlock async = {};
    auto data = AutoRef<FactorialCallData>(new FactorialCallData{});
    DWORD result;

    CompletionThunk cb([&](XAsyncBlock* async)
    {
        EXPECT_EQ(FactorialResult(async, &result), E_ABORT);
    });

    async.context = &cb;
    async.callback = CompletionThunk::Callback;
    async.queue = queue;

    data.Ref->iterationWait = 500;
    data.Ref->value = 5;

    EXPECT_HRESULT_SUCCEEDED(FactorialDistributedAsync(data.Ref, &async));

    while (XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 700))
    {
    }
    EXPECT_EQ(XAsyncGetStatus(&async, false), E_PENDING);
    XAsyncCancel(&async);

    EXPECT_EQ(XAsyncGetStatus(&async, true), E_ABORT);

    // Drain the queue before verifying opcodes to ensure cleanup has been recorded
    UINT64 drainTicks = GetTickCount64();
    while ((!XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion) || !XTaskQueueIsEmpty(queue, XTaskQueuePort::Work))
        && GetTickCount64() - drainTicks < 2000)
    {
        Sleep(10);
    }

    auto opCodes = data.Ref->GetOpCodes();
    VerifyHasOp(opCodes, XAsyncOp::Cancel);
    VerifyHasOp(opCodes, XAsyncOp::Cleanup);
    VerifyHasOp(opCodes, XAsyncOp::DoWork);
    EXPECT_QUEUE_EMPTY(queue);
}

TEST_F(XThreadingTests, VerifyRunAsync)
{
    XAsyncBlock async = {};
    HRESULT expected, result;

    WorkThunk cb([&](XAsyncBlock*)
    {
        return expected;
    });

    async.context = &cb;
    async.queue = queue;

    expected = 0x12345678;

    EXPECT_HRESULT_SUCCEEDED(XAsyncRun(&async, WorkThunk::Callback));

    result = XAsyncGetStatus(&async, true);

    // Drain the queue before verifying it's empty to ensure cleanup has been recorded
    UINT64 drainTicks = GetTickCount64();
    while ((!XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion) || !XTaskQueueIsEmpty(queue, XTaskQueuePort::Work))
        && GetTickCount64() - drainTicks < 2000)
    {
        Sleep(10);
    }

    EXPECT_EQ(result, expected);
    EXPECT_QUEUE_EMPTY(queue);
}

TEST_F(XThreadingTests, VerifyCustomQueue)
{
    XAsyncBlock async = {};
    auto data = AutoRef<FactorialCallData>(new FactorialCallData{});
    DWORD result;
    DWORD completionThreadId;

    CompletionThunk cb([&](XAsyncBlock* async)
    {
        completionThreadId = GetCurrentThreadId();
        EXPECT_HRESULT_SUCCEEDED(FactorialResult(async, &result));
    });

    async.context = &cb;
    async.callback = CompletionThunk::Callback;

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &async.queue));

    data.Ref->value = 5;

    EXPECT_HRESULT_SUCCEEDED(FactorialAsync(data.Ref, &async));

    EXPECT_TRUE(XTaskQueueDispatch(async.queue, XTaskQueuePort::Work, 100));
    EXPECT_EQ(data.Ref->result, (DWORD)120);
    EXPECT_EQ(GetCurrentThreadId(), data.Ref->workThread);

    EXPECT_TRUE(XTaskQueueDispatch(async.queue, XTaskQueuePort::Completion, 100));
    EXPECT_EQ(result, (DWORD)120);
    EXPECT_EQ(GetCurrentThreadId(), completionThreadId);

    EXPECT_QUEUE_EMPTY(async.queue);
    XTaskQueueCloseHandle(async.queue);
}

TEST_F(XThreadingTests, VerifyCantScheduleTwice)
{
    XAsyncBlock async = {};
    auto data = AutoRef<FactorialCallData>(new FactorialCallData{});

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &async.queue));

    EXPECT_HRESULT_SUCCEEDED(XAsyncBegin(&async, data.Ref, nullptr, nullptr, FactorialWorkerSimple));
    EXPECT_HRESULT_SUCCEEDED(XAsyncSchedule(&async, 0));
    EXPECT_EQ(E_UNEXPECTED, XAsyncSchedule(&async, 0));

    XAsyncCancel(&async);

    // Dispatch to clear out the queue
    while (XTaskQueueDispatch(async.queue, XTaskQueuePort::Work, 0));

    EXPECT_QUEUE_EMPTY(async.queue);
    XTaskQueueCloseHandle(async.queue);
}

TEST_F(XThreadingTests, VerifyWaitForCompletion)
{
    XAsyncBlock async = {};
    auto data = AutoRef<FactorialCallData>(new FactorialCallData{});
    DWORD result = 0;
    HANDLE completionEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    EXPECT_TRUE(completionEvent != nullptr);

    CompletionThunk cb([&](XAsyncBlock* async)
    {
        Sleep(2000);
        EXPECT_HRESULT_SUCCEEDED(FactorialResult(async, &result));
        SetEvent(completionEvent);
    });

    async.context = &cb;
    async.callback = CompletionThunk::Callback;
    async.queue = queue;

    data.Ref->value = 5;

    EXPECT_HRESULT_SUCCEEDED(FactorialAsync(data.Ref, &async));
    EXPECT_HRESULT_SUCCEEDED(XAsyncGetStatus(&async, true));
    EXPECT_EQ((DWORD)WAIT_OBJECT_0, WaitForSingleObject(completionEvent, 5000));
    CloseHandle(completionEvent);

    UINT64 ticks = GetTickCount64();
    while ((!XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion) || !XTaskQueueIsEmpty(queue, XTaskQueuePort::Work))
        && GetTickCount64() - ticks < 2000)
    {
        Sleep(10);
    }

    EXPECT_EQ(data.Ref->result, result);
    EXPECT_EQ(data.Ref->result, (DWORD)120);
    EXPECT_QUEUE_EMPTY(queue);
}

// Since XAsyncBeginAlloc is an undocumented function, testing it is not necessary.
/**
TEST_F(XThreadingTests, VerifyBeginAsyncAlloc)
{
    XAsyncBlock async = {};
    DWORD result;

    CompletionThunk cb([&](XAsyncBlock* async)
    {
        EXPECT_HRESULT_SUCCEEDED(FactorialResult(async, &result));
    });

    async.context = &cb;
    async.callback = CompletionThunk::Callback;
    async.queue = queue;

    EXPECT_HRESULT_SUCCEEDED(FactorialAllocateAsync(5, &async));
    EXPECT_HRESULT_SUCCEEDED(XAsyncGetStatus(&async, true));

    // Drain the queue before verifying it's empty to ensure cleanup has been recorded
    UINT64 drainTicks = GetTickCount64();
    while ((!XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion) || !XTaskQueueIsEmpty(queue, XTaskQueuePort::Work))
        && GetTickCount64() - drainTicks < 2000)
    {
        Sleep(10);
    }

    EXPECT_EQ(result, (DWORD)120);
    EXPECT_QUEUE_EMPTY(queue);
}
*/

TEST_F(XThreadingTests, VerifyPeriodicPattern)
{
    struct Controller
    {
        XTaskQueueHandle queue;
        bool enabled;
        uint32_t callbackCount;
        std::function<void(Controller* controller)> schedule;
    };

    auto schedule = [](Controller* controller)
    {
        XAsyncBlock* async = new XAsyncBlock{};
        async->context = controller;
        async->queue = controller->queue;
        async->callback = [](XAsyncBlock* async)
        {
            Controller* pcontroller = (Controller*)async->context;
            pcontroller->callbackCount++;
            if (pcontroller->enabled)
            {
                pcontroller->schedule(pcontroller);
            }
            delete async;
        };

        EXPECT_HRESULT_SUCCEEDED(XAsyncBegin(async, controller, nullptr, nullptr,
                                     [](XAsyncOp op, const XAsyncProviderData* data)
                                     {
                                         if (op == XAsyncOp::DoWork)
                                         {
                                             XAsyncComplete(data->async, S_OK, 0);
                                         }
                                         return S_OK;
                                     }));

        EXPECT_HRESULT_SUCCEEDED(XAsyncSchedule(async, 30));
    };

    Controller c;
    c.queue = queue;
    c.enabled = true;
    c.callbackCount = 0;
    c.schedule = schedule;

    // Now run this thing for a while
    schedule(&c);

    uint64_t ticks = GetTickCount64();
    while (c.callbackCount < 10)
    {
        Sleep(100);
        EXPECT_TRUE(GetTickCount64() - ticks < 10000);
    }

    c.enabled = false;
    while (XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 500))
    {
    }
    EXPECT_QUEUE_EMPTY(queue);
}

TEST_F(XThreadingTests, VerifyRunAlotAsync)
{
    int count = 20000;
    WorkThunk cb([&](XAsyncBlock*)
    {
        return 0;
    });

    auto asyncs = std::unique_ptr<XAsyncBlock[]>(new XAsyncBlock[count]{});

    for (int i = 0; i < count; i++)
    {
        auto& async = asyncs[i];
        async.queue = queue;
        async.context = &cb;

        HRESULT hr = XAsyncRun(&async, WorkThunk::Callback);
        if (FAILED(hr))
        {
            FAIL();
        }
    }

    while (!XTaskQueueIsEmpty(queue, XTaskQueuePort::Work) ||
        !XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion))
    {
        while (XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 500))
        {
        }
    }
}

TEST_F(XThreadingTests, VerifyGetAsyncStatusNoDeadlock)
{
    WorkThunk cb([](XAsyncBlock*)
    {
        Sleep(10);
        return 0;
    });

    XAsyncBlock async = {};
    async.queue = queue;
    async.context = &cb;

    for (int iteration = 0; iteration < 500; iteration++)
    {
        HRESULT hr = XAsyncRun(&async, WorkThunk::Callback);
        if (FAILED(hr)) EXPECT_HRESULT_SUCCEEDED(hr);
        while (XAsyncGetStatus(&async, false) == E_PENDING)
        {
            Sleep(0);
        }
    }
}

TEST_F(XThreadingTests, VerifyGlobalQueueUsage)
{
    XAsyncBlock async = {};

    auto nopProvider = [](XAsyncOp op, const XAsyncProviderData* d)
    {
        if (op == XAsyncOp::Cancel)
        {
            XAsyncComplete(d->async, E_ABORT, 0);
        }
        return S_OK;
    };

    // Verify we use the global queue
    EXPECT_HRESULT_SUCCEEDED(XAsyncBegin(&async, nullptr, nullptr, nullptr, nopProvider));
    XAsyncCancel(&async);
    EXPECT_EQ(E_ABORT, XAsyncGetStatus(&async, true));

    // Now null the global queue and verify the right error happens
    XTaskQueueHandle globalQueue;
    EXPECT_TRUE(XTaskQueueGetCurrentProcessTaskQueue(&globalQueue));
    XTaskQueueSetCurrentProcessTaskQueue(nullptr);

    EXPECT_EQ(E_NO_TASK_QUEUE, XAsyncBegin(&async, nullptr, nullptr, nullptr, nopProvider));
    XTaskQueueSetCurrentProcessTaskQueue(globalQueue);
    XTaskQueueCloseHandle(globalQueue);
}

TEST_F(XThreadingTests, VerifyFailedBeginCompletes)
{
    XAsyncBlock async{};
    async.queue = queue;

    auto failProvider = [](XAsyncOp op, const XAsyncProviderData*)
    {
        return op == XAsyncOp::Begin ? E_FAIL : E_UNEXPECTED;
    };

    // XAsyncBegin should still succeed even if the begin op fails, because
    // the call was successfully started.
    EXPECT_HRESULT_SUCCEEDED(XAsyncBegin(&async, nullptr, nullptr, nullptr, failProvider));
    EXPECT_EQ(E_FAIL, XAsyncGetStatus(&async, true));
}

TEST_F(XThreadingTests, VerifyZeroPayloadCleansUpFast)
{
    XAsyncBlock async{};

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &async.queue));

    struct Context
    {
        uint32_t cleanupCount = 0;
    };

    auto emptyProvider = [](XAsyncOp op, const XAsyncProviderData* data)
    {
        switch (op)
        {
        case XAsyncOp::Begin:
            EXPECT_HRESULT_SUCCEEDED(XAsyncSchedule(data->async, 0));
            break;

        case XAsyncOp::DoWork:
            XAsyncComplete(data->async, S_OK, 0);
            break;

        case XAsyncOp::Cleanup:
            ((Context*)data->context)->cleanupCount++;
            break;
        }

        return S_OK;
    };

    Context cxt;
    cxt.cleanupCount = 0;

    EXPECT_HRESULT_SUCCEEDED(XAsyncBegin(&async, &cxt, nullptr, nullptr, emptyProvider));
    EXPECT_TRUE(XTaskQueueDispatch(async.queue, XTaskQueuePort::Work, 0));
    EXPECT_EQ((uint32_t)1, cxt.cleanupCount);

    while (XTaskQueueDispatch(async.queue, XTaskQueuePort::Completion, 0));

    // Should only call cleanup once.
    EXPECT_EQ((uint32_t)1, cxt.cleanupCount);

    XTaskQueueCloseHandle(async.queue);
}

TEST_F(XThreadingTests, VerifyCompleteInBegin)
{
    struct Context
    {
        HANDLE evt = nullptr;

        Context()
        {
            evt = CreateEvent(nullptr, TRUE, FALSE, nullptr);
            EXPECT_TRUE(evt != nullptr);
        }

        ~Context()
        {
            if (evt) CloseHandle(evt);
        }
    };

    Context context;
    XAsyncBlock async{};
    async.queue = queue;
    async.context = &context;
    async.callback = [](XAsyncBlock* async)
    {
        Context* cxt = static_cast<Context*>(async->context);
        SetEvent(cxt->evt);
    };

    auto provider = [](XAsyncOp op, const XAsyncProviderData* data)
    {
        switch (op)
        {
        case XAsyncOp::Begin:
            XAsyncComplete(data->async, S_OK, 0);
            break;

        case XAsyncOp::Cleanup:
            break;

        default:
            break;
        }
        return S_OK;
    };

    EXPECT_HRESULT_SUCCEEDED(XAsyncBegin(&async, nullptr, nullptr, nullptr, provider));
    EXPECT_HRESULT_SUCCEEDED(XAsyncGetStatus(&async, true));
    EXPECT_EQ((DWORD)WAIT_OBJECT_0, WaitForSingleObject(context.evt, 2500));
}

TEST_F(XThreadingTests, VerifyBeginAfterTerminate)
{
        XAsyncBlock async{};
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &async.queue));

        auto provider = [](XAsyncOp, const XAsyncProviderData*)
        {
            return S_OK;
        };

        // Terminate the queue
        XTaskQueueTerminate(async.queue, true, nullptr, nullptr);

        // XAsyncBegin should fail early with an abort.
        EXPECT_EQ(E_ABORT, XAsyncBegin(&async, nullptr, nullptr, nullptr, provider));

        XTaskQueueCloseHandle(async.queue);
    }

TEST_F(XThreadingTests, VerifyFailureInDoWork)
{
    XAsyncBlock async{};
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool,
                                      &async.queue));

    constexpr static HRESULT hrSpecial = 0x8009ABCD;

    auto provider = [](XAsyncOp op, const XAsyncProviderData* data)
    {
        switch (op)
        {
        case XAsyncOp::Begin:
            return XAsyncSchedule(data->async, 0);

        case XAsyncOp::Cleanup:
            break;

        case XAsyncOp::DoWork:
            return hrSpecial;

        default:
            break;
        }
        return S_OK;
    };

    // Ensure that the call runs through and correctly reports our special
    // error.
    EXPECT_HRESULT_SUCCEEDED(XAsyncBegin(&async, nullptr, nullptr, nullptr, provider));
    EXPECT_EQ(hrSpecial, XAsyncGetStatus(&async, true));

    XTaskQueueCloseHandle(async.queue);
}

TEST_F(XThreadingTests, VerifyDuplicateResultCallsFail)
{
        XAsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData {});
        DWORD result;
        std::vector<XAsyncOp> ops;

        async.queue = queue;

        data.Ref->value = 5;

        EXPECT_HRESULT_SUCCEEDED(FactorialAsync(data.Ref, &async));
        EXPECT_HRESULT_SUCCEEDED(XAsyncGetStatus(&async, true));

        EXPECT_HRESULT_SUCCEEDED(FactorialResult(&async, &result));
        EXPECT_EQ(E_ILLEGAL_METHOD_CALL, FactorialResult(&async, &result));
    }

TEST_F(XThreadingTests, VerifyDuplicatePendingResultCallsSucceed)
{
    struct Context
    {
        HANDLE complete = nullptr;
        HANDLE waiting = nullptr;

        Context()
        {
            complete = CreateEvent(nullptr, TRUE, FALSE, nullptr);
            EXPECT_TRUE(complete != nullptr);

            waiting = CreateEvent(nullptr, TRUE, FALSE, nullptr);
            EXPECT_TRUE(waiting != nullptr);
        }

        ~Context()
        {
            if (complete) CloseHandle(complete);
            if (waiting) CloseHandle(waiting);
        }
    };

    auto provider = [](XAsyncOp op, const XAsyncProviderData* data)
    {
        Context* cxt;

        switch (op)
        {
        case XAsyncOp::Begin:
            return XAsyncSchedule(data->async, 0);

        case XAsyncOp::Cleanup:
            break;

        case XAsyncOp::DoWork:
            cxt = (Context*)(data->async->context);
            SetEvent(cxt->waiting);
            WaitForSingleObject(cxt->complete, INFINITE);
            XAsyncComplete(data->async, S_OK, 0);
            break;

        default:
            break;
        }
        return S_OK;
    };

    Context context;
    XAsyncBlock async{};
    async.queue = queue;
    async.context = &context;

    EXPECT_HRESULT_SUCCEEDED(XAsyncBegin(&async, nullptr, nullptr, nullptr, provider));

    // Wait for the provider to get its do work called
    EXPECT_EQ((DWORD)WAIT_OBJECT_0, WaitForSingleObject(context.waiting, 2000));

    // Now try to access the results -- we should get pending and be able to
    // do this again and again.

    for (uint32_t idx = 0; idx < 10; idx++)
    {
        EXPECT_EQ(E_PENDING, XAsyncGetResult(&async, nullptr, 0, nullptr, nullptr));
    }

    // Now complete the work
    SetEvent(context.complete);
    EXPECT_HRESULT_SUCCEEDED(XAsyncGetStatus(&async, true));

    EXPECT_HRESULT_SUCCEEDED(XAsyncGetResult(&async, nullptr, 0, nullptr, nullptr));

    // Because there was no payload this should continue to succeed
    EXPECT_HRESULT_SUCCEEDED(XAsyncGetResult(&async, nullptr, 0, nullptr, nullptr));
}

TEST_F(XThreadingTests, VerifyStockQueue)
{
    AutoQueueHandle queue;
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::Manual, &queue));

    bool workCalled = false;
    bool completeCalled = false;

    CallbackThunk<void, void> complete([&]()
    {
        completeCalled = true;
    });

    CallbackThunk<void, void> work([&]()
    {
        workCalled = true;
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Completion, 0, &complete,
                                                         CallbackThunk<void, void>::Callback));
    });

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, &work,
                                                     CallbackThunk<void, void>::Callback));

    EXPECT_TRUE(XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 5000));
    EXPECT_TRUE(workCalled);
    EXPECT_TRUE(completeCalled);
}

TEST_F(XThreadingTests, VerifyCompositeQueue)
{
    AutoQueueHandle queue;
    DWORD calls = 0;

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::Manual, &queue));

    CallbackThunk<void, void> work([&]
    {
        calls++;

        // Now create a composite queue with work and completion pointing to the work
        // stream of the original queue and invoke more work and a completion
        // They should all run on the work side.  Queues do not fully shut down until
        // they are empty so we should be fine auto closing the child queue here.

        XTaskQueuePortHandle workStream;
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueGetPort(queue.Handle(), XTaskQueuePort::Work, &workStream));

        AutoQueueHandle composite;
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreateComposite(workStream, workStream, &composite));

        EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(composite, XTaskQueuePort::Work, 0, &calls,
                                                         [](void* context, BOOLEAN)
                                                         {
                                                             DWORD* pcalls = (DWORD*)context;
                                                             (*pcalls)++;
                                                         }));
    });

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, &work,
                                                     CallbackThunk<void, void>::Callback));

    // Now wait for the queue to drain. The completion side of the queue should never have an item
    // in it.
    EXPECT_TRUE(XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion));
    UINT64 ticks = GetTickCount64();
    while (!XTaskQueueIsEmpty(queue, XTaskQueuePort::Work))
    {
        EXPECT_LT(GetTickCount64() - ticks, (UINT64)1000);
        Sleep(100);
    }
}

TEST_F(XThreadingTests, VerifyDuplicateQueueHandle)
{
    const size_t count = 10;
    XTaskQueueHandle queue;
    XTaskQueueHandle dups[count];

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

    for (int idx = 0; idx < count; idx++)
    {
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueDuplicateHandle(queue, &dups[idx]));
        EXPECT_TRUE(queue != dups[idx]);
    }

    for (int idx = 0; idx < count; idx++)
    {
        XTaskQueueCloseHandle(dups[idx]);
    }
    
    alignas(void*) uint8_t fakeHandleStorage[64] = {};
    EXPECT_HRESULT_FAILED(XTaskQueueDuplicateHandle(reinterpret_cast<XTaskQueueHandle>(fakeHandleStorage), &dups[1]));
    XTaskQueueCloseHandle(queue);
}

TEST_F(XThreadingTests, VerifyDispatch)
{
    AutoQueueHandle queue;
    DWORD workCalls = 0;
    DWORD completeCalls = 0;
    DWORD dispatched = 0;
    DWORD workThreadId = 0;
    DWORD completeThreadId = 0;
    const DWORD count = 10;

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

    CallbackThunk<void, void> workThunk([&]()
    {
        workCalls++;
        workThreadId = GetCurrentThreadId();
    });

    CallbackThunk<void, void> completeThunk([&]()
    {
        completeCalls++;
        completeThreadId = GetCurrentThreadId();
    });

    for (DWORD idx = 0; idx < count; idx++)
    {
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, &workThunk,
                                                         CallbackThunk<void, void>::Callback));
    }

    EXPECT_FALSE(XTaskQueueIsEmpty(queue, XTaskQueuePort::Work));
    EXPECT_TRUE(XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion));

    for (DWORD idx = 0; idx < count; idx++)
    {
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Completion, 0, &completeThunk,
                                                         CallbackThunk<void, void>::Callback));
    }

    EXPECT_FALSE(XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion));

    while (XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0))
    {
        dispatched++;
    }

    EXPECT_EQ(count, dispatched);
    EXPECT_EQ(count, workCalls);

    dispatched = 0;

    while (XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0))
    {
        dispatched++;
    }

    EXPECT_EQ(count, dispatched);
    EXPECT_EQ(count, workCalls);
    EXPECT_EQ(count, completeCalls);
    
    queue.Close();
    workCalls = 0;
    completeCalls = 0;
    dispatched = 0;

    // Note: inverting who has the manual thread and who has the thread pool for variety

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::ThreadPool, &queue));

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, &workThunk,
                                                     CallbackThunk<void, void>::Callback));
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, &workThunk,
                                                     CallbackThunk<void, void>::Callback));

    EXPECT_TRUE(XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0));
    EXPECT_EQ((DWORD)1, workCalls);

    EXPECT_TRUE(XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0));
    EXPECT_EQ((DWORD)2, workCalls);
    EXPECT_EQ(GetCurrentThreadId(), workThreadId);

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Completion, 0, &completeThunk,
                                                     CallbackThunk<void, void>::Callback));

    UINT64 ticks = GetTickCount64();
    while (!XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion))
    {
        EXPECT_LT(GetTickCount64() - ticks, (UINT64)1000);
        Sleep(100);
    }

    EXPECT_EQ((DWORD)1, completeCalls);
    EXPECT_NE(GetCurrentThreadId(), completeThreadId);
    
    workCalls = completeCalls = workThreadId = completeThreadId = 0;

    CallbackThunk<void, void> completeHandoffThunk([&]()
    {
        completeCalls++;
        completeThreadId = GetCurrentThreadId();
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, &workThunk,
                                                         CallbackThunk<void, void>::Callback));
    });

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Completion, 0, &completeHandoffThunk,
                                                     CallbackThunk<void, void>::Callback));

    ticks = GetTickCount64();
    while (!XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion))
    {
        EXPECT_LT(GetTickCount64() - ticks, (UINT64)1000);
        XTaskQueueDispatch(queue, XTaskQueuePort::Work, 100);
    }

    ticks = GetTickCount64();
    while (!XTaskQueueIsEmpty(queue, XTaskQueuePort::Work))
    {
        EXPECT_LT(GetTickCount64() - ticks, (UINT64)1000);
        XTaskQueueDispatch(queue, XTaskQueuePort::Work, 100);
    }

    EXPECT_EQ(GetCurrentThreadId(), workThreadId);
}

TEST_F(XThreadingTests, VerifySubmittedCallback)
{
    AutoQueueHandle queue;
    XTaskQueueRegistrationToken token;
    const DWORD workCount = 4;
    const DWORD completeCount = 7;

    struct SubmitCount
    {
        DWORD Work;
        DWORD Completion;
    } submitCount;

    submitCount.Work = submitCount.Completion = 0;

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueRegisterMonitor(queue, &submitCount,
                                               [](void* cxt, XTaskQueueHandle, XTaskQueuePort stream)
                                               {
                                                   SubmitCount* s = (SubmitCount*)cxt;
                                                   if (stream == XTaskQueuePort::Work)
                                                   {
                                                       s->Work++;
                                                   }
                                                   else
                                                   {
                                                       s->Completion++;
                                                   }
                                               }, &token));

    auto cb = [](void*, BOOLEAN)
    {
    };

    for (DWORD i = 0; i < workCount; i++)
    {
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitCallback(queue, XTaskQueuePort::Work, nullptr, cb));
    }

    for (DWORD i = 0; i < completeCount; i++)
    {
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitCallback(queue, XTaskQueuePort::Completion, nullptr, cb));
    }

    EXPECT_EQ(submitCount.Work, workCount);
    EXPECT_EQ(submitCount.Completion, completeCount);

    XTaskQueueUnregisterMonitor(queue, token);

    // Now drain the queues
    while (XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0));
    while (XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0));
}

TEST_F(XThreadingTests, VerifySubmitCallbackWithWait)
{
    AutoQueueHandle queue;

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

    struct ResultData
    {
        uint64_t Times[3];
    };

    struct ArgData
    {
        ResultData* Data;
        int Index;
    };

    ResultData result;

    XTaskQueuePort streams[] =
    {
        XTaskQueuePort::Work,
        XTaskQueuePort::Completion
    };

    auto cb = [](void* context, BOOLEAN)
    {
        ArgData* data = (ArgData*)context;
        data->Data->Times[data->Index] = GetTickCount64();
    };

    for (int i = 0; i < _countof(streams); i++)
    {
        XTaskQueuePort stream = streams[i];
        uint64_t baseTicks = GetTickCount64();

        ArgData call1;
        call1.Index = 0;
        call1.Data = &result;
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, stream, 1000, &call1, cb));

        ArgData call2;
        call2.Index = 1;
        call2.Data = &result;
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, stream, 0, &call2, cb));

        ArgData call3;
        call3.Index = 2;
        call3.Data = &result;
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, stream, 500, &call3, cb));

        // We should be able to dispatch one without waiting
        EXPECT_TRUE(XTaskQueueDispatch(queue, stream, 0));
        EXPECT_FALSE(XTaskQueueDispatch(queue, stream, 0));

        EXPECT_TRUE(XTaskQueueDispatch(queue, stream, 700));
        EXPECT_TRUE(XTaskQueueDispatch(queue, stream, 1200));
        EXPECT_FALSE(XTaskQueueDispatch(queue, stream, 0));

        uint64_t call1Ticks = result.Times[0] - baseTicks;
        uint64_t call2Ticks = result.Times[1] - baseTicks;
        uint64_t call3Ticks = result.Times[2] - baseTicks;

        // Call 1 at index 0 should have a tick count > 1000 and < 1100 (100ms tolerance for debugger overhead)
        EXPECT_TRUE(call1Ticks >= 1000 && call1Ticks < 1100);
        EXPECT_TRUE(call2Ticks < 100);
        EXPECT_TRUE(call3Ticks >= 500 && call3Ticks < 600);
    }
}

TEST_F(XThreadingTests, VerifyRegisterCallbackSubmitted)
{
    AutoQueueHandle queue;
    const uint32_t count = 5;
    XTaskQueueRegistrationToken tokens[count];
    uint32_t calls[count];

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

    auto cb = [](void* context, XTaskQueueHandle, XTaskQueuePort)
    {
        uint32_t* p = static_cast<uint32_t*>(context);
        (*p)++;
    };

    auto dummy = [](void*, BOOLEAN)
    {
    };

    for (uint32_t idx = 0; idx < count; idx++)
    {
        calls[idx] = 0;
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueRegisterMonitor(queue, &(calls[idx]), cb, &tokens[idx]));
    }

    // queue some calls
    XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, nullptr, dummy);
    XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, nullptr, dummy);
    XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, nullptr, dummy);

    // Should be a correct count on all calls
    for (uint32_t idx = 0; idx < count; idx++)
    {
        EXPECT_EQ(calls[idx], 3u);
    }

    // Nuke every odd entry
    for (uint32_t idx = 1; idx < count; idx += 2)
    {
        XTaskQueueUnregisterMonitor(queue, tokens[idx]);
    }

    // Now make some more calls.
    XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, nullptr, dummy);
    XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, nullptr, dummy);
    XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, nullptr, dummy);

    // Should be a correct count on all calls
    for (uint32_t idx = 0; idx < count; idx++)
    {
        uint32_t expectedCount = (idx & 1) ? 3 : 6;
        EXPECT_EQ(calls[idx], expectedCount);
    }

    // Dispatch all calls on the queue so we can shut it down
    while (XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0));
}

TEST_F(XThreadingTests, VerifyImmediateDispatch)
{
    AutoQueueHandle queue;
    uint32_t callCount = 0;

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Immediate, &queue));

    auto callback = [](void* ptr, BOOLEAN)
    {
        uint32_t* pint = (uint32_t*)ptr;
        (*pint)++;
    };

    const uint32_t count = 10;

    for (uint32_t i = 1; i <= count; i++)
    {
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Completion, 0, &callCount, callback));
        EXPECT_EQ(i, callCount);
    }

    // Verify a deferred completion still works
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Completion, 200, &callCount, callback));
    EXPECT_EQ(count, callCount);
    Sleep(500);
    EXPECT_EQ(count + 1, callCount);
}

TEST_F(XThreadingTests, VerifySerializedThreadPoolDispatch)
{
    AutoQueueHandle queue;
    const uint32_t total = 100;
    struct Data
    {
        uint32_t Count = 0;
        uint32_t Work[total];
    };

    struct PerCallData
    {
        uint32_t Index;
        Data* D;
    };

    Data data;
    data.Count = 0;
    ZeroMemory(data.Work, sizeof(data.Work));

    PerCallData callData[total];
    for (uint32_t i = 0; i < total; i++)
    {
        callData[i].Index = i;
        callData[i].D = &data;
    }

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::SerializedThreadPool,
                                      &queue));

    auto callback = [](void* ptr, BOOLEAN)
    {
        PerCallData* pdata = (PerCallData*)ptr;
        if (pdata->Index == 0)
        {
            pdata->D->Work[pdata->Index] = 5;
        }
        else
        {
            pdata->D->Work[pdata->Index] = pdata->D->Work[pdata->D->Count - 1] + 5;
        }
        pdata->D->Count++;
    };

    for (uint32_t i = 0; i < total; i++)
    {
        EXPECT_HRESULT_SUCCEEDED(
            XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Completion, 0, &(callData[i]), callback));
    }

    Sleep(500);

    EXPECT_EQ(total, data.Count);
    uint32_t previous = 0;
    for (uint32_t i = 0; i < total; i++)
    {
        EXPECT_EQ(previous + 5, data.Work[i]);
        previous = data.Work[i];
    }
}

/**
 * Our test cases won't cover infinite timeouts for now
TEST_F(XThreadingTests, VerifyMultithreadManualDispatch)
{
    AutoQueueHandle queue;
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Immediate, &queue));

    // We use a manual dispatch queue and spin up two threads to process it.  We are testing
    // that XTaskQueueDispatch never returns with an INFINITE timeout unless the task queue is
    // getting terminated.

    auto threadCallback = [](PVOID param) -> DWORD
    {
        XTaskQueueHandle queue = static_cast<XTaskQueueHandle>(param);

        return 0;
    };

    DWORD tid;
    AutoHandle thread1 = CreateThread(nullptr, 0, threadCallback, queue.Handle(), 0, &tid);
    AutoHandle thread2 = CreateThread(nullptr, 0, threadCallback, queue.Handle(), 0, &tid);

    EXPECT_TRUE(thread1.Handle() != nullptr);
    EXPECT_TRUE(thread2.Handle() != nullptr);

    HANDLE threadWaits[] = {thread1.Handle(), thread2.Handle()};

    auto workCallback = [](void*, BOOLEAN)
    {
    };

    const uint32_t count = 5;
    for (uint32_t i = 1; i <= count; i++)
    {
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitCallback(queue, XTaskQueuePort::Work, nullptr, workCallback));
    }

    EXPECT_TRUE(WaitForMultipleObjects(2, threadWaits, FALSE, 750) == WAIT_TIMEOUT);

    XTaskQueuePortHandle workPort;
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueGetPort(queue, XTaskQueuePort::Work, &workPort));

    XTaskQueuePortHandle completionPort;
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueGetPort(queue, XTaskQueuePort::Completion, &completionPort));

    AutoQueueHandle compositeQueue;
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreateComposite(workPort, completionPort, &compositeQueue));

    for (uint32_t i = 1; i <= count; i++)
    {
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitCallback(compositeQueue, XTaskQueuePort::Work, nullptr, workCallback));
    }

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueTerminate(compositeQueue, true, nullptr, nullptr));

    EXPECT_TRUE(WaitForMultipleObjects(2, threadWaits, FALSE, 750) == WAIT_TIMEOUT);

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueTerminate(queue, true, nullptr, nullptr));

    EXPECT_TRUE(WaitForSingleObject(threadWaits[0], 750) == WAIT_OBJECT_0);
    EXPECT_TRUE(WaitForSingleObject(threadWaits[1], 750) == WAIT_OBJECT_0);
}
*/

TEST_F(XThreadingTests, VerifyRegisterWithAutoReset)
{
    AutoQueueHandle queue;
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue));
    _VerifyRegisterWithAutoReset(queue);

    PumpedTaskQueue pumpedQueue;
    _VerifyRegisterWithAutoReset(pumpedQueue.queue);
}

TEST_F(XThreadingTests, VerifyQueueTermination)
{
    AutoQueueHandle queue;
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue));
    _VerifyQueueTermination(queue, false, false, false);

    queue.Close();
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue));
    _VerifyQueueTermination(queue, true, false, false);

    queue.Close();
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::SerializedThreadPool,
                                      XTaskQueueDispatchMode::SerializedThreadPool, &queue));
    _VerifyQueueTermination(queue, true, true, false);
}

TEST_F(XThreadingTests, VerifyEmptyQueueTermination)
{
    AutoQueueHandle queue;
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue));
    _VerifyQueueTermination(queue, false, false, true);

    queue.Close();
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue));
    _VerifyQueueTermination(queue, true, false, true);

    queue.Close();
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::SerializedThreadPool,
                                      XTaskQueueDispatchMode::SerializedThreadPool, &queue));
    _VerifyQueueTermination(queue, true, true, true);
}

TEST_F(XThreadingTests, VerifyGlobalQueue)
{
    AutoQueueHandle queue;
    EXPECT_TRUE(XTaskQueueGetCurrentProcessTaskQueue(&queue));
    EXPECT_TRUE(queue != nullptr);

    auto cb = [](void*, BOOLEAN)
    {
    };

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitCallback(queue, XTaskQueuePort::Work, nullptr, cb));

    // Now replace the global with our own.
    AutoQueueHandle globalQueue(queue.Release());
    AutoQueueHandle ourQueue;
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &ourQueue));

    XTaskQueueSetCurrentProcessTaskQueue(ourQueue);

    EXPECT_TRUE(XTaskQueueGetCurrentProcessTaskQueue(&queue));
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitCallback(queue, XTaskQueuePort::Work, nullptr, cb));
    EXPECT_FALSE(XTaskQueueIsEmpty(ourQueue, XTaskQueuePort::Work));
    while (XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0))
    {
    };

    // Null the queue and verify we get false
    queue.Close();
    XTaskQueueSetCurrentProcessTaskQueue(nullptr);
    EXPECT_FALSE(XTaskQueueGetCurrentProcessTaskQueue(&queue));
    EXPECT_TRUE(queue.Handle() == nullptr);

    // Replace the global queue back
    XTaskQueueSetCurrentProcessTaskQueue(globalQueue);
}

TEST_F(XThreadingTests, VerifyCloseInTerminationForThreadPool)
{
    struct TestData
    {
        AutoQueueHandle queue;
        bool terminationCalled = false;
    } data;

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool,
                                      &data.queue));

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueTerminate(data.queue, false, &data, [](void* context)
    {
        TestData* pd = (TestData*)context;
        XTaskQueueCloseHandle(pd->queue.Release());
        pd->terminationCalled = true;
    }));

    uint64_t ticks = GetTickCount64();
    while (!data.terminationCalled && GetTickCount64() - ticks < 5000)
    {
        Sleep(250);
    }

    EXPECT_TRUE(data.terminationCalled);
}

TEST_F(XThreadingTests, VerifyCloseInTerminationForManual)
{
    struct TestData
    {
        AutoQueueHandle queue;
        bool terminationCalled = false;
    } data;

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &data.queue));

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueTerminate(data.queue, false, &data, [](void* context)
    {
        TestData* pd = (TestData*)context;
        pd->terminationCalled = true;
        XTaskQueueCloseHandle(pd->queue.Release());
    }));

    uint64_t ticks = GetTickCount64();
    while (!data.terminationCalled && GetTickCount64() - ticks < 5000)
    {
        XTaskQueueDispatch(data.queue, XTaskQueuePort::Work, 0);
        XTaskQueueDispatch(data.queue, XTaskQueuePort::Completion, 0);
        Sleep(250);
    }

    EXPECT_TRUE(data.terminationCalled);
}

TEST_F(XThreadingTests, VerifyWaitTermination)
{
    uint64_t start = GetTickCount64();
    do
    {
        XTaskQueueHandle queue;
        HRESULT hr = XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue);
        if (FAILED(hr)) FAIL();

        hr = XTaskQueueTerminate(queue, true, nullptr, nullptr);
        if (FAILED(hr)) FAIL();

        XTaskQueueCloseHandle(queue);
    }
    while (GetTickCount64() - start < 5000);
}

/**
 * Our test cases won't cover infinite timeouts for now
TEST_F(XThreadingTests, VerifyManualDispatchAtTermination)
{
    AutoQueueHandle queue;

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

    auto dispatcher = [](void*, XTaskQueueHandle queue, XTaskQueuePort port)
    {
        XTaskQueueDispatch(queue, port, 0);
    };

    XTaskQueueRegistrationToken token;
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueRegisterMonitor(queue, nullptr, dispatcher, &token));
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueTerminate(queue, true, nullptr, nullptr));
}
*/

TEST_F(XThreadingTests, VerifyTerminationOfCompositeQueue)
{
    struct TestData
    {
        AutoQueueHandle queue;
        bool workInvoked = false;
        bool workCanceled = false;
        bool completionInvoked = false;
        bool completionCanceled = false;
        bool futureInvoked = false;
        bool futureCanceled = false;
        bool waitInvoked = false;
        bool waitCanceled = false;
        bool queueTerminated = false;
        XTaskQueueCallback* CompletionCallback = nullptr;
    } data, compositeData;

    AutoHandle waitHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    EXPECT_TRUE(waitHandle != nullptr);

    AutoHandle compositeWaitHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    EXPECT_TRUE(compositeWaitHandle != nullptr);

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &data.queue));

    XTaskQueuePortHandle port;
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueGetPort(data.queue, XTaskQueuePort::Work, &port));
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreateComposite(port, port, &compositeData.queue));

    auto workCallback = [](void* context, BOOLEAN canceled)
    {
        TestData* pd = (TestData*)context;
        pd->workInvoked = true;
        pd->workCanceled = canceled;
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitCallback(pd->queue, XTaskQueuePort::Completion, pd, pd->CompletionCallback));
    };

    auto futureCallback = [](void* context, BOOLEAN canceled)
    {
        TestData* pd = (TestData*)context;
        pd->futureInvoked = true;
        pd->futureCanceled = canceled;
    };

    auto waitCallback = [](void* context, BOOLEAN canceled)
    {
        TestData* pd = (TestData*)context;
        pd->waitInvoked = true;
        pd->waitCanceled = canceled;
    };

    auto completionCallback = [](void* context, BOOLEAN canceled)
    {
        TestData* pd = (TestData*)context;
        pd->completionInvoked = true;
        pd->completionCanceled = canceled;
    };

    auto terminationCallback = [](void* context)
    {
        TestData* pd = (TestData*)context;
        pd->queueTerminated = true;
        XTaskQueueCloseHandle(pd->queue.Release());
    };

    data.CompletionCallback = completionCallback;
    compositeData.CompletionCallback = completionCallback;

    // Submit work callbacks to both queues and terminate the composite.
    XTaskQueueRegistrationToken token;

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitCallback(data.queue, XTaskQueuePort::Work, &data, workCallback));
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(data.queue, XTaskQueuePort::Work, 300, &data, futureCallback));
    EXPECT_HRESULT_SUCCEEDED(
        XTaskQueueRegisterWaiter(data.queue, XTaskQueuePort::Work, waitHandle, &data, waitCallback, &token));

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitCallback(compositeData.queue, XTaskQueuePort::Work, &compositeData, workCallback));
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(compositeData.queue, XTaskQueuePort::Work, 300, &compositeData,
                                                     futureCallback));
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueRegisterWaiter(compositeData.queue, XTaskQueuePort::Work, compositeWaitHandle,
                                              &compositeData, waitCallback, &token));

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueTerminate(compositeData.queue, false, &compositeData, terminationCallback));

    bool somethingDispatched;
    do
    {
        somethingDispatched = XTaskQueueDispatch(data.queue, XTaskQueuePort::Work, 500);
        somethingDispatched |= XTaskQueueDispatch(data.queue, XTaskQueuePort::Completion, 500);
    }
    while (somethingDispatched);

    // Verify -- calls for the main queue should have gone through but calls for the composite
    // should have been canceled.

    EXPECT_TRUE(data.workInvoked);
    EXPECT_TRUE(data.completionInvoked);
    EXPECT_TRUE(data.futureInvoked);
    EXPECT_FALSE(data.waitInvoked);
    EXPECT_FALSE(data.workCanceled);
    EXPECT_FALSE(data.completionCanceled);
    EXPECT_FALSE(data.futureCanceled);

    EXPECT_TRUE(compositeData.workCanceled);
    EXPECT_TRUE(compositeData.completionCanceled);
    EXPECT_TRUE(compositeData.futureCanceled);
    EXPECT_TRUE(compositeData.waitCanceled);
    EXPECT_TRUE(compositeData.queueTerminated);

    // Verify it is still possible to schedule a call on the main queue and that registrations remain
    data.workInvoked = false;
    data.workCanceled = false;
    data.waitInvoked = false;
    data.waitCanceled = false;
    compositeData.waitInvoked = false;
    compositeData.waitCanceled = false;
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitCallback(data.queue, XTaskQueuePort::Work, &data, workCallback));

    SetEvent(waitHandle);
    SetEvent(compositeWaitHandle);

    do
    {
        somethingDispatched = XTaskQueueDispatch(data.queue, XTaskQueuePort::Work, 500);
        somethingDispatched |= XTaskQueueDispatch(data.queue, XTaskQueuePort::Completion, 500);
    }
    while (somethingDispatched);

    EXPECT_TRUE(data.workInvoked);
    EXPECT_TRUE(data.waitInvoked);
    EXPECT_FALSE(data.workCanceled);
    EXPECT_FALSE(data.waitCanceled);
    EXPECT_FALSE(compositeData.waitInvoked);
}

TEST_F(XThreadingTests, VerifyQueueMonitorHasCorrectPorts)
{
    AutoQueueHandle queue, compositeQueue;

    XTaskQueuePort queuePort = (XTaskQueuePort)(-1);
    XTaskQueuePort compositeQueuePort = (XTaskQueuePort)(-1);

    auto cb = [](void*, BOOLEAN)
    {
    };

    auto monitorCallback = [](void* context, XTaskQueueHandle, XTaskQueuePort port)
    {
        XTaskQueuePort* pp = (XTaskQueuePort*)context;
        *pp = port;
    };

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

    XTaskQueuePortHandle port;
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueGetPort(queue, XTaskQueuePort::Work, &port));
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreateComposite(port, port, &compositeQueue));

    XTaskQueueRegistrationToken token;
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueRegisterMonitor(queue, &queuePort, monitorCallback, &token));
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueRegisterMonitor(compositeQueue, &compositeQueuePort, monitorCallback, &token));

    // Submitting a call to the composite port should generate a monitor callback on each
    // queue with the correct port ID.
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitCallback(compositeQueue, XTaskQueuePort::Completion, nullptr, cb));
    while (XTaskQueueDispatch(compositeQueue, XTaskQueuePort::Completion, 100));

    EXPECT_EQ(queuePort, XTaskQueuePort::Work);
    EXPECT_EQ(compositeQueuePort, XTaskQueuePort::Completion);
}

TEST_F(XThreadingTests, VerifyNestedQueueDispatchOrder)
{
    auto Nest = [](XTaskQueueHandle q) -> XTaskQueueHandle
    {
        XTaskQueuePortHandle port;
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueGetPort(q, XTaskQueuePort::Work, &port));

        XTaskQueueHandle newQueue;
        EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreateComposite(port, port, &newQueue));
        return newQueue;
    };

    AutoQueueHandle queue;
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

    AutoQueueHandle outer(Nest(queue));
    AutoQueueHandle inner(Nest(outer));

    uint32_t step = 0;

    NextStep(&step, 0, L"Submitting Tasks");

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitDelayedCallback(outer, XTaskQueuePort::Work, 3000, &step, [](void* context, BOOLEAN)
    {
        NextStep(context, 6, L"Delayed task on outer queue");
    }));

    EXPECT_HRESULT_SUCCEEDED(XTaskQueueSubmitCallback(inner, XTaskQueuePort::Work, &step, [](void* context, BOOLEAN)
    {
        NextStep(context, 2, L"Task on inner queue");
    }));

    NextStep(&step, 1, L"About to tick queues");

    // Note that when dispatching on a queue we're actually dispathing
    // the queues port, so this may dispatch outer, inner or main.
    XTaskQueueDispatch(outer, XTaskQueuePort::Work, 0);
    XTaskQueueDispatch(outer, XTaskQueuePort::Completion, 0);

    NextStep(&step, 3, L"Closing inner queue");
    XTaskQueueCloseHandle(inner.Release());

    NextStep(&step, 4, L"Tick queues again");
    XTaskQueueDispatch(outer, XTaskQueuePort::Work, 0);
    XTaskQueueDispatch(outer, XTaskQueuePort::Completion, 0);

    NextStep(&step, 5, L"Wait for outer to finish");

    XTaskQueueDispatch(outer, XTaskQueuePort::Work, 4000);
    XTaskQueueDispatch(outer, XTaskQueuePort::Completion, 1000);

    NextStep(&step, 7, L"Closing outer queue");
    XTaskQueueCloseHandle(outer.Release());
}

TEST_F(XThreadingTests, VerifyCompositeTerminationRaceRepro)
{
    // Stress test for two race conditions in XTaskQueue termination:
    // Race #1: Nested Terminate during SignalTerminations iteration
    // Race #2: Concurrent ScheduleTermination heap corruption
    //
    // Test Parameters (configurable via environment variables):
    // HC_STRESS_XTASKQUEUE_REPRO=1 - Enable stress mode
    // HC_STRESS_XTASKQUEUE_REPRO_AVOID_RACE=1 - Use wait=true (default: wait=false)
    //
    // CRITICAL: Run with page heap enabled for Race #2 detection!
    // gflags /p /enable <test_exe> /full

    auto getEnvBool = [](PCWSTR name) -> bool
    {
        wchar_t buffer[8] = {};
        DWORD len = GetEnvironmentVariableW(name, buffer, _countof(buffer));
        if (len == 0 || len >= _countof(buffer))
        {
            return false;
        }
        return buffer[0] == L'1' || _wcsicmp(buffer, L"true") == 0;
    };

    bool stress = getEnvBool(L"HC_STRESS_XTASKQUEUE_REPRO");
    bool avoidRaceMode = getEnvBool(L"HC_STRESS_XTASKQUEUE_REPRO_AVOID_RACE");

    // Test parameters: Aggressive concurrency to trigger race conditions
    constexpr size_t k_thread_count = 64; // High thread count for maximum interleaving
    constexpr size_t k_iterations_per_thread = 100; // Iterations per thread
    constexpr size_t k_work_items_per_queue = 10; // Work items to keep callbacks active

    const size_t threadCount = stress ? 256 : k_thread_count;
    const size_t iterationsPerThread = stress ? 100 : k_iterations_per_thread;

    // Create root queue with thread pool dispatch
    AutoQueueHandle root;
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueCreate(
        XTaskQueueDispatchMode::ThreadPool,
        XTaskQueueDispatchMode::ThreadPool,
        &root));

    // Get ports for creating composite delegates
    XTaskQueuePortHandle workPort = nullptr;
    XTaskQueuePortHandle completionPort = nullptr;
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueGetPort(root, XTaskQueuePort::Work, &workPort));
    EXPECT_HRESULT_SUCCEEDED(XTaskQueueGetPort(root, XTaskQueuePort::Completion, &completionPort));

    // Synchronization and counters
    std::atomic<int> ready{0};
    std::atomic<int> done{0};
    std::atomic<int> createErrors{0};
    std::atomic<int> submitErrors{0};
    std::atomic<int> terminateErrors{0};
    std::atomic<int> workCallbackCount{0};
    std::atomic<int> delegateTerminationsRemaining{0}; // Track delegate termination completion
    std::atomic<bool> go{false};

    std::mutex cvMutex;
    std::condition_variable cv;

    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    // Spawn worker threads
    for (size_t t = 0; t < threadCount; ++t)
    {
        threads.emplace_back([&]
        {
            // Signal ready and wait for coordinated start
            ready.fetch_add(1, std::memory_order_acq_rel);
            {
                std::lock_guard<std::mutex> lock(cvMutex);
                cv.notify_all();
            }

            {
                std::unique_lock<std::mutex> lock(cvMutex);
                cv.wait(lock, [&] { return go.load(std::memory_order_acquire); });
            }

            // Rapidly create, populate, and terminate composite queues
            // This creates continuous churn of termination callbacks executing concurrently
            for (size_t iter = 0; iter < iterationsPerThread; ++iter)
            {
                XTaskQueueHandle delegateQueue = nullptr;
                HRESULT hr = XTaskQueueCreateComposite(workPort, completionPort, &delegateQueue);

                if (FAILED(hr) || delegateQueue == nullptr)
                {
                    createErrors.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }

                // Submit work items that will be executing/queued during termination
                // This ensures callbacks are active when termination occurs
                for (size_t w = 0; w < k_work_items_per_queue; ++w)
                {
                    HRESULT submitHr = XTaskQueueSubmitCallback(
                        delegateQueue,
                        XTaskQueuePort::Work,
                        &workCallbackCount,
                        [](void* context, BOOLEAN canceled)
                        {
                            if (!canceled)
                            {
                                std::atomic<int>* counter = static_cast<std::atomic<int>*>(context);
                                counter->fetch_add(1, std::memory_order_relaxed);
                                // Brief work to keep callbacks active during termination
                                std::this_thread::sleep_for(std::chrono::microseconds(50));
                            }
                        });

                    if (FAILED(submitHr))
                    {
                        submitErrors.fetch_add(1, std::memory_order_relaxed);
                    }
                }

                // Track this delegate's termination - increment before terminating
                delegateTerminationsRemaining.fetch_add(1, std::memory_order_acq_rel);

                // Terminate with wait=false and a callback to track completion
                // Each queue is independent, so we coordinate termination externally
                hr = XTaskQueueTerminate(
                    delegateQueue,
                    avoidRaceMode, // default is wait=false: delegate terminations are independent
                    &delegateTerminationsRemaining,
                    [](void* context)
                    {
                        // Decrement when this delegate's termination completes
                        std::atomic<int>* counter = static_cast<std::atomic<int>*>(context);
                        counter->fetch_sub(1, std::memory_order_acq_rel);
                    });

                if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_INVALID_STATE))
                {
                    terminateErrors.fetch_add(1, std::memory_order_relaxed);
                    // Rollback the counter since termination failed
                    delegateTerminationsRemaining.fetch_sub(1, std::memory_order_acq_rel);
                }

                XTaskQueueCloseHandle(delegateQueue);

                // Periodic yield to maximize interleaving of termination callbacks
                if ((iter % 10) == 0)
                {
                    std::this_thread::yield();
                }
            }

            // Signal completion
            done.fetch_add(1, std::memory_order_acq_rel);
            {
                std::lock_guard<std::mutex> lock(cvMutex);
                cv.notify_all();
            }
        });
    }

    // Wait for all threads to be ready
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        const bool started = cv.wait_for(lock, std::chrono::seconds(30), [&]
        {
            return ready.load(std::memory_order_acquire) == static_cast<int>(threadCount);
        });
        EXPECT_TRUE(started);
    }

    // Start all threads simultaneously
    go.store(true, std::memory_order_release);
    cv.notify_all();

    // Wait for all threads to complete
    {
        std::unique_lock<std::mutex> lock(cvMutex);
        const bool finished = cv.wait_for(lock, std::chrono::seconds(300), [&]
        {
            return done.load(std::memory_order_acquire) == static_cast<int>(threadCount);
        });
        EXPECT_TRUE(finished);
    }

    // Join all threads
    for (auto& t : threads)
    {
        t.join();
    }

    // Terminate the root before waiting for delegate terminations.
    // Queues are independent, so this order should be stable.
    XTaskQueueTerminate(root, true, nullptr, nullptr);
    XTaskQueueCloseHandle(root.Release());

    // Wait for all delegate terminations to complete before exiting,
    // so the DLL isn't unloaded out from under termination processing.
    UINT64 waitStartTicks = GetTickCount64();
    while (delegateTerminationsRemaining.load(std::memory_order_acquire) > 0
        && (GetTickCount64() - waitStartTicks) < (UINT64)60000)
    {
        std::this_thread::yield();
    }
    EXPECT_EQ(0, delegateTerminationsRemaining.load(std::memory_order_acquire));

    // Validate results
    EXPECT_EQ(0, submitErrors.load());
}

// Delayed Race conditions aren't covered as they rely on the internal XTaskQueueSetTestHooks method.