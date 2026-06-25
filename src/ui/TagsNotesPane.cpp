// ============================================================
// TagsNotesPane.cpp - Compact tags + notes side-by-side
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

    const QString groupStyle =
        "QGroupBox { font-size: 11px; font-weight: bold; "
        "  border: 1px solid #ccc; border-radius: 4px; "
        "  margin-top: 8px; padding-top: 4px; }";
    const QString fieldStyle =
        "QListWidget, QTextEdit, QLineEdit { font-size: 11px; "
        "  border: 1px solid #ddd; border-radius: 3px; }";

    // Tags (left) - stretch=1, equal to Notes
    auto* tagBox = new QGroupBox("Tags", this);
    tagBox->setStyleSheet(groupStyle);
    tagBox->setMinimumWidth(120);
    auto* tagLay = new QVBoxLayout(tagBox);
    tagLay->setContentsMargins(4, 4, 4, 4);
    tagLay->setSpacing(2);

    auto* inputRow = new QHBoxLayout();
    inputRow->setSpacing(2);
    tagInput_ = new QLineEdit(tagBox);
    tagInput_->setPlaceholderText("Add tag...");
    tagInput_->setMaximumHeight(24);
    tagInput_->setStyleSheet(fieldStyle);
    addTagBtn_ = new QPushButton("Add", tagBox);
    addTagBtn_->setMaximumHeight(24);
    addTagBtn_->setStyleSheet("QPushButton { font-size: 11px; padding: 2px 8px; }");
    rmTagBtn_  = new QPushButton("X", tagBox);
    rmTagBtn_->setMaximumHeight(24);
    rmTagBtn_->setMaximumWidth(28);
    rmTagBtn_->setStyleSheet("QPushButton { font-size: 11px; padding: 2px 4px; }");
    inputRow->addWidget(tagInput_);
    inputRow->addWidget(addTagBtn_);
    inputRow->addWidget(rmTagBtn_);
    tagLay->addLayout(inputRow);

    tagList_ = new QListWidget(tagBox);
    tagList_->setSelectionMode(QAbstractItemView::SingleSelection);
    // No maximum height - let the list fill the available vertical space
    // in the group box (issue #3).
    tagList_->setStyleSheet(fieldStyle);
    tagLay->addWidget(tagList_, 1);

    outer->addWidget(tagBox, 1);

    // Notes (right) - stretch=1, equal to Tags (issue #3)
    auto* noteBox = new QGroupBox("Notes", this);
    noteBox->setStyleSheet(groupStyle);
    noteBox->setMinimumWidth(120);
    auto* noteLay = new QVBoxLayout(noteBox);
    noteLay->setContentsMargins(4, 4, 4, 4);
    noteLay->setSpacing(2);
    noteEdit_ = new QTextEdit(noteBox);
    noteEdit_->setPlaceholderText("Add notes...");
    noteEdit_->setStyleSheet(fieldStyle);
    noteLay->addWidget(noteEdit_, 1);

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
