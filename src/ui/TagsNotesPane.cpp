// ============================================================
// TagsNotesPane.cpp - Tags (top) + Notes (bottom), stacked vertically
// ============================================================

#include "TagsNotesPane.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QKeyEvent>
#include <QScrollArea>

namespace DocuSearch {

TagsNotesPane::TagsNotesPane(QWidget* parent) : QWidget(parent) {
    // Vertical layout: Tags on top, Notes below.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(8);

    // Modern section header for Tags.
    auto* tagsHeader = new QLabel("TAGS", this);
    tagsHeader->setStyleSheet(
        "QLabel { color: #0078D4; font-size: 11px; font-weight: bold; "
        "  text-transform: uppercase; letter-spacing: 1.5px; "
        "  background: transparent; border: none; padding: 2px; }");
    outer->addWidget(tagsHeader);

    // ---- Tags (top) ----
    auto* tagBox = new QGroupBox(this);
    tagBox->setStyleSheet(
        "QGroupBox { "
        "  border: 1px solid #e0e0e0; "
        "  border-radius: 8px; "
        "  margin-top: 0px; "
        "  padding: 12px 8px 8px 8px; "
        "  background-color: #ffffff; "
        "} "
        "QGroupBox::title { background: transparent; }");
    tagBox->setTitle("");
    auto* tagLay = new QVBoxLayout(tagBox);
    tagLay->setContentsMargins(4, 4, 4, 4);
    tagLay->setSpacing(6);

    auto* inputRow = new QHBoxLayout();
    inputRow->setSpacing(4);
    tagInput_ = new QLineEdit(tagBox);
    tagInput_->setPlaceholderText("Add tag...");
    tagInput_->setMaximumHeight(32);
    tagInput_->setStyleSheet(
        "QLineEdit { "
        "  padding: 4px 10px; "
        "  border-radius: 16px; "
        "  background-color: #f5f5f5; "
        "  border: 1px solid #e0e0e0; "
        "  font-size: 12px; "
        "} "
        "QLineEdit:focus { border: 2px solid #0078D4; }");
    addTagBtn_ = new QPushButton("+ Add", tagBox);
    addTagBtn_->setMaximumHeight(32);
    addTagBtn_->setCursor(Qt::PointingHandCursor);
    addTagBtn_->setStyleSheet(
        "QPushButton { "
        "  padding: 4px 12px; "
        "  border-radius: 16px; "
        "  background-color: #0078D4; "
        "  color: #ffffff; "
        "  border: none; "
        "  font-size: 12px; "
        "  font-weight: 500; "
        "} "
        "QPushButton:hover { background-color: #0067C0; }");
    rmTagBtn_  = new QPushButton("Remove", tagBox);
    rmTagBtn_->setMaximumHeight(32);
    rmTagBtn_->setCursor(Qt::PointingHandCursor);
    rmTagBtn_->setStyleSheet(
        "QPushButton { "
        "  padding: 4px 10px; "
        "  border-radius: 16px; "
        "  background-color: #f0f0f0; "
        "  color: #606060; "
        "  border: 1px solid #e0e0e0; "
        "  font-size: 12px; "
        "} "
        "QPushButton:hover { background-color: #e0e0e0; }");
    inputRow->addWidget(tagInput_);
    inputRow->addWidget(addTagBtn_);
    inputRow->addWidget(rmTagBtn_);
    tagLay->addLayout(inputRow);

    tagList_ = new QListWidget(tagBox);
    tagList_->setSelectionMode(QAbstractItemView::SingleSelection);
    tagList_->setMinimumHeight(50);
    // Tag chips: rounded, colored background, modern look.
    tagList_->setStyleSheet(
        "QListWidget { "
        "  background-color: transparent; "
        "  border: none; "
        "} "
        "QListWidget::item { "
        "  padding: 6px 12px; "
        "  margin: 2px; "
        "  border-radius: 14px; "
        "  background-color: #e3f2fd; "
        "  color: #0067C0; "
        "  font-size: 12px; "
        "  font-weight: 500; "
        "} "
        "QListWidget::item:hover { "
        "  background-color: #bbdefb; "
        "} "
        "QListWidget::item:selected { "
        "  background-color: #0078D4; "
        "  color: #ffffff; "
        "}");
    tagLay->addWidget(tagList_, 1);

    outer->addWidget(tagBox, 1);

    // ---- Notes (bottom) ----
    auto* notesHeader = new QLabel("NOTES", this);
    notesHeader->setStyleSheet(
        "QLabel { color: #0078D4; font-size: 11px; font-weight: bold; "
        "  text-transform: uppercase; letter-spacing: 1.5px; "
        "  background: transparent; border: none; padding: 2px; }");
    outer->addWidget(notesHeader);

    auto* noteBox = new QGroupBox(this);
    noteBox->setStyleSheet(
        "QGroupBox { "
        "  border: 1px solid #e0e0e0; "
        "  border-radius: 8px; "
        "  margin-top: 0px; "
        "  padding: 12px 8px 8px 8px; "
        "  background-color: #ffffff; "
        "} "
        "QGroupBox::title { background: transparent; }");
    auto* noteLay = new QVBoxLayout(noteBox);
    noteLay->setContentsMargins(4, 4, 4, 4);
    noteLay->setSpacing(4);
    noteEdit_ = new QTextEdit(noteBox);
    noteEdit_->setPlaceholderText("Add notes...");
    noteEdit_->setMinimumHeight(50);
    noteEdit_->setStyleSheet(
        "QTextEdit { "
        "  background-color: #ffffff; "
        "  border: 1px solid #e0e0e0; "
        "  border-radius: 8px; "
        "  font-size: 12px; "
        "  padding: 8px; "
        "} "
        "QTextEdit:focus { border: 2px solid #0078D4; }");
    noteLay->addWidget(noteEdit_, 1);

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
