// ============================================================
// SearchBar.cpp - Modern Windows 11 search bar with icon
// ============================================================

#include "SearchBar.h"

#include <QHBoxLayout>
#include <QCompleter>
#include <QStringListModel>
#include <QLabel>
#include <QTimer>

namespace DocuSearch {

SearchBar::SearchBar(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    // Search box with modern styling - rounded, with left padding for
    // the search icon that we'll draw via a QLabel.
    edit_ = new QLineEdit(this);
    edit_->setPlaceholderText("Search documents...");
    edit_->setClearButtonEnabled(true);
    edit_->setMinimumHeight(36);

    // Add left padding so text doesn't overlap the search icon.
    // The icon is drawn via a QLabel positioned inside the QLineEdit.
    edit_->setStyleSheet(
        "QLineEdit { "
        "  padding-left: 32px; "
        "  border-radius: 18px; "
        "  background-color: palette(base); "
        "  border: 1px solid palette(mid); "
        "  font-size: 13px; "
        "} "
        "QLineEdit:focus { "
        "  border: 2px solid #0078D4; "
        "  padding-left: 31px; "
        "} "
        "QLineEdit:hover { "
        "  border-color: palette(dark); "
        "}");

    // Search icon label (positioned inside the QLineEdit via layout)
    auto* searchIcon = new QLabel(edit_);
    searchIcon->setText("\xE2\x8C\x95");  // U+2315 (SEARCH / TELEPHONE RECORDER)
    searchIcon->setStyleSheet(
        "QLabel { color: #808080; font-size: 14px; "
        "  background: transparent; border: none; }");
    searchIcon->setGeometry(8, 6, 20, 24);
    searchIcon->setAttribute(Qt::WA_TransparentForMouseEvents);

    searchBtn_ = new QPushButton("Search", this);
    searchBtn_->setDefault(true);
    searchBtn_->setMinimumHeight(36);
    searchBtn_->setCursor(Qt::PointingHandCursor);

    savedBox_  = new QComboBox(this);
    savedBox_->setToolTip("Click to run a saved search");
    savedBox_->addItem("-- Saved Searches --");
    savedBox_->setMinimumHeight(36);
    savedBox_->setCursor(Qt::PointingHandCursor);

    filterBtn_ = nullptr;

    layout->addWidget(edit_, 1);
    layout->addWidget(searchBtn_);
    layout->addWidget(savedBox_);

    connect(searchBtn_, &QPushButton::clicked, this, &SearchBar::onReturnPressed);
    connect(edit_, &QLineEdit::returnPressed, this, &SearchBar::onReturnPressed);
    connect(edit_, &QLineEdit::textChanged, this, &SearchBar::onTextChanged);

    // Auto-search with 300ms debounce (Windows 11 style live search).
    autoSearchTimer_ = new QTimer(this);
    autoSearchTimer_->setSingleShot(true);
    autoSearchTimer_->setInterval(300);
    connect(autoSearchTimer_, &QTimer::timeout, this, &SearchBar::onReturnPressed);
    connect(edit_, &QLineEdit::textChanged, this, [this]() {
        autoSearchTimer_->start();
    });

    connect(savedBox_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx){
        if (idx > 0) {
            emit savedSearchSelected(savedBox_->itemText(idx));
            savedBox_->setCurrentIndex(0);
        }
    });

    auto* completer = new QCompleter(this);
    completer->setModel(new QStringListModel(this));
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    edit_->setCompleter(completer);
}

QString SearchBar::text() const { return edit_->text(); }
void SearchBar::setText(const QString& s) { edit_->setText(s); }
void SearchBar::setPlaceholder(const QString& s) { edit_->setPlaceholderText(s); }

void SearchBar::setSavedSearches(const QStringList& names) {
    savedBox_->blockSignals(true);
    savedBox_->clear();
    savedBox_->addItem("-- Saved Searches --");
    savedBox_->addItems(names);
    savedBox_->blockSignals(false);
    auto* m = qobject_cast<QStringListModel*>(edit_->completer()->model());
    if (m) m->setStringList(names);
}

void SearchBar::onReturnPressed() {
    emit searchRequested(edit_->text().trimmed());
}

void SearchBar::onTextChanged(const QString& s) {
    Q_UNUSED(s);
}

} // namespace DocuSearch
