#pragma once

// ============================================================
// JumpList.h - Native Windows 7+ taskbar jump list
// ============================================================
//
// Qt6 dropped the QWinJumpList helper from Qt5::WindowsExtras, so we
// implement the same functionality directly against the Win32
// ICustomDestinationList COM interface. This works on Windows 7, 8,
// 10, and 11 - including the modern Windows 11 taskbar.
//
// Usage:
//   JumpList jl;
//   jl.addTask("Open Settings", "--settings");
//   jl.addTask("Pause Indexing", "--pause");
//   jl.addSeparator();
//   jl.commit();
//
// Tasks appear under the right-click menu of the app's taskbar icon.
// ============================================================

#include <QString>
#include <QStringList>
#include <vector>

namespace DocuSearch {
namespace Win {

class JumpList {
public:
    JumpList();
    ~JumpList();

    JumpList(const JumpList&)            = delete;
    JumpList& operator=(const JumpList&) = delete;

    // Add a user task. `title` is the display name; `args` are the
    // command-line arguments passed to DocuSearch.exe when the task
    // is clicked.
    void addTask(const QString& title, const QString& args);

    // Insert a separator between tasks.
    void addSeparator();

    // Begin a "Recent" category - files added via addRecentFile()
    // will appear here. Calling this is optional; if you skip it,
    // only the Tasks list is shown.
    void enableRecentCategory();

    // Add a file to the Recent category (by absolute path).
    void addRecentFile(const QString& path);

    // Write the jump list to the shell. Returns true on success.
    // After commit, the JumpList object is empty and can be reused.
    bool commit();

private:
    struct Task {
        QString title;
        QString args;
        bool   isSeparator = false;
    };
    std::vector<Task> tasks_;
    QStringList       recent_;
    bool              wantRecent_ = false;
};

} // namespace Win
} // namespace DocuSearch
