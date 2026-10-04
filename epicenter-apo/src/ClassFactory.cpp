#include "EpicenterApo.h"
#include <new>

namespace epicenter {
namespace apo {

class CEpicenterApoClassFactory final : public IClassFactory {
public:
    CEpicenterApoClassFactory() = default;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        *ppvObject = nullptr;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppvObject = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (count == 0) delete this;
        return count;
    }

    // IClassFactory
    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        *ppvObject = nullptr;
        if (pUnkOuter != nullptr) return CLASS_E_NOAGGREGATION;

        auto* apo = new (std::nothrow) CEpicenterApo();
        if (!apo) return E_OUTOFMEMORY;

        HRESULT hr = apo->QueryInterface(riid, ppvObject);
        apo->Release();
        return hr;
    }

    STDMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) {
            serverLocks_.fetch_add(1, std::memory_order_relaxed);
        } else {
            serverLocks_.fetch_sub(1, std::memory_order_relaxed);
        }
        return S_OK;
    }

    static bool canUnload() {
        return serverLocks_.load(std::memory_order_acquire) == 0;
    }

private:
    std::atomic<ULONG> refCount_{1};
    static inline std::atomic<LONG> serverLocks_{0};
};

IClassFactory* createClassFactory() {
    return new (std::nothrow) CEpicenterApoClassFactory();
}

bool canServerUnload() {
    return CEpicenterApoClassFactory::canUnload();
}

} // namespace apo
} // namespace epicenter
