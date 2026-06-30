// ============================================================
// TagsNotesPane.cpp - Tags (top) + Notes (bottom), stacked vertically
// ============================================================

#include "TagsNotesPane.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QKeyEvent>
#include <QScrollArea>
#include <QLabel>

namespace DocuSearch {

TagsNotesPane::TagsNotesPane(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    // ---- Tags section ----
    auto* tagsHeader = new QLabel("TAGS", this);
    tagsHeader->setObjectName("sectionLabel");
    outer->addWidget(tagsHeader);

    // Tag input on its own row.
    tagInput_ = new QLineEdit(this);
    tagInput_->setPlaceholderText("Add tag...");
    tagInput_->setMaximumHeight(26);
    outer->addWidget(tagInput_);

    // Buttons row: Add + Remove.
    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(3);
    addTagBtn_ = new QPushButton("+ Add", this);
    addTagBtn_->setMaximumHeight(24);
    addTagBtn_->setCursor(Qt::PointingHandCursor);
    addTagBtn_->setDefault(true);
    rmTagBtn_ = new QPushButton("Remove", this);
    rmTagBtn_->setMaximumHeight(24);
    rmTagBtn_->setCursor(Qt::PointingHandCursor);
    btnRow->addWidget(addTagBtn_);
    btnRow->addWidget(rmTagBtn_);
    outer->addLayout(btnRow);

    // Tag list — no inline style, Theme QSS handles colors.
    tagList_ = new QListWidget(this);
    tagList_->setSelectionMode(QAbstractItemView::SingleSelection);
    tagList_->setMinimumHeight(30);
    outer->addWidget(tagList_, 1);

    // ---- Notes section ----
    auto* notesHeader = new QLabel("NOTES", this);
    notesHeader->setObjectName("sectionLabel");
    outer->addWidget(notesHeader);

    noteEdit_ = new QTextEdit(this);
    noteEdit_->setPlaceholderText("Add notes...");
    noteEdit_->setMinimumHeight(30);
    outer->addWidget(noteEdit_, 2);

    connect(addTagBtn_, &QPushButton::clicked, this, &TagsNotesPane::onAddTag);
    connect(rmTagBtn_,  &QPushButton::clicked, this, &TagsNotesPane::onRemoveTag);
    connect(tagInput_,  &QLineEdit::returnPressed, this, &TagsNotesPane::onAddTag);
    connect(noteEdit_,  &QTextEdit::textChanged,   this, &TagsNotesPane::onNoteEdited);
}

void TagsNotesPane::setFileId(qint64 id) { fileId_ = id; }

void TagsNotesPane::setTags(const QStringList& tags) {
    tagList_->clear();
    for (const auto& t : tags) tagList_->addItem(t);
}

void TagsNotesPane::setNote(const QString& note) {
    noteEdit_->blockSignals(true);
    noteEdit_->setPlainText(note);
    noteEdit_->blockSignals(false);
}

QStringList TagsNotesPane::tags() const {
    QStringList out;
    for (int i = 0; i < tagList_->count(); ++i) out << tagList_->item(i)->text();
    return out;
}

QString TagsNotesPane::note() const { return noteEdit_->toPlainText(); }

void TagsNotesPane::onAddTag() {
    const QString t = tagInput_->text().trimmed();
    if (t.isEmpty() || fileId_ == 0) return;
    for (int i = 0; i < tagList_->count(); ++i) {
        if (tagList_->item(i)->text() == t) {
            tagInput_->clear();
            return;
        }
    }
    tagList_->addItem(t);
    tagInput_->clear();
    emit tagAdded(fileId_, t);
}

void TagsNotesPane::onRemoveTag() {
    auto* cur = tagList_->currentItem();
    if (!cur) return;
    const QString t = cur->text();
    delete cur;
    emit tagRemoved(fileId_, t);
}

void TagsNotesPane::onNoteEdited() {
    if (fileId_ != 0) emit noteChanged(fileId_, noteEdit_->toPlainText());
}

} // namespace DocuSearch
