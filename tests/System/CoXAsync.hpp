#include <xasync.h>
#include <xasyncprovider.h>
#include <memory>
#include <optional>
#include <variant>
#include <functional>

// template<class T> class Result {
//     HRESULT hr;
//     std::optional<T> value;
//     std::variant<T, HRESULT, XAsync
// public:
//     Result(HRESULT hr) : hr(hr) {}
//     Result(T value) : hr(S_OK), value(value) {}
//     HRESULT getHr() const { return hr; }
//     std::optional<T> getValue() const { return value; }
// };
namespace CoXAsync {
template<class T> class XAsync {
private:
    XAsyncBlock* asyncBlock;
    struct Data {
        XAsyncBlock asyncBlock;
        // Custom data for the async operation
    };

    std::unique_ptr<Data> data;
public:
    XAsync(XAsyncBlock* block) : asyncBlock(block), data(std::make_unique<Data>()) {}

    XAsync() : data(std::make_unique<Data>()) {
        asyncBlock = &data->asyncBlock;
    }

    XAsync& withQueue(XTaskQueueHandle queue) {
        asyncBlock->queue = queue;
        return *this;
    }

    XAsync& then(std::function<void(std::variant<T, HRESULT> result)> callback) {
        // asyncBlock->callback = callback;

        return *this;
    }

    HRESULT begin(std::function<std::variant<T, HRESULT, XAsync<T>>(XAsyncBlock*)> work) {
        HRESULT r = XAsyncBegin(asyncBlock, data.get(), nullptr/*(const void*)(HRESULT(XAsync<int>::*)(std::function<std::variant<int, HRESULT, XAsync<int>> (XAsyncBlock *)>))&XAsync<T>::begin*/, __FUNCTION__, [](XAsyncOp op, const XAsyncProviderData* data) -> HRESULT {
            switch (op) {
                case XAsyncOp::Begin:
                    // Handle begin
                    break;
                case XAsyncOp::DoWork:
                    // Handle work
                    break;
                case XAsyncOp::GetResult:
                    // Handle get result
                    break;
                case XAsyncOp::Cancel:
                    // Handle cancel
                    break;
                case XAsyncOp::Cleanup: {
                    std::unique_ptr<Data> cleanupPtr(static_cast<Data*>(data->context));
                    break;
                }
                default:
                    break;
            }
            return S_OK;
        });
        if (FAILED(r)) {
            return r;
        }
        // Keep it alive delegate lifetime to provider
        data.release();
        return S_OK;
    }
};
}