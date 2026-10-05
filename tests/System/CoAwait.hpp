#pragma once
#include <xasync.h>
#include <xasyncprovider.h>
#include <memory>
#include <optional>
#include <variant>
#include <functional>

#include <coroutine>

namespace CoXAsync {

template<class T>
struct promise;

template<class T>
struct coroutine : std::coroutine_handle<promise<T>> {
    using promise_type = CoXAsync::promise<T>;
};

template<class T>
struct Result {
    std::variant<T, HRESULT> result;
    Result(T value) { 
        static_assert(!std::is_same_v<T, HRESULT>, "Return type must not match HRESULT");
        this->result = value; 
    }
    Result(HRESULT hr) { this->result = hr; }
    HRESULT get_status() const { return std::holds_alternative<HRESULT>(result) ? std::get<HRESULT>(result) : S_OK; }
    T get_value() const { return std::get<T>(result); }
};

template<>
struct Result<void> {
    HRESULT status = E_FAIL;
    Result(HRESULT hr) { this->status = hr; }
    HRESULT get_status() const { return status; }
};

template<class T>
struct awaitable {
    promise<T>* promise = nullptr;
    bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        promise->invoke_continuation();
    }
    void await_resume() noexcept {
    }
};

template<class T>
class promise {
    std::variant<T, HRESULT> result = E_FAIL;
public:
    std::function<void(HRESULT, SIZE_T)> continuation;
    coroutine<T> get_return_object() { return { coroutine<T>::from_promise(*this) }; }
    std::suspend_always initial_suspend() noexcept { return {}; }
    awaitable<T> final_suspend() noexcept {
        return { this };
    }
    void return_value(T value) { 
        static_assert(!std::is_same_v<T, HRESULT>, "Return type must not match HRESULT");
        this->result = value;
    }
    void return_value(HRESULT hr) {
        this->result = hr;
    }
    void unhandled_exception() {}
    HRESULT get_status() const { return std::holds_alternative<HRESULT>(result) ? std::get<HRESULT>(result) : S_OK; }
    T get_value() const { return std::get<T>(result); }
    void invoke_continuation() {
        if (continuation){
            if (std::holds_alternative<T>(result)) {
                continuation(S_OK, sizeof(T));
            } else {
                continuation(get_status(), 0);
            }
        }
    }
};

template<>
class promise<void> {
    HRESULT result = E_FAIL;
public:
    std::function<void(HRESULT, SIZE_T)> continuation;
    coroutine<void> get_return_object() { return { coroutine<void>::from_promise(*this) }; }
    std::suspend_always initial_suspend() noexcept { return {}; }
    awaitable<void> final_suspend() noexcept {
        return { this };
    }
    void return_value(HRESULT hr) {
        this->result = hr;
    }
    void unhandled_exception() {}
    HRESULT get_status() const { return result; }
    void invoke_continuation() {
        if (continuation) {
            continuation(get_status(), 0);
        }
    }
};

struct dynamic_result {};

template<class T> class XAsync {
public:
    class Context;
    using work_callback = std::function<coroutine<T>(Context context)>;
    using store_result_callback = std::function<HRESULT(void* buffer, size_t size)>;
private:
    // Ensure self managed asyncBlock keeps alive until this object is destroyed
    std::shared_ptr<XAsyncBlock> asyncBlock = nullptr;
    struct Data {
        // Ensure self managed asyncBlock keeps alive until the asynchronous operation completes
        std::shared_ptr<XAsyncBlock> asyncBlock;
        XAsyncBlock* providerBlock;
        work_callback work;
        coroutine<T> coro;
        std::atomic<bool> isCanceled{false};
        store_result_callback store_result;
        XTaskQueueHandle queue = nullptr;

        XTaskQueueHandle getOrCreateQueue() { 
            if (!queue) {
                XTaskQueuePortHandle port;
                if (FAILED(XTaskQueueGetPort(providerBlock->queue, XTaskQueuePort::Work, &port))) {
                    return nullptr;
                }
                if (FAILED(XTaskQueueCreateComposite(port, port, &queue))) {
                    return nullptr;
                }
            }
            return queue;
        }

        ~Data() {
            if (queue) {
                XTaskQueueCloseHandle(queue);
            }
            coro.destroy();
        }
    };

    static HRESULT provider(XAsyncOp op, const XAsyncProviderData* data) {
        Data* contextData = static_cast<Data*>(data->context);
        switch (op) {
            case XAsyncOp::Begin:
                contextData->providerBlock = data->async;
                contextData->coro = contextData->work(Context(contextData));
                contextData->coro.promise().continuation = [providerBlock = contextData->providerBlock](HRESULT hr, SIZE_T length) {
                    XAsyncComplete(providerBlock, hr, length);
                };
                XAsyncSchedule(data->async, 0);
                break;
            case XAsyncOp::DoWork:
                contextData->coro.resume();
                if (!contextData->coro.done()) {
                    return E_PENDING;
                }
                break;
            case XAsyncOp::GetResult:
                if (std::is_same_v<T, dynamic_result> && contextData->store_result) {
                    contextData->store_result(data->buffer, data->bufferSize);
                    contextData->store_result = nullptr;
                    contextData->coro.resume();
                } else if (contextData->coro.done()) {
                    HRESULT status = contextData->coro.promise().get_status();
                    if (FAILED(status)) {
                        return status;
                    }
                    if constexpr (!std::is_same_v<T, void> && !std::is_same_v<T, dynamic_result>) {
                        *static_cast<T*>(data->buffer) = std::move(contextData->coro.promise().get_value());
                    }
                    return S_OK;
                } else {
                    return E_PENDING;
                }
                break;
            case XAsyncOp::Cancel:
                contextData->isCanceled = true;
                break;
            case XAsyncOp::Cleanup: {
                std::unique_ptr<Data> cleanupPtr(static_cast<Data*>(data->context));
                break;
            }
            default:
                break;
        }
        return S_OK;
    }

    static constexpr auto identity = provider;

    HRESULT begin() {
        HRESULT r = XAsyncBegin(asyncBlock.get(), data.get(), reinterpret_cast<const void*>(identity), __FUNCTION__, provider);
        if (FAILED(r)) {
            return r;
        }
        data.release();
        return S_OK;
    }

    XAsync(XAsyncBlock* block, work_callback work) : asyncBlock(std::shared_ptr<XAsyncBlock>(block, [](XAsyncBlock*) { /* not owning this block */})), data(std::make_unique<Data>()) {
        data->work = std::move(work);
    }
    std::unique_ptr<Data> data;
public:
    class Context {
        Data* data;
    public:
        struct switch_to_worker {
            Context context;
            UINT32 delay = 0;
            bool await_ready() { return false; }
            void await_suspend(std::coroutine_handle<> h) {
                XAsyncSchedule(context.data->providerBlock, delay);
            }
            void await_resume() { }
        };
        struct store_result {
            Context context;
            size_t required_buffer_size;
            store_result_callback store_result;

            bool await_ready() { return false; }
            void await_suspend(std::coroutine_handle<> h) {
                context.data->store_result = std::move(store_result);
                XAsyncComplete(context.data->providerBlock, S_OK, required_buffer_size);
            }
            void await_resume() { }
        };

        Context(Data* block) : data(block) {}
        switch_to_worker switchToWorker() { return { *this, 0 }; }
        switch_to_worker delay(UINT32 d) { return { *this, d }; }
        store_result storeResult(size_t required_buffer_size, store_result_callback store_result) {
            static_assert(std::is_same_v<T, dynamic_result>, "Only dynamic_result return type is supported for storeResult");
            return { *this, required_buffer_size, std::move(store_result) };
        }
        XTaskQueueHandle getOrCreateQueue() {
            return data->getOrCreateQueue();
        }
    };

    static HRESULT begin(XAsyncBlock* block, work_callback work) {
        return XAsync<T>(block, std::move(work)).begin();
    }

    template<class Y>
    static HRESULT getResult(XAsyncBlock* block, SIZE_T bufferSize, Y *buffer, SIZE_T *bufferUsed) {
        static_assert(std::is_same_v<T, dynamic_result> && (std::is_same_v<Y, void> || std::is_same_v<Y, unsigned char> || std::is_same_v<Y, char>) || std::is_same_v<T, Y> && std::is_trivially_copyable_v<Y>, "This type must be trivially copyable and match the expected type");
        return XAsyncGetResult(block, reinterpret_cast<const void*>(identity), bufferSize, buffer, bufferUsed);
    }

    XAsync(work_callback work) : data(std::make_unique<Data>()) {
        static_assert(!std::is_same_v<T, dynamic_result>, "Dynamic results are only supported for C facing async apis, use c++ types instead");
        asyncBlock = data->asyncBlock = std::make_shared<XAsyncBlock>();
        data->work = std::move(work);
    }

    XAsync& withQueue(XTaskQueueHandle queue) {
        static_assert(!std::is_same_v<T, dynamic_result>, "Dynamic results are not supported for withQueue");
        asyncBlock->queue = queue;
        return *this;
    }

    bool await_ready() {
        static_assert(!std::is_same_v<T, dynamic_result>, "Dynamic results are not supported for await_ready");
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) {
        static_assert(!std::is_same_v<T, dynamic_result>, "Dynamic results are not supported for await_suspend");
        asyncBlock->callback = [](XAsyncBlock* asyncBlock) {
            std::coroutine_handle<>::from_address(asyncBlock->context).resume();
        };
        asyncBlock->context = h.address();
        this->begin();
    }

    Result<T> await_resume() { 
        static_assert(!std::is_same_v<T, dynamic_result>, "Dynamic results are not supported for await_resume");
        HRESULT hr = XAsyncGetStatus(asyncBlock.get(), false);
        if (FAILED(hr)) {
            return hr;
        }
        if constexpr (std::is_same_v<T, void>) {
            return 0;
        } else {
            size_t resultSize = 0;
            hr = XAsyncGetResultSize(asyncBlock.get(), &resultSize);
            if (FAILED(hr)) {
                return hr;
            }
            if (resultSize != sizeof(T)) {
                return E_FAIL;
            }
            T result;
            hr = XAsyncGetResult(asyncBlock.get(), reinterpret_cast<const void*>(identity), resultSize, &result, nullptr);
            if (FAILED(hr)) {
                return hr;
            }
            return std::move(result);
        }
    }
};
}