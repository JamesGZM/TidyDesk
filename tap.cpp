// SPDX-License-Identifier: MIT
// Original implementation using the Windows SDK XAML diagnostics interfaces.
#include "shared.h"
#undef GetCurrentTime
#pragma comment(linker, "/export:DllGetClassObject")
#pragma comment(linker, "/export:DllCanUnloadNow")
#include <windows.ui.xaml.h>
#include <xamlom.h>
#include <ocidl.h>
#include <wrl.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.h>
#include <atomic>
#include <vector>
#include <set>
#include <mutex>
#include <cwchar>
#include <memory>

using namespace Microsoft::WRL;
using namespace winrt::Windows::UI::Xaml;
namespace {
HMODULE moduleHandle = nullptr;
constexpr UINT ApplyMessage = WM_APP + 30;
constexpr UINT RemoveMessage = WM_APP + 31;
constexpr UINT StopMessage = WM_APP + 32;

struct Entry {
    InstanceHandle handle{};
    double applied = 0.0;
    bool overridden = false;
    winrt::weak_ref<FrameworkElement> element;
    winrt::Windows::Foundation::IInspectable local{nullptr};
};

class Tap final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IObjectWithSite,
                                       IVisualTreeServiceCallback2> {
    ComPtr<IXamlDiagnostics> diagnostics;
    ComPtr<IVisualTreeService> tree;
    HWND window = nullptr;
    HWND host = nullptr;
    HANDLE hostProcess = nullptr;
    HANDLE stopEvent = nullptr;
    std::atomic<bool> stopping{false};
    std::mutex handlesMutex;
    std::set<InstanceHandle> matched;
    // Accessed only on the SetSite UI thread.
    std::vector<Entry> entries;
    unsigned opacity = 0;

    void ReportError(HRESULT error) noexcept { PostMessageW(host, MsgError, static_cast<WPARAM>(error), 0); }

    bool Restore(Entry& item) noexcept {
        try {
            if (auto element = item.element.get()) {
                // If another customizer took ownership, do not overwrite its value.
                if (!item.overridden) return true;
                if (element.Opacity() != item.applied) { item.overridden = false; return true; }
                if (item.local == DependencyProperty::UnsetValue()) element.ClearValue(UIElement::OpacityProperty());
                else element.SetValue(UIElement::OpacityProperty(), item.local);
                item.overridden = false;
            }
            return true;
        } catch (...) { ReportError(winrt::to_hresult()); return false; }
    }

    void Update(Entry& item) noexcept {
        if (opacity == 100) { Restore(item); return; }
        try {
            if (auto element = item.element.get()) {
                if (item.overridden && element.Opacity() != item.applied) return;
                item.applied = static_cast<double>(opacity) / 100.0;
                item.overridden = true;
                element.Opacity(item.applied);
            }
        } catch (...) { ReportError(winrt::to_hresult()); }
    }
    void Apply(InstanceHandle handle) noexcept {
        if (stopping) return;
        for (const auto& item : entries) if (item.handle == handle) return;
        if (entries.size() >= 32) return;
        try {
            winrt::Windows::Foundation::IInspectable object{nullptr};
            winrt::check_hresult(diagnostics->GetIInspectableFromHandle(handle,
                reinterpret_cast<::IInspectable**>(winrt::put_abi(object))));
            auto element = object.try_as<FrameworkElement>();
            if (!element || winrt::get_class_name(element) != L"Taskbar.TaskbarBackground") return;
            // Never hide the TaskbarFrame or icon parents. Match background class exactly.
            auto local = element.ReadLocalValue(UIElement::OpacityProperty());
            if (local != DependencyProperty::UnsetValue() &&
                !local.try_as<winrt::Windows::Foundation::IPropertyValue>()) {
                ReportError(E_NOTIMPL); // Do not replace a data binding/expression.
                return;
            }
            Entry saved{handle, 0.0, false, winrt::make_weak(element), local};
            entries.push_back(saved); // Save rollback information before changing UI.
            Update(entries.back());
            PostMessageW(host, MsgChanged, entries.size(), 0);
        } catch (...) { ReportError(winrt::to_hresult()); }
    }

    static LRESULT CALLBACK WindowProc(HWND wnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        auto self = reinterpret_cast<Tap*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Tap*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(wnd, message, wparam, lparam);
        if (message == MsgSetOpacity) {
            if (self->stopping || wparam > 100) return 0;
            self->opacity = static_cast<unsigned>(wparam);
            for (auto& item : self->entries) self->Update(item);
            PostMessageW(self->host, MsgChanged, self->entries.size(), 0);
            return 0;
        }
        if (message == ApplyMessage) { self->Apply(static_cast<InstanceHandle>(wparam)); return 0; }
        if (message == RemoveMessage) {
            for (auto it = self->entries.begin(); it != self->entries.end(); ++it) {
                if (it->handle == static_cast<InstanceHandle>(wparam)) {
                    self->Restore(*it);
                    self->entries.erase(it);
                    PostMessageW(self->host, MsgChanged, self->entries.size(), 0);
                    break;
                }
            }
            return 0;
        }
        if (message == StopMessage) {
            unsigned failures = 0;
            for (auto& item : self->entries) if (!self->Restore(item)) ++failures;
            self->entries.clear();
            PostMessageW(self->host, MsgRestored, failures, 0);
            DestroyWindow(wnd);
            return 0;
        }
        if (message == WM_NCDESTROY) {
            SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
            if (self->window == wnd) self->Release(); // UI-window lifetime reference.
        }
        return DefWindowProcW(wnd, message, wparam, lparam);
    }

    static DWORD WINAPI Worker(void* context) noexcept {
        auto self = static_cast<Tap*>(context);
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const HRESULT advised = self->tree->AdviseVisualTreeChange(self);
        if (FAILED(advised)) self->ReportError(advised);
        else PostMessageW(self->host, MsgAttached, reinterpret_cast<WPARAM>(self->window), 0);
        if (SUCCEEDED(advised)) {
            HANDLE waits[]{self->hostProcess, self->stopEvent};
            WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            self->stopping = true;
            const auto result = self->tree->UnadviseVisualTreeChange(self);
            if (FAILED(result)) self->ReportError(result);
        }
        self->stopping = true;
        PostMessageW(self->window, StopMessage, 0, 0);
        if (SUCCEEDED(initialized)) CoUninitialize();
        self->Release(); // Worker lifetime reference.
        return 0;
    }
public:
    ~Tap() {
        if (hostProcess) CloseHandle(hostProcess);
        if (stopEvent) CloseHandle(stopEvent);
    }
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) override {
        if (!site) {
            if (stopEvent) SetEvent(stopEvent);
            return S_OK;
        }
        if (diagnostics) return E_UNEXPECTED;
        HRESULT result = site->QueryInterface(IID_PPV_ARGS(&diagnostics));
        if (FAILED(result)) return result;
        result = site->QueryInterface(IID_PPV_ARGS(&tree));
        if (FAILED(result)) return result;
        BSTR data = nullptr;
        result = diagnostics->GetInitializationData(&data);
        if (FAILED(result)) return result;
        unsigned long pid = 0;
        unsigned long long hwnd = 0;
        const int parsed = data ? swscanf_s(data, L"%lu:%llu", &pid, &hwnd) : 0;
        SysFreeString(data);
        if (parsed != 2 || !pid || !hwnd) return E_INVALIDARG;
        host = reinterpret_cast<HWND>(static_cast<UINT_PTR>(hwnd));
        DWORD actualPid = 0;
        GetWindowThreadProcessId(host, &actualPid);
        if (actualPid != pid) return E_INVALIDARG;
        hostProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (!hostProcess) return HRESULT_FROM_WIN32(GetLastError());
        wchar_t eventName[96]{};
        swprintf_s(eventName, L"Local\\LiteTaskbar.Stop.%lu", pid);
        stopEvent = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, eventName);
        if (!stopEvent) return HRESULT_FROM_WIN32(GetLastError());
        // Keep callback code mapped until Explorer exits; never unload beneath XAML.
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(&moduleHandle), &pinned))
            return HRESULT_FROM_WIN32(GetLastError());
        WNDCLASSW wc{};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = moduleHandle;
        wc.lpszClassName = TapClass;
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return HRESULT_FROM_WIN32(GetLastError());
        AddRef();
        window = CreateWindowExW(0, TapClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, moduleHandle, this);
        if (!window) { Release(); return HRESULT_FROM_WIN32(GetLastError()); }
        AddRef();
        HANDLE worker = CreateThread(nullptr, 0, Worker, this, 0, nullptr);
        if (!worker) {
            const auto error = HRESULT_FROM_WIN32(GetLastError());
            Release();
            DestroyWindow(window);
            return error;
        }
        CloseHandle(worker);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSite(REFIID iid, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        return diagnostics ? diagnostics->QueryInterface(iid, output) : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation, VisualElement element,
                                                 VisualMutationType mutation) override {
        if (stopping) return S_OK;
        try {
            std::lock_guard<std::mutex> lock(handlesMutex);
            if (mutation == Add && element.Type && wcscmp(element.Type, L"Taskbar.TaskbarBackground") == 0) {
                if (matched.size() < 32 && matched.insert(element.Handle).second)
                    PostMessageW(window, ApplyMessage, static_cast<WPARAM>(element.Handle), 0);
            } else if (mutation == Remove && matched.erase(element.Handle)) {
                PostMessageW(window, RemoveMessage, static_cast<WPARAM>(element.Handle), 0);
            }
        } catch (...) { ReportError(E_OUTOFMEMORY); }
        return S_OK; // Do not propagate our customization failure into Explorer.
    }
    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState, LPCWSTR) override { return S_OK; }
};

class Factory final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IClassFactory> {
public:
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto instance = Make<Tap>();
        return instance ? instance->QueryInterface(iid, output) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};
}
STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** output) {
    if (!output) return E_POINTER;
    *output = nullptr;
    if (clsid != TapClsid) return CLASS_E_CLASSNOTAVAILABLE;
    auto factory = Make<Factory>();
    return factory ? factory->QueryInterface(iid, output) : E_OUTOFMEMORY;
}
STDAPI DllCanUnloadNow() { return S_FALSE; }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { moduleHandle = instance; DisableThreadLibraryCalls(instance); }
    return TRUE;
}
