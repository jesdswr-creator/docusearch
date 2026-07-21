#pragma once

// ============================================================
// IFilePreview.h - Abstract interface for file preview widgets
// ============================================================
//
// Each concrete preview (PDF, Image, Text, Office) implements this
// interface. PreviewPane uses the interface to route files to the
// correct preview widget without knowing the concrete type.
//
// SAFETY CONTRACT:
//   - loadFile() MUST NOT throw exceptions to the caller.
//   - All errors must be handled internally and shown as fallback UI.
//   - clear() must be safe to call multiple times.
//   - getWidget() must never return null.
// ============================================================

#include <QString>
#include <QWidget>

namespace DocuSearch {

class IFilePreview {
public:
    virtual ~IFilePreview() = default;

    // Load and display the file. Returns true on success, false on
    // failure (file not found, too large, corrupted, etc.). On failure,
    // the widget should show a meaningful fallback message.
    virtual bool loadFile(const QString& filePath) = 0;

    // Returns the QWidget pointer for embedding in layouts.
    // Never returns null.
    virtual QWidget* getWidget() = 0;

    // Clears all loaded content and releases resources. Safe to call
    // multiple times. Resets to initial state.
    virtual void clear() = 0;

    // Returns a human-readable name like "PDF", "Image", "Text", "Office".
    virtual QString getTypeName() const = 0;
};

} // namespace DocuSearch
