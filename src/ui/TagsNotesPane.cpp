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
    // Vertical layout: Tags on top, Notes below.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    // ---- Tags section ----
    auto* tagsHeader = new QLabel("TAGS", this);
    tagsHeader->setStyleSheet(
        "QLabel { color: #0078D4; font-size: 10px; font-weight: bold; "
        "  text-transform: uppercase; letter-spacing: 1px; "
        "  background: transparent; border: none; padding: 1px; }");
    outer->addWidget(tagsHeader);

    // Tag input on its own row (the panel is narrow, so we can't fit
    // input + 2 buttons in one row without crushing them).
    tagInput_ = new QLineEdit(this);
    tagInput_->setPlaceholderText("Add tag...");
    tagInput_->setMaximumHeight(26);
    tagInput_->setStyleSheet(
        "QLineEdit { "
        "  padding: 2px 8px; "
        "  border-radius: 4px; "
        "  background-color: #f5f5f5; "
        "  border: 1px solid #e0e0e0; "
        "  font-size: 11px; "
        "} "
        "QLineEdit:focus { border: 1px solid #0078D4; }");
    outer->addWidget(tagInput_);

    // Buttons row: Add + Remove side by side, compact.
    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(3);
    addTagBtn_ = new QPushButton("+ Add", this);
    addTagBtn_->setMaximumHeight(24);
    addTagBtn_->setCursor(Qt::PointingHandCursor);
    addTagBtn_->setStyleSheet(
        "QPushButton { "
        "  padding: 2px 8px; "
        "  border-radius: 4px; "
        "  background-color: #0078D4; "
        "  color: #ffffff; "
        "  border: none; "
        "  font-size: 11px; "
        "  font-weight: 500; "
        "} "
        "QPushButton:hover { background-color: #0067C0; }");
    rmTagBtn_ = new QPushButton("Remove", this);
    rmTagBtn_->setMaximumHeight(24);
    rmTagBtn_->setCursor(Qt::PointingHandCursor);
    rmTagBtn_->setStyleSheet(
        "QPushButton { "
        "  padding: 2px 8px; "
        "  border-radius: 4px; "
        "  background-color: #f0f0f0; "
        "  color: #606060; "
        "  border: 1px solid #e0e0e0; "
        "  font-size: 11px; "
        "} "
        "QPushButton:hover { background-color: #e0e0e0; }");
    btnRow->addWidget(addTagBtn_);
    btnRow->addWidget(rmTagBtn_);
    outer->addLayout(btnRow);

    // Tag list.
    tagList_ = new QListWidget(this);
    tagList_->setSelectionMode(QAbstractItemView::SingleSelection);
    tagList_->setMinimumHeight(30);
    tagList_->setStyleSheet(
        "QListWidget { "
        "  background-color: #ffffff; "
        "  border: 1px solid #e0e0e0; "
        "  border-radius: 4px; "
        "  font-size: 11px; "
        "} "
        "QListWidget::item { "
        "  padding: 4px 8px; "
        "  margin: 1px; "
        "  border-radius: 8px; "
        "  background-color: #e3f2fd; "
        "  color: #0067C0; "
        "} "
        "QListWidget::item:hover { background-color: #bbdefb; } "
        "QListWidget::item:selected { "
        "  background-color: #0078D4; "
        "  color: #ffffff; "
        "}");
    outer->addWidget(tagList_, 1);

    // ---- Notes section ----
    auto* notesHeader = new QLabel("NOTES", this);
    notesHeader->setStyleSheet(
        "QLabel { color: #0078D4; font-size: 10px; font-weight: bold; "
        "  text-transform: uppercase; letter-spacing: 1px; "
        "  background: transparent; border: none; padding: 1px; }");
    outer->addWidget(notesHeader);

    noteEdit_ = new QTextEdit(this);
    noteEdit_->setPlaceholderText("Add notes...");
    noteEdit_->setMinimumHeight(30);
    noteEdit_->setStyleSheet(
        "QTextEdit { "
        "  background-color: #ffffff; "
        "  border: 1px solid #e0e0e0; "
        "  border-radius: 4px; "
        "  font-size: 11px; "
        "  padding: 4px; "
        "} "
        "QTextEdit:focus { border: 1px solid #0078D4; }");
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
