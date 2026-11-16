#pragma once
// Minimal RAII for WebKit C API refs. Avoids depending on WTF's WKRetainPtr.
#include <WebKit/WKType.h>
#include <string>

namespace meridian::engine {

template<typename T>
class WKPtr {
public:
    WKPtr() = default;
    static WKPtr adopt(T ref) { WKPtr p; p.m_ref = ref; return p; }
    static WKPtr retain(T ref) { if (ref) WKRetain(ref); return adopt(ref); }
    WKPtr(const WKPtr& o) : m_ref(o.m_ref) { if (m_ref) WKRetain(m_ref); }
    WKPtr(WKPtr&& o) noexcept : m_ref(o.m_ref) { o.m_ref = nullptr; }
    WKPtr& operator=(WKPtr o) noexcept { std::swap(m_ref, o.m_ref); return *this; }
    ~WKPtr() { if (m_ref) WKRelease(m_ref); }
    T get() const { return m_ref; }
    explicit operator bool() const { return m_ref != nullptr; }
private:
    T m_ref { nullptr };
};

template<typename T> WKPtr<T> adoptWK(T ref) { return WKPtr<T>::adopt(ref); }

// String helpers (UTF-16 <-> WKString/WKURL).
std::wstring toWString(WKStringRef);
std::wstring toWString(WKURLRef);
WKPtr<WKStringRef> makeWKString(const std::wstring&);
WKPtr<WKURLRef> makeWKURL(const std::wstring&);

} // namespace meridian::engine
