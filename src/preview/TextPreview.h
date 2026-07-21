#pragma once

// ============================================================
// TextPreview.h - Preview widget for plain-text files
// ============================================================

#include "IFilePreview.h"

#include <QWidget>
#include <QPlainTextEdit>
#include <QLabel>

namespace DocuSearch {

class TextPreview : public QWidget, public IFilePreview {
    Q_OBJECT
public:
    explicit TextPreview(QWidget* parent = nullptr);

    // IFilePreview interface
    bool loadFile(const QString& filePath) override;
    QWidget* getWidget() override { return this; }
    void clear() override;
    QString getTypeName() const override { return "Text"; }

private:
    QPlainTextEdit* m_textEdit   = nullptr;
    QLabel*         m_infoLabel  = nullptr;

    static constexpr int MAX_BYTES = 50 * 1024;  // 50 KB preview cap
};

} // namespace DocuSearch
