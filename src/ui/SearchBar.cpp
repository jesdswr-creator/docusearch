// ============================================================
// SearchBar.cpp - Modern Windows 11 search bar with icon
// ============================================================

#include "SearchBar.h"

#include <QHBoxLayout>
#include <QCompleter>
#include <QStringListModel>
#include <QTimer>

namespace DocuSearch {

SearchBar::SearchBar(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    edit_ = new QLineEdit(this);
    edit_->setPlaceholderText("Search documents...");
    edit_->setClearButtonEnabled(true);
    edit_->setMinimumHeight(34);

    // No inline stylesheet — Theme QSS handles all colors for dark/light.
    searchBtn_ = new QPushButton("Search", this);
    searchBtn_->setDefault(true);
    searchBtn_->setMinimumHeight(34);
    searchBtn_->setCursor(Qt::PointingHandCursor);

    savedBox_ = new QComboBox(this);
    savedBox_->setToolTip("Click to run a saved search");
    savedBox_->addItem("-- Saved Searches --");
    savedBox_->setMinimumHeight(34);
    savedBox_->setCursor(Qt::PointingHandCursor);

    filterBtn_ = nullptr;

    layout->addWidget(edit_, 1);
    layout->addWidget(searchBtn_);
    layout->addWidget(savedBox_);

    connect(searchBtn_, &QPushButton::clicked, this, &SearchBar::onReturnPressed);
    connect(edit_, &QLineEdit::returnPressed, this, &SearchBar::onReturnPressed);
    connect(edit_, &QLineEdit::textChanged, this, &SearchBar::onTextChanged);

    // Auto-search with 300ms debounce.
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
