// ============================================================
// SearchBar.cpp - Modern search bar matching reference design
// ============================================================

#include "SearchBar.h"
#include "IconUtils.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QCompleter>
#include <QStringListModel>
#include <QTimer>
#include <QApplication>
#include <QPalette>
#include <QFile>
#include <QSvgRenderer>
#include <QPainter>
#include <QResizeEvent>
#include <QMouseEvent>

namespace DocuSearch {

// A QLineEdit subclass that paints a leading search icon inside the
// input. We give the input a left text margin so typed text doesn't
// overlap the icon. Exposed as a public nested class via SearchBar.h.
class SearchBarSearchLineEdit : public QLineEdit {
public:
    explicit SearchBarSearchLineEdit(QWidget* parent = nullptr) : QLineEdit(parent) {
        setObjectName("searchInput");
        setPlaceholderText("Search documents...");
        setMinimumHeight(36);
        setMaximumHeight(36);
        setTextMargins(36, 0, 36, 0);
    }

    void setSearchIconPixmap(const QPixmap& pm) {
        searchPixmap_ = pm;
        update();
    }

protected:
    void paintEvent(QPaintEvent* e) override {
        QLineEdit::paintEvent(e);
        if (!searchPixmap_.isNull()) {
            QPainter p(this);
            const int y = (height() - searchPixmap_.height()) / 2;
            p.drawPixmap(10, y, searchPixmap_);
        }
    }

private:
    QPixmap searchPixmap_;
};

// A small container widget that holds the search QLineEdit plus an
// overlaid clear button on the right. Both children are positioned
// manually in resizeEvent so the clear button visually overlaps the
// input's right padding area.
class SearchInputWrap : public QWidget {
public:
    explicit SearchInputWrap(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(36);
        setMaximumHeight(36);
        setStyleSheet("background: transparent;");
    }

    void setLineEdit(SearchBarSearchLineEdit* edit) {
        edit_ = edit;
        edit_->setParent(this);
    }

    void setClearButton(QPushButton* btn) {
        clearBtn_ = btn;
        clearBtn_->setParent(this);
        clearBtn_->raise();
    }

protected:
    void resizeEvent(QResizeEvent* e) override {
        QWidget::resizeEvent(e);
        if (edit_) {
            edit_->setGeometry(0, 0, width(), height());
        }
        if (clearBtn_) {
            const int btnW = clearBtn_->width();
            const int btnH = clearBtn_->height();
            const int x = width() - btnW - 8;
            const int y = (height() - btnH) / 2;
            clearBtn_->move(x, y);
            clearBtn_->raise();
        }
    }

private:
    SearchBarSearchLineEdit* edit_ = nullptr;
    QPushButton* clearBtn_ = nullptr;
};

SearchBar::SearchBar(QWidget* parent) : QWidget(parent) {
    setObjectName("searchBarArea");

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 10, 16, 10);
    layout->setSpacing(10);

    // ---- Search input wrap (input + clear button overlay) ----
    auto* inputWrap = new SearchInputWrap(this);
    inputWrap->setMaximumWidth(420);
    inputWrap->setMinimumWidth(280);

    edit_ = new SearchBarSearchLineEdit;
    inputWrap->setLineEdit(edit_);

    clearBtn_ = new QPushButton;
    clearBtn_->setCursor(Qt::PointingHandCursor);
    clearBtn_->setToolTip("Clear search");
    clearBtn_->setFixedSize(22, 22);
    // Style as a small circular gray button with X icon.
    clearBtn_->setStyleSheet(
        "QPushButton { background-color: #e5e7eb; border: none; border-radius: 11px; }"
        "QPushButton:hover { background-color: #d1d5db; }");
    clearBtn_->hide();
    inputWrap->setClearButton(clearBtn_);

    // ---- Search button (with Ctrl+K badge) ----
    // The reference design uses a single blue button containing:
    //   [search-icon] Search [Ctrl+K]
    // We build this as a custom QWidget with HBoxLayout. The child
    // QLabels are set to WA_TransparentForMouseEvents so mouse clicks
    // pass through them to the parent widget, where our event filter
    // catches MouseButtonPress and triggers search.
    searchBtnWidget_ = new QWidget(this);
    searchBtnWidget_->setObjectName("searchBtn");
    searchBtnWidget_->setCursor(Qt::PointingHandCursor);
    searchBtnWidget_->setMinimumHeight(36);
    searchBtnWidget_->setMaximumHeight(36);
    searchBtnWidget_->installEventFilter(this);
    auto* sbwLay = new QHBoxLayout(searchBtnWidget_);
    sbwLay->setContentsMargins(12, 0, 6, 0);
    sbwLay->setSpacing(6);
    searchIconLbl_ = new QLabel(searchBtnWidget_);
    searchIconLbl_->setStyleSheet("background: transparent;");
    searchIconLbl_->setFixedSize(14, 14);
    searchIconLbl_->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* searchTextLbl = new QLabel("Search", searchBtnWidget_);
    searchTextLbl->setStyleSheet(
        "background: transparent; color: #ffffff; "
        "font-size: 13.5px; font-weight: 600;");
    searchTextLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* shortcutLbl = new QLabel("Ctrl+K", searchBtnWidget_);
    shortcutLbl->setStyleSheet(
        "background: rgba(255,255,255,0.25); color: #ffffff; "
        "border-radius: 4px; padding: 2px 6px; "
        "font-size: 11px; font-weight: 500;");
    shortcutLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    sbwLay->addWidget(searchIconLbl_);
    sbwLay->addWidget(searchTextLbl);
    sbwLay->addWidget(shortcutLbl);

    // ---- Saved searches dropdown ----
    savedBox_ = new QComboBox(this);
    savedBox_->setToolTip("Saved searches");
    savedBox_->addItem("Saved Searches");
    savedBox_->setMinimumHeight(36);
    savedBox_->setMaximumHeight(36);
    savedBox_->setCursor(Qt::PointingHandCursor);
    savedBox_->setMinimumWidth(140);

    // ---- Filters button ----
    filtersBtn_ = new QPushButton(this);
    filtersBtn_->setObjectName("toolbarBtn");
    filtersBtn_->setCursor(Qt::PointingHandCursor);
    filtersBtn_->setText("Filters");
    filtersBtn_->setMinimumHeight(36);
    filtersBtn_->setMaximumHeight(36);

    // ---- Add Folder button ----
    addFolderBtn_ = new QPushButton(this);
    addFolderBtn_->setObjectName("toolbarBtn");
    addFolderBtn_->setCursor(Qt::PointingHandCursor);
    addFolderBtn_->setText("Add Folder");
    addFolderBtn_->setMinimumHeight(36);
    addFolderBtn_->setMaximumHeight(36);

    // ---- Icon buttons: Refresh, List, Grid, More ----
    auto makeIconBtn = [this](const QString& tooltip) -> QPushButton* {
        auto* b = new QPushButton(this);
        b->setObjectName("iconBtn");
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tooltip);
        return b;
    };
    refreshBtn_ = makeIconBtn("Refresh");
    listBtn_    = makeIconBtn("List view");
    gridBtn_    = makeIconBtn("Grid view");
    moreBtn_    = makeIconBtn("More");

    // ---- Assemble layout ----
    layout->addWidget(inputWrap);
    layout->addWidget(searchBtnWidget_);
    layout->addWidget(savedBox_);
    layout->addWidget(filtersBtn_);
    layout->addStretch();
    layout->addWidget(addFolderBtn_);
    layout->addWidget(refreshBtn_);
    layout->addWidget(listBtn_);
    layout->addWidget(gridBtn_);
    layout->addWidget(moreBtn_);

    // ---- Signals ----
    connect(clearBtn_, &QPushButton::clicked, this, [this]{
        edit_->clear();
        emit searchRequested(QString());
    });
    connect(edit_, &QLineEdit::returnPressed, this, &SearchBar::onReturnPressed);
    connect(edit_, &QLineEdit::textChanged, this, &SearchBar::onTextChanged);

    autoSearchTimer_ = new QTimer(this);
    autoSearchTimer_->setSingleShot(true);
    autoSearchTimer_->setInterval(300);
    connect(autoSearchTimer_, &QTimer::timeout, this, &SearchBar::onReturnPressed);
    connect(edit_, &QLineEdit::textChanged, this, [this](){
        autoSearchTimer_->start();
    });

    connect(savedBox_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx){
        if (idx > 0) {
            emit savedSearchSelected(savedBox_->itemText(idx));
            savedBox_->setCurrentIndex(0);
        }
    });

    connect(addFolderBtn_, &QPushButton::clicked, this, &SearchBar::addFolderRequested);
    connect(refreshBtn_,   &QPushButton::clicked, this, &SearchBar::refreshRequested);
    connect(filtersBtn_,   &QPushButton::clicked, this, &SearchBar::filtersRequested);

    auto* completer = new QCompleter(this);
    completer->setModel(new QStringListModel(this));
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    edit_->setCompleter(completer);

    refreshIcons();
}

bool SearchBar::eventFilter(QObject* obj, QEvent* e) {
    // Search button click → trigger search.
    if (obj == searchBtnWidget_) {
        if (e->type() == QEvent::MouseButtonPress ||
            e->type() == QEvent::MouseButtonDblClick) {
            onReturnPressed();
            return true;
        }
    }
    return QWidget::eventFilter(obj, e);
}

void SearchBar::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
}

void SearchBar::refreshIcons() {
    QColor textColor = qApp->palette().color(QPalette::Text);
    QColor whiteText("#ffffff");

    // Search icon inside input (muted gray, not text color).
    // Use static_cast (not qobject_cast) because SearchBarSearchLineEdit
    // doesn't have Q_OBJECT — we know edit_ was constructed as a
    // SearchBarSearchLineEdit in the constructor.
    auto* sle = static_cast<SearchBarSearchLineEdit*>(edit_);
    if (sle) {
        sle->setSearchIconPixmap(loadLucidePixmap("search", QColor("#9ca3af"), 16, devicePixelRatio()));
    }

    // Clear button icon (X) — gray on gray bg.
    clearBtn_->setIcon(loadLucideIcon("x", QColor("#6b7280"), 12));
    clearBtn_->setIconSize(QSize(12, 12));

    // Search button icon (white on blue)
    if (searchIconLbl_) {
        searchIconLbl_->setPixmap(loadLucidePixmap("search", whiteText, 14, devicePixelRatio()));
    }

    filtersBtn_->setIcon(loadLucideIcon("filter", textColor, 16));
    filtersBtn_->setIconSize(QSize(16, 16));

    addFolderBtn_->setIcon(loadLucideIcon("plus", textColor, 16));
    addFolderBtn_->setIconSize(QSize(16, 16));

    refreshBtn_->setIcon(loadLucideIcon("refresh-cw", textColor, 16));
    refreshBtn_->setIconSize(QSize(16, 16));

    listBtn_->setIcon(loadLucideIcon("list", textColor, 16));
    listBtn_->setIconSize(QSize(16, 16));

    gridBtn_->setIcon(loadLucideIcon("layout-grid", textColor, 16));
    gridBtn_->setIconSize(QSize(16, 16));

    moreBtn_->setIcon(loadLucideIcon("more-horizontal", textColor, 16));
    moreBtn_->setIconSize(QSize(16, 16));
}

QString SearchBar::text() const { return edit_->text(); }
void SearchBar::setText(const QString& s) { edit_->setText(s); }
void SearchBar::setPlaceholder(const QString& s) { edit_->setPlaceholderText(s); }

void SearchBar::setSavedSearches(const QStringList& names) {
    savedBox_->blockSignals(true);
    savedBox_->clear();
    savedBox_->addItem("Saved Searches");
    savedBox_->addItems(names);
    savedBox_->blockSignals(false);
    // Update the completer model (if a completer was set).
    auto* completer = edit_->completer();
    if (completer) {
        auto* m = qobject_cast<QStringListModel*>(completer->model());
        if (m) m->setStringList(names);
    }
}

void SearchBar::onReturnPressed() {
    emit searchRequested(edit_->text().trimmed());
}

void SearchBar::onTextChanged(const QString& s) {
    clearBtn_->setVisible(!s.isEmpty());
}

} // namespace DocuSearch
