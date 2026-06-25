// ============================================================
// TagsNotesPane.cpp — Compact tags + notes side-by-side
// ============================================================

#include "TagsNotesPane.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QKeyEvent>
#include <QScrollArea>

namespace DocuSearch {

TagsNotesPane::TagsNotesPane(QWidget* parent) : QWidget(parent) {
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    // Tags (left)
    auto* tagBox = new QGroupBox("Tags", this);
    tagBox->setStyleSheet("QGroupBox { font-size: 11px; font-weight: bold; border: 1px solid #ccc; border-radius: 4px; margin-top: 8px; padding-top: 4px; }");
    auto* tagLay = new QVBoxLayout(tagBox);
    tagLay->setSpacing(2);

    auto* inputRow = new QHBoxLayout();
    inputRow->setSpacing(2);
    tagInput_ = new QLineEdit(this);
    tagInput_->setPlaceholderText("Add tag...");
    tagInput_->setMaximumHeight(24);
    addTagBtn_ = new QPushButton("Add", this);
    addTagBtn_->setMaximumHeight(24);
    addTagBtn_->setStyleSheet("QPushButton { font-size: 11px; padding: 2px 8px; }");
    rmTagBtn_  = new QPushButton("X", this);
    rmTagBtn_->setMaximumHeight(24);
    rmTagBtn_->setMaximumWidth(28);
    rmTagBtn_->setStyleSheet("QPushButton { font-size: 11px; padding: 2px 4px; }");
    inputRow->addWidget(tagInput_);
    inputRow->addWidget(addTagBtn_);
    inputRow->addWidget(rmTagBtn_);
    tagLay->addLayout(inputRow);

    tagList_ = new QListWidget(this);
    tagList_->setSelectionMode(QAbstractItemView::SingleSelection);
    tagList_->setMaximumHeight(80);
    tagList_->setStyleSheet("QListWidget { font-size: 11px; border: 1px solid #ddd; border-radius: 3px; }");
    tagLay->addWidget(tagList_);

    outer->addWidget(tagBox, 1);

    // Notes (right)
    auto* noteBox = new QGroupBox("Notes", this);
    noteBox->setStyleSheet("QGroupBox { font-size: 11px; font-weight: bold; border: 1px solid #ccc; border-radius: 4px; margin-top: 8px; padding-top: 4px; }");
    auto* noteLay = new QVBoxLayout(noteBox);
    noteLay->setSpacing(2);
    noteEdit_ = new QTextEdit(this);
    noteEdit_->setPlaceholderText("Add notes...");
    noteEdit_->setStyleSheet("QTextEdit { font-size: 11px; border: 1px solid #ddd; border-radius: 3px; }");
    noteLay->addWidget(noteEdit_);

    outer->addWidget(noteBox, 2);

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
