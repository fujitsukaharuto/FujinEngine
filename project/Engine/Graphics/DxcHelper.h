#pragma once
#include <dxcapi.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <atomic>
#include <stdexcept>
#include <cstring>

#pragma comment(lib, "dxcompiler.lib")

namespace Fujin {

using Microsoft::WRL::ComPtr;

namespace detail {
    inline ComPtr<IDxcUtils>&     DxcUtils()    { static ComPtr<IDxcUtils>     s; return s; }
    inline ComPtr<IDxcCompiler3>& DxcCompiler() { static ComPtr<IDxcCompiler3> s; return s; }

    inline void EnsureDxc() {
        auto& u = DxcUtils();
        auto& c = DxcCompiler();
        if (!u) {
            if (FAILED(DxcCreateInstance(CLSID_DxcUtils,    IID_PPV_ARGS(&u))) ||
                FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&c))))
                throw std::runtime_error("DxcCreateInstance failed");
        }
    }

    inline ComPtr<IDxcBlob> CompileBuffer(DxcBuffer& source, const wchar_t* target, const wchar_t* name) {
        // Derive include directory from file path so #include "Foo.hlsli" resolves correctly
        std::wstring includeDir;
        {
            std::wstring n(name);
            size_t slash = n.find_last_of(L"/\\");
            if (slash != std::wstring::npos)
                includeDir = n.substr(0, slash);
        }

        std::vector<LPCWSTR> args = {
            name,
            L"-E", L"main",
            L"-T", target,
#ifdef _DEBUGMODE
            L"-Zi", L"-Od",
#else
            L"-O3",
#endif
        };
        if (!includeDir.empty()) {
            args.push_back(L"-I");
            args.push_back(includeDir.c_str());
        }

        ComPtr<IDxcIncludeHandler> includeHandler;
        DxcUtils()->CreateDefaultIncludeHandler(&includeHandler);

        ComPtr<IDxcResult> result;
        if (FAILED(DxcCompiler()->Compile(&source, args.data(), (UINT32)args.size(),
                                           includeHandler.Get(), IID_PPV_ARGS(&result))))
            throw std::runtime_error("DXC compile call failed");

        HRESULT status = S_OK;
        result->GetStatus(&status);
        if (FAILED(status)) {
            ComPtr<IDxcBlobUtf8> errors;
            result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
            if (errors && errors->GetStringLength())
                OutputDebugStringA(errors->GetStringPointer());
            throw std::runtime_error("Shader compile failed");
        }

        ComPtr<IDxcBlob> blob;
        result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&blob), nullptr);
        return blob;
    }

    // Minimal COM blob that owns its binary data — used to wrap loaded .cso files.
    class RawBlob final : public IDxcBlob {
        std::vector<uint8_t> m_data;
        std::atomic<ULONG>   m_ref{ 1 };
    public:
        explicit RawBlob(std::vector<uint8_t> d) : m_data(std::move(d)) {}
        LPVOID  GetBufferPointer() noexcept override { return m_data.data(); }
        SIZE_T  GetBufferSize()    noexcept override { return m_data.size(); }
        HRESULT QueryInterface(REFIID iid, void** ppv) override {
            if (iid == __uuidof(IDxcBlob) || iid == __uuidof(IUnknown)) {
                *ppv = this; AddRef(); return S_OK;
            }
            return E_NOINTERFACE;
        }
        ULONG AddRef()  override { return ++m_ref; }
        ULONG Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }
    };

    // Derive compiled output path: "Resource/Shaders/Foo.VS.hlsl" -> "Resource/Shaders/Compiled/Foo.VS.cso"
    inline std::wstring CompiledPath(const wchar_t* hlslPath) {
        std::wstring p(hlslPath);
        size_t slash = p.find_last_of(L"/\\");
        std::wstring dir  = (slash != std::wstring::npos) ? p.substr(0, slash + 1) : L"";
        std::wstring file = (slash != std::wstring::npos) ? p.substr(slash + 1)    : p;
        size_t dot = file.rfind(L'.');
        if (dot != std::wstring::npos) file = file.substr(0, dot) + L".cso";
        return dir + L"Compiled/" + file;
    }
} // namespace detail

// ─── Runtime compile ──────────────────────────────────────────────────────────

inline ComPtr<IDxcBlob> CompileShaderDXC(const char* src, const wchar_t* target) {
    detail::EnsureDxc();
    DxcBuffer source = {};
    source.Ptr      = src;
    source.Size     = strlen(src);
    source.Encoding = DXC_CP_UTF8;
    return detail::CompileBuffer(source, target, L"<inline>");
}

inline ComPtr<IDxcBlob> CompileShaderFromFile(const wchar_t* filePath, const wchar_t* target) {
    detail::EnsureDxc();
    ComPtr<IDxcBlobEncoding> fileBlob;
    if (FAILED(detail::DxcUtils()->LoadFile(filePath, nullptr, &fileBlob)))
        throw std::runtime_error("Failed to load shader file");
    DxcBuffer source = {};
    source.Ptr      = fileBlob->GetBufferPointer();
    source.Size     = fileBlob->GetBufferSize();
    source.Encoding = DXC_CP_ACP;
    return detail::CompileBuffer(source, target, filePath);
}

// ─── Precompiled (.cso) loading ───────────────────────────────────────────────

// Load a precompiled shader blob from a .cso file.
inline ComPtr<IDxcBlob> LoadCompiledShader(const wchar_t* csoPath) {
    HANDLE hFile = CreateFileW(csoPath, GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Failed to open compiled shader (.cso)");

    LARGE_INTEGER sz{};
    GetFileSizeEx(hFile, &sz);

    std::vector<uint8_t> data(static_cast<size_t>(sz.QuadPart));
    DWORD read = 0;
    ReadFile(hFile, data.data(), static_cast<DWORD>(data.size()), &read, nullptr);
    CloseHandle(hFile);

    ComPtr<IDxcBlob> blob;
    blob.Attach(new detail::RawBlob(std::move(data)));
    return blob;
}

// In Debug: load precompiled if the .cso exists, otherwise compile from source.
//           No exception is thrown when the file is absent — avoids first-chance
//           noise in the VS debugger output.
// In Release: always load precompiled (.cso must exist).
inline ComPtr<IDxcBlob> LoadOrCompileShader(const wchar_t* hlslPath, const wchar_t* target) {
    std::wstring csoPath = detail::CompiledPath(hlslPath);
#ifdef _DEBUGMODE
    if (GetFileAttributesW(csoPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        return LoadCompiledShader(csoPath.c_str());
    return CompileShaderFromFile(hlslPath, target);
#else
    return LoadCompiledShader(csoPath.c_str());
#endif
}

} // namespace Fujin
