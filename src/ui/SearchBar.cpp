// ============================================================
// SearchBar.cpp
// ============================================================

#include "SearchBar.h"

#include <QHBoxLayout>
#include <QCompleter>
#include <QStringListModel>

namespace DocuSearch {

SearchBar::SearchBar(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    edit_      = new QLineEdit(this);
    edit_->setPlaceholderText("Search files and content...  (e.g. NOC type:pdf  or  \"Executive Lounge\")");
    edit_->setClearButtonEnabled(true);
    edit_->setMinimumHeight(30);

    // Search button - use the 'default' style from Theme QSS (accent blue).
    // Previously had hardcoded inline styles that didn't adapt to dark mode.
    searchBtn_ = new QPushButton("Search", this);
    searchBtn_->setDefault(true);
    searchBtn_->setMinimumHeight(30);

    // Saved searches dropdown - no inline stylesheet, let Theme QSS style it.
    // Previously had hardcoded 'border: 1px solid #ccc' which stayed light
    // in dark mode.
    savedBox_  = new QComboBox(this);
    savedBox_->setToolTip("Click to run a saved search");
    savedBox_->addItem("-- Saved Searches --");
    savedBox_->setMinimumHeight(30);

    // Filters button removed - all filters work via search syntax
    filterBtn_ = nullptr;

    layout->addWidget(edit_, 1);
    layout->addWidget(searchBtn_);
    layout->addWidget(savedBox_);

    connect(searchBtn_, &QPushButton::clicked, this, &SearchBar::onReturnPressed);
    connect(edit_, &QLineEdit::returnPressed, this, &SearchBar::onReturnPressed);
    connect(edit_, &QLineEdit::textChanged, this, &SearchBar::onTextChanged);
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
