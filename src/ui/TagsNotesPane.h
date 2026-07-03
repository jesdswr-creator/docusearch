#pragma once

// ============================================================
// TagsNotesPane.h - Tags + Notes panel matching reference design
// ============================================================
//
// Layout (vertical):
//   ┌──────────────────────────────────────────────────┐
//   │ Tags [🏷]                                         │
//   │ [official] [noc] [examination] [railway]         │
//   │ [+ Add Tag]                                       │
//   ├──────────────────────────────────────────────────┤
//   │ Notes [📝]                                        │
//   │ ┌──────────────────────────────────────────┐    │
//   │ │ Request letter for NOC to appear for ...  │    │
//   │ └──────────────────────────────────────────┘    │
//   │ Modified: 20 Aug 2025 10:28 AM                  │
//   └──────────────────────────────────────────────────┘
// ============================================================

#include <QWidget>
#include <QLineEdit>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QLayout>
#include <QList>

namespace DocuSearch {

class FlowLayout;

class TagsNotesPane : public QWidget {
    Q_OBJECT
public:
    explicit TagsNotesPane(QWidget* parent = nullptr);

    void setTags(const QStringList& tags);
    void setNote(const QString& note);
    void setFileId(qint64 id);

    QStringList tags() const;
    QString note() const;

    void refreshIcons();

signals:
    void tagAdded(qint64 fileId, const QString& tag);
    void tagRemoved(qint64 fileId, const QString& tag);
    void noteChanged(qint64 fileId, const QString& note);

private slots:
    void onAddTag();
    void onRemoveTag();
    void onNoteEdited();

private:
    qint64       fileId_ = 0;
    QLabel*      tagsHeaderLbl_   = nullptr;
    QWidget*     tagsContainer_   = nullptr;
    FlowLayout*  tagsLayout_      = nullptr;
    QPushButton* addTagBtn_       = nullptr;
    QLineEdit*   tagInput_        = nullptr;
    QLabel*      notesHeaderLbl_  = nullptr;
    QTextEdit*   noteEdit_        = nullptr;
    QLabel*      notesModifiedLbl_ = nullptr;
    QStringList  currentTags_;

    void rebuildTagPills();
    QString colorForTag(int index) const;
};

} // namespace DocuSearch
