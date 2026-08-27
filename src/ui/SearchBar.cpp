// ============================================================
// SearchBar.cpp - Clean search bar with Search button
// ============================================================

#include "SearchBar.h"
#include "IconUtils.h"

#include <QHBoxLayout>
#include <QCompleter>
#include <QStringListModel>
#include <QApplication>
#include <QPalette>
#include <QFile>
#include <QSvgRenderer>
#include <QPainter>
#include <QMouseEvent>

namespace DocuSearch {

class SearchBarSearchLineEdit : public QLineEdit {
public:
    explicit SearchBarSearchLineEdit(QWidget* parent = nullptr) : QLineEdit(parent) {
        setObjectName("searchInput");
        setPlaceholderText("Search documents...");
        setMinimumHeight(36);
        setMaximumHeight(36);
        setTextMargins(36, 0, 36, 0);
    }
    void setSearchIconPixmap(const QPixmap& pm) { searchPixmap_ = pm; update(); }
protected:
    void paintEvent(QPaintEvent* e) override {
        QLineEdit::paintEvent(e);
        if (!searchPixmap_.isNull()) {
            QPainter p(this);
            p.drawPixmap(10, (height() - searchPixmap_.height()) / 2, searchPixmap_);
        }
    }
private:
    QPixmap searchPixmap_;
};

class SearchInputWrap : public QWidget {
public:
    explicit SearchInputWrap(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(36); setMaximumHeight(36);
    }
    void setLineEdit(SearchBarSearchLineEdit* edit) { edit_ = edit; edit_->setParent(this); }
    void setClearButton(QPushButton* btn) { clearBtn_ = btn; clearBtn_->setParent(this); clearBtn_->raise(); }
protected:
    void resizeEvent(QResizeEvent*) override {
        if (edit_) edit_->setGeometry(0, 0, width(), height());
        if (clearBtn_) clearBtn_->move(width() - clearBtn_->width() - 8, (height() - clearBtn_->height()) / 2);
    }
private:
    SearchBarSearchLineEdit* edit_ = nullptr;
    QPushButton* clearBtn_ = nullptr;
};

SearchBar::SearchBar(QWidget* parent) : QWidget(parent) {
    setObjectName("searchBarArea");

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 8, 16, 8);
    layout->setSpacing(6);

    // Search input
    auto* inputWrap = new SearchInputWrap(this);
    inputWrap->setMaximumWidth(600);
    inputWrap->setMinimumWidth(200);
    edit_ = new SearchBarSearchLineEdit;
    inputWrap->setLineEdit(edit_);

    clearBtn_ = new QPushButton;
    clearBtn_->setCursor(Qt::PointingHandCursor);
    clearBtn_->setToolTip("Clear the search box");
    clearBtn_->setFixedSize(22, 22);
    clearBtn_->hide();
    inputWrap->setClearButton(clearBtn_);

    // Search button
    searchBtn_ = new QPushButton("Search", this);
    searchBtn_->setObjectName("searchBtn");
    searchBtn_->setCursor(Qt::PointingHandCursor);
    searchBtn_->setMinimumHeight(36);
    searchBtn_->setMaximumHeight(36);

    // Saved searches
    savedBox_ = new QComboBox(this);
    savedBox_->setToolTip("Re-run one of your saved searches");
    savedBox_->addItem("Saved");
    savedBox_->setMinimumHeight(36);
    savedBox_->setMaximumHeight(36);
    savedBox_->setCursor(Qt::PointingHandCursor);
    savedBox_->setMinimumWidth(100);

    // Add Folder
    addFolderBtn_ = new QPushButton("Add Folder", this);
    addFolderBtn_->setObjectName("addFolderBtn");
    addFolderBtn_->setCursor(Qt::PointingHandCursor);
    addFolderBtn_->setMinimumHeight(36);
    addFolderBtn_->setMaximumHeight(36);

    // Extract
    extractBtn_ = new QPushButton("Extract", this);
    extractBtn_->setObjectName("extractBtn");
    extractBtn_->setCursor(Qt::PointingHandCursor);
    extractBtn_->setMinimumHeight(36);
    extractBtn_->setMaximumHeight(36);

    // Refresh button — REMOVED (was confusing; auto-scan every 1 hour
    // handles folder rescans automatically). Keep the member pointer
    // so existing code that references it doesn't break, but don't
    // create or show the button.
    refreshBtn_ = nullptr;

    // Dummy buttons (not added to layout — no longer needed)
    filtersBtn_ = new QPushButton(this); filtersBtn_->setVisible(false);
    listBtn_ = new QPushButton(this); listBtn_->setVisible(false);
    gridBtn_ = new QPushButton(this); gridBtn_->setVisible(false);
    moreBtn_ = new QPushButton(this); moreBtn_->setVisible(false);

    // Layout: input | Search | Saved | Add Folder | Extract
    layout->addWidget(inputWrap, 1);
    layout->addWidget(searchBtn_);
    layout->addWidget(savedBox_);
    layout->addWidget(addFolderBtn_);
    layout->addWidget(extractBtn_);

    // Signals
    connect(clearBtn_, &QPushButton::clicked, this, [this]{ edit_->clear(); emit searchRequested(QString()); });
    connect(edit_, &QLineEdit::returnPressed, this, &SearchBar::onReturnPressed);
    connect(edit_, &QLineEdit::textChanged, this, &SearchBar::onTextChanged);
    connect(searchBtn_, &QPushButton::clicked, this, &SearchBar::onReturnPressed);

    connect(savedBox_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx){
        if (idx > 0) { emit savedSearchSelected(savedBox_->itemText(idx)); savedBox_->setCurrentIndex(0); }
    });
    connect(addFolderBtn_, &QPushButton::clicked, this, &SearchBar::addFolderRequested);
    // Note: refreshBtn_ removed — auto-scan handles rescans.
    connect(extractBtn_, &QPushButton::clicked, this, &SearchBar::extractRequested);
    connect(filtersBtn_, &QPushButton::clicked, this, &SearchBar::filtersRequested);

    auto* completer = new QCompleter(this);
    completer->setModel(new QStringListModel(this));
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    edit_->setCompleter(completer);

    refreshIcons();
}

bool SearchBar::eventFilter(QObject* obj, QEvent* e) { return QWidget::eventFilter(obj, e); }

void SearchBar::refreshIcons() {
    QColor textColor = qApp->palette().color(QPalette::Text);
    QColor whiteText("#ffffff");

    if (edit_) edit_->setSearchIconPixmap(loadLucidePixmap("search", QColor("#9ca3af"), 16, devicePixelRatio()));
    clearBtn_->setIcon(loadLucideIcon("x", QColor("#6b7280"), 12));
    clearBtn_->setIconSize(QSize(12, 12));
    searchBtn_->setIcon(loadLucideIcon("search", whiteText, 14));
    searchBtn_->setIconSize(QSize(14, 14));
    addFolderBtn_->setIcon(loadLucideIcon("plus", whiteText, 16));
    addFolderBtn_->setIconSize(QSize(16, 16));
    extractBtn_->setIcon(loadLucideIcon("file-text", whiteText, 16));
    extractBtn_->setIconSize(QSize(16, 16));
    // refreshBtn_ removed — no icon to set.
}

QString SearchBar::text() const { return edit_->text(); }
void SearchBar::setText(const QString& s) { edit_->setText(s); }
void SearchBar::setPlaceholder(const QString& s) { edit_->setPlaceholderText(s); }

void SearchBar::setSavedSearches(const QStringList& names) {
    savedBox_->blockSignals(true);
    savedBox_->clear();
    savedBox_->addItem("Saved");
    savedBox_->addItems(names);
    savedBox_->blockSignals(false);
    auto* c = edit_->completer();
    if (c) { auto* m = qobject_cast<QStringListModel*>(c->model()); if (m) m->setStringList(names); }
}

void SearchBar::onReturnPressed() { emit searchRequested(edit_->text().trimmed()); }
void SearchBar::onTextChanged(const QString& s) { clearBtn_->setVisible(!s.isEmpty()); }

} // namespace DocuSearch
