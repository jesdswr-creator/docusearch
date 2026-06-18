// ============================================================
// TagsNotesPane.cpp
// ============================================================

#include "TagsNotesPane.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QKeyEvent>

namespace DocuSearch {

TagsNotesPane::TagsNotesPane(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);

    // Tags
    auto* tagBox = new QGroupBox("Tags", this);
    auto* tagLay = new QVBoxLayout(tagBox);

    auto* inputRow = new QHBoxLayout();
    tagInput_ = new QLineEdit(this);
    tagInput_->setPlaceholderText("Add tag (e.g. Urgent, VIP, Pending…)");
    addTagBtn_ = new QPushButton("Add", this);
    rmTagBtn_  = new QPushButton("Remove", this);
    inputRow->addWidget(tagInput_);
    inputRow->addWidget(addTagBtn_);
    inputRow->addWidget(rmTagBtn_);
    tagLay->addLayout(inputRow);

    tagList_ = new QListWidget(this);
    tagList_->setSelectionMode(QAbstractItemView::SingleSelection);
    tagLay->addWidget(tagList_);

    outer->addWidget(tagBox, 1);

    // Notes
    auto* noteBox = new QGroupBox("Notes", this);
    auto* noteLay = new QVBoxLayout(noteBox);
    noteEdit_ = new QTextEdit(this);
    noteEdit_->setPlaceholderText("Add notes for this file…");
    noteEdit_->setMaximumHeight(150);
    noteLay->addWidget(noteEdit_);
    outer->addWidget(noteBox, 1);

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
    // Avoid duplicates
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
