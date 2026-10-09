// SPDX-License-Identifier: GPL-3.0-only
#ifdef _WIN32
#include "asma/core/Trash.h"

#include "TrashCommon.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace fs = std::filesystem;

namespace asma {

namespace {

// COM for this thread, for as long as the object lives.
struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ~ComScope()
    {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};

template <class T> struct Ref {
    T* p = nullptr;
    ~Ref()
    {
        if (p) p->Release();
    }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};

// Hears where the deleted item went, and stops a delete that would not go to
// the Recycle Bin: the shell deletes for good when a drive has none.
class Sink final : public IFileOperationProgressSink {
public:
    fs::path where;
    bool refused = false;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override
    {
        if (riid == IID_IUnknown || riid == IID_IFileOperationProgressSink) {
            *out = static_cast<IFileOperationProgressSink*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs_; } // lives on the stack

    HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD flags, IShellItem*) override
    {
        if (flags & TSF_DELETE_RECYCLE_IF_POSSIBLE) return S_OK;
        refused = true;
        return E_ABORT;
    }
    HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem*, HRESULT result, IShellItem* created) override
    {
        if (SUCCEEDED(result) && created) {
            PWSTR path = nullptr;
            if (SUCCEEDED(created->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                where = fs::path(path);
                CoTaskMemFree(path);
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE StartOperations() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UpdateProgress(UINT, UINT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResetTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PauseTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResumeTimer() override { return S_OK; }

private:
    ULONG refs_ = 1;
};

std::wstring volumeOf(const fs::path& file)
{
    wchar_t volume[MAX_PATH + 1] = {};
    if (!GetVolumePathNameW(fs::absolute(file).c_str(), volume, MAX_PATH)) return {};
    return volume;
}

} // namespace

bool trashAvailable(const fs::path& file)
{
    const std::wstring volume = volumeOf(file);
    if (volume.empty()) return false;
    // Only fixed drives have a Recycle Bin by default; asking it answers for the rest.
    if (GetDriveTypeW(volume.c_str()) == DRIVE_FIXED) return true;
    SHQUERYRBINFO info{};
    info.cbSize = sizeof info;
    return SUCCEEDED(SHQueryRecycleBinW(volume.c_str(), &info));
}

TrashResult moveToTrash(const fs::path& file)
{
    TrashResult result;
    ComScope com;
    Ref<IFileOperation> op;
    Ref<IShellItem> item;
    if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op)))
        || FAILED(SHCreateItemFromParsingName(fs::absolute(file).c_str(), nullptr, IID_PPV_ARGS(&item)))) {
        result.error = "cannot reach the Recycle Bin";
        return result;
    }
    // The sink is the only guard against a delete that would not recycle:
    // without it, or without these flags, nothing is deleted at all.
    Sink sink;
    DWORD cookie = 0;
    if (FAILED(op->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI
                                     | FOFX_RECYCLEONDELETE | FOFX_EARLYFAILURE))
        || FAILED(op->Advise(&sink, &cookie))) {
        result.error = "cannot reach the Recycle Bin";
        return result;
    }
    const HRESULT queued = op->DeleteItem(item.p, nullptr);
    const HRESULT done = SUCCEEDED(queued) ? op->PerformOperations() : queued;
    BOOL aborted = FALSE;
    op->GetAnyOperationsAborted(&aborted);
    op->Unadvise(cookie);
    std::error_code ec;
    if (SUCCEEDED(done) && !aborted && !sink.where.empty() && !fs::exists(file, ec)) {
        result.ok = true;
        result.where = sink.where;
    } else {
        result.error = sink.refused ? "there is no Recycle Bin on its drive" : "cannot move it to the Recycle Bin";
    }
    return result;
}

std::string restoreFromTrash(const fs::path& where, const fs::path& to)
{
    const std::string error = detail::renameBack(where, to);
    if (error.empty()) {
        // The Recycle Bin keeps what it knows of $Rxxxx.ext in $Ixxxx.ext.
        const std::wstring name = where.filename().wstring();
        if (name.size() > 2 && name[0] == L'$' && name[1] == L'R') {
            std::error_code ec;
            fs::remove(where.parent_path() / (L"$I" + name.substr(2)), ec);
        }
    }
    return error;
}

} // namespace asma
#endif
