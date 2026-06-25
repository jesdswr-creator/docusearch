// ============================================================
// JumpList.cpp - Native Win32 ICustomDestinationList wrapper
// ============================================================
//
// Implements the Windows shell jump-list API. We use the "Tasks"
// custom destination list, which appears when the user right-clicks
// the app's taskbar button or looks at its Start-menu tile.
//
// All COM objects are released via RAII smart pointers (Microsoft::WRL
// would be ideal, but we keep dependencies minimal by using
// std::unique_ptr with a custom deleter).
// ============================================================

#include "JumpList.h"
#include "../core/Logger.h"
#include "../core/Constants.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shobjidl.h>
#  include <objbase.h>
#  include <shlguid.h>
#  include <propkey.h>
#  include <propvarutil.h>
#  include <shellapi.h>
#  include <knownfolders.h>
#  include <shlobj.h>
#  pragma comment(lib, "ole32.lib")
#  pragma comment(lib, "shell32.lib")
#  pragma comment(lib, "propsys.lib")
#endif

#include <memory>
#include <QDir>
#include <QRegularExpression>

namespace DocuSearch {
namespace Win {

#ifdef Q_OS_WIN

// RAII wrapper for COM initialization
class ComScope {
public:
    ComScope() : hr_(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() { if (SUCCEEDED(hr_)) ::CoUninitialize(); }
    bool ok() const { return SUCCEEDED(hr_); }
private:
    HRESULT hr_;
};

// Deleter for IUnknown-style COM objects
struct ComReleaser {
    template<typename T>
    void operator()(T* p) const { if (p) p->Release(); }
};
template<typename T>
using com_ptr = std::unique_ptr<T, ComReleaser>;

// Convert a QString to a wide-string BSTR allocated via SysAllocString.
// Caller must SysFreeString the result.
static BSTR toBstr(const QString& s) {
    const std::wstring w = s.toStdWString();
    return ::SysAllocStringLen(w.c_str(), static_cast<UINT>(w.size()));
}

// Build a shell link (IShellLinkW) pointing at our own .exe with the
// given arguments. The returned IShellLinkW has its refcount at 1.
static com_ptr<IShellLinkW> makeShellLink(const QString& args) {
    IShellLinkW* raw = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&raw)))) {
        return nullptr;
    }
    com_ptr<IShellLinkW> link(raw);

    // Path to DocuSearch.exe - derived from the running process
    wchar_t exePath[MAX_PATH] = {0};
    ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (FAILED(link->SetPath(exePath))) return nullptr;
    if (FAILED(link->SetArguments(args.toStdWString().c_str()))) return nullptr;

    // Show normal window
    link->SetShowCmd(SW_SHOWNORMAL);

    // Description shows up as a tooltip. SetDescription takes a wide string
    // (LPCWSTR); convert the UTF-8 char* from Constants::kAppName.
    const std::wstring wDesc = QString::fromUtf8(Constants::kAppName).toStdWString();
    link->SetDescription(wDesc.c_str());

    return link;
}

// Convert an IShellLinkW into an IShellItem (the type the shell wants
// when adding an object to a jump list). We do this by persisting the
// link to a temporary .lnk file in %TEMP% and then creating an
// IShellItem from that path.
static com_ptr<IShellItem> linkToShellItem(IShellLinkW* link,
                                            const QString& title) {
    // Persist the link
    com_ptr<IPersistFile> pf;
    {
        IPersistFile* raw = nullptr;
        if (FAILED(link->QueryInterface(IID_PPV_ARGS(&raw)))) return nullptr;
        pf.reset(raw);
    }

    // Temp .lnk path: %TEMP%\DocuSearch_<title>.lnk
    const QString tempDir = QDir::tempPath();
    QString slug = title;
    slug.replace(QRegularExpression("[^A-Za-z0-9_-]+"), "_");
    const QString lnkPath = tempDir + QStringLiteral("/DocuSearch_") +
                             slug + QStringLiteral(".lnk");
    const std::wstring w = lnkPath.toStdWString();
    if (FAILED(pf->Save(w.c_str(), TRUE))) return nullptr;

    // Create a shell item from the path
    IShellItem* raw = nullptr;
    if (FAILED(::SHCreateItemFromParsingName(w.c_str(), nullptr,
                                              IID_PPV_ARGS(&raw)))) {
        return nullptr;
    }
    return com_ptr<IShellItem>(raw);
}

#endif // Q_OS_WIN

// ============================================================
// JumpList public API
// ============================================================

JumpList::JumpList()  = default;
JumpList::~JumpList() = default;

void JumpList::addTask(const QString& title, const QString& args) {
    tasks_.push_back({title, args, false});
}

void JumpList::addSeparator() {
    tasks_.push_back({QString(), QString(), true});
}

void JumpList::enableRecentCategory() {
    wantRecent_ = true;
}

void JumpList::addRecentFile(const QString& path) {
    recent_.append(path);
}

bool JumpList::commit() {
#ifdef Q_OS_WIN
    ComScope com;
    if (!com.ok()) {
        DS_WARN("JumpList", "COM init failed");
        return false;
    }

    // Get the ICustomDestinationList interface
    ICustomDestinationList* rawCdl = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_DestinationList, nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&rawCdl)))) {
        DS_WARN("JumpList", "CoCreateInstance(CLSID_DestinationList) failed");
        return false;
    }
    com_ptr<ICustomDestinationList> cdl(rawCdl);

    // Begin list generation. The shell hands us a max slot count.
    UINT maxSlots = 0;
    IObjectArray* rawRemoved = nullptr;
    if (FAILED(cdl->BeginList(&maxSlots, IID_PPV_ARGS(&rawRemoved)))) {
        DS_WARN("JumpList", "BeginList failed");
        return false;
    }
    com_ptr<IObjectArray> removed(rawRemoved);

    // ---- Tasks category -------------------------------------------------
    if (!tasks_.empty()) {
        IObjectCollection* rawCol = nullptr;
        if (FAILED(::CoCreateInstance(CLSID_EnumerableObjectCollection,
                                       nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&rawCol)))) {
            DS_WARN("JumpList", "CoCreateInstance(EnumerableObjectCollection) failed");
            return false;
        }
        com_ptr<IObjectCollection> col(rawCol);

        for (const auto& t : tasks_) {
            if (t.isSeparator) {
                IShellLinkW* sepLink = nullptr;
                // Create a "separator" by adding a link with the special
                // separator property - we use IShellLinkW with empty args
                // and set System.AppUserModel.IsDestListSeparator = TRUE.
                if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr,
                                               CLSCTX_INPROC_SERVER,
                                               IID_PPV_ARGS(&sepLink)))) {
                    continue;
                }
                com_ptr<IShellLinkW> sep(sepLink);
                // Set the separator property via IPropertyStore
                IPropertyStore* rawPs = nullptr;
                if (SUCCEEDED(sep->QueryInterface(IID_PPV_ARGS(&rawPs)))) {
                    com_ptr<IPropertyStore> ps(rawPs);
                    PROPVARIANT pv;
                    ::InitPropVariantFromBoolean(TRUE, &pv);
                    ps->SetValue(PKEY_AppUserModel_IsDestListSeparator, pv);
                    ps->Commit();
                    ::PropVariantClear(&pv);
                }
                col->AddObject(sep.get());
                continue;
            }

            auto link = makeShellLink(t.args);
            if (!link) continue;

            // Set the title via IPropertyStore (System.Title)
            IPropertyStore* rawPs = nullptr;
            if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&rawPs)))) {
                com_ptr<IPropertyStore> ps(rawPs);
                PROPVARIANT pv;
                ::InitPropVariantFromString(t.title.toStdWString().c_str(), &pv);
                ps->SetValue(PKEY_Title, pv);
                ps->Commit();
                ::PropVariantClear(&pv);
            }
            col->AddObject(link.get());
        }

        // Hand the collection to the shell as the "Tasks" category
        IObjectArray* rawArr = nullptr;
        if (SUCCEEDED(col->QueryInterface(IID_PPV_ARGS(&rawArr)))) {
            com_ptr<IObjectArray> arr(rawArr);
            cdl->AddUserTasks(arr.get());
        }
    }

    // ---- Recent category (optional) ------------------------------------
    if (wantRecent_) {
        cdl->AppendKnownCategory(KDC_RECENT);
    }

    // Commit
    HRESULT hr = cdl->CommitList();
    if (FAILED(hr)) {
        DS_WARN("JumpList", QString("CommitList failed: hr=0x%1")
                 .arg(static_cast<ulong>(hr), 8, 16, QChar('0')));
        return false;
    }

    DS_INFO("JumpList", QString("Committed %1 task(s)").arg(tasks_.size()));
    tasks_.clear();
    recent_.clear();
    wantRecent_ = false;
    return true;
#else
    Q_UNUSED(tasks_); Q_UNUSED(recent_); Q_UNUSED(wantRecent_);
    return false;
#endif
}

} // namespace Win
} // namespace DocuSearch
