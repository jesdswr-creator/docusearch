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
    edit_->setPlaceholderText("Search files, content, OCR text…  (e.g. \"Executive Lounge\" type:pdf)");
    edit_->setClearButtonEnabled(true);

    searchBtn_ = new QPushButton("Search", this);
    searchBtn_->setDefault(true);

    savedBox_  = new QComboBox(this);
    savedBox_->setToolTip("Saved searches");
    savedBox_->addItem("— Saved —");

    filterBtn_ = new QPushButton("Filters", this);
    filterBtn_->setCheckable(true);

    layout->addWidget(edit_);
    layout->addWidget(searchBtn_);
    layout->addWidget(savedBox_);
    layout->addWidget(filterBtn_);

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
    connect(filterBtn_, &QPushButton::toggled, this, &SearchBar::advancedFiltersToggled);

    // Auto-complete from saved searches / recent
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
    savedBox_->addItem("— Saved —");
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
    // Could emit live-search signal with debounce — left to MainWindow.
}

} // namespace DocuSearch
