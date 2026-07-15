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
#include <QMouseEvent>

namespace DocuSearch {

// A QLineEdit subclass that paints a leading search icon inside the
// input. We give the input a left text margin so typed text doesn't
// overlap the icon.
class SearchBarSearchLineEdit : public QLineEdit {
public:
    explicit SearchBarSearchLineEdit(QWidget* parent = nullptr) : QLineEdit(parent) {
        setObjectName("searchInput");
        setPlaceholderText("Search documents... (Ctrl+K to focus)");
        setMinimumHeight(38);
        setMaximumHeight(38);
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
// overlaid clear button on the right.
class SearchInputWrap : public QWidget {
public:
    explicit SearchInputWrap(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(38);
        setMaximumHeight(38);
        
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
    layout->setSpacing(8);

    // ---- Search input wrap (input + clear button overlay) ----
    // This is the ONLY search bar. Pressing Enter or typing triggers
    // search automatically (via autoSearchTimer_). No separate
    // "Search" button is needed.
    auto* inputWrap = new SearchInputWrap(this);
    inputWrap->setMaximumWidth(500);
    inputWrap->setMinimumWidth(300);

    edit_ = new SearchBarSearchLineEdit;
    inputWrap->setLineEdit(edit_);

    clearBtn_ = new QPushButton;
    clearBtn_->setCursor(Qt::PointingHandCursor);
    clearBtn_->setToolTip("Clear search");
    clearBtn_->setFixedSize(22, 22);
    // clearBtn styled by QSS
    clearBtn_->hide();
    inputWrap->setClearButton(clearBtn_);

    // ---- Saved searches dropdown ----
    savedBox_ = new QComboBox(this);
    savedBox_->setToolTip("Saved searches");
    savedBox_->addItem("Saved Searches");
    savedBox_->setMinimumHeight(38);
    savedBox_->setMaximumHeight(38);
    savedBox_->setCursor(Qt::PointingHandCursor);
    savedBox_->setMinimumWidth(130);

    // ---- Filters button (colored accent) ----
    filtersBtn_ = new QPushButton(this);
    filtersBtn_->setObjectName("filtersBtn");
    filtersBtn_->setCursor(Qt::PointingHandCursor);
    filtersBtn_->setText("Filters");
    filtersBtn_->setMinimumHeight(38);
    filtersBtn_->setMaximumHeight(38);
    filtersBtn_->setToolTip("Advanced filters");

    // ---- Add Folder button (primary action — blue) ----
    addFolderBtn_ = new QPushButton(this);
    addFolderBtn_->setObjectName("addFolderBtn");
    addFolderBtn_->setCursor(Qt::PointingHandCursor);
    addFolderBtn_->setText("Add Folder");
    addFolderBtn_->setMinimumHeight(38);
    addFolderBtn_->setMaximumHeight(38);
    addFolderBtn_->setToolTip("Add a folder to the index");

    // ---- Extract button (extract text from all indexed files) ----
    extractBtn_ = new QPushButton(this);
    extractBtn_->setObjectName("extractBtn");
    extractBtn_->setCursor(Qt::PointingHandCursor);
    extractBtn_->setText("Extract");
    extractBtn_->setMinimumHeight(38);
    extractBtn_->setMaximumHeight(38);
    extractBtn_->setToolTip("Extract text content from all indexed files");

    // ---- Refresh button ----
    auto makeIconBtn = [this](const QString& tooltip, const QString& objName) -> QPushButton* {
        auto* b = new QPushButton(this);
        b->setObjectName(objName);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tooltip);
        return b;
    };
    refreshBtn_ = makeIconBtn("Refresh", "refreshBtn");
    listBtn_    = makeIconBtn("List view", "listBtn");
    gridBtn_    = makeIconBtn("Grid view", "gridBtn");
    moreBtn_    = makeIconBtn("More", "moreBtn");

    // ---- Assemble layout ----
    layout->addWidget(inputWrap, 1);  // input takes available space
    layout->addWidget(savedBox_);
    layout->addWidget(addFolderBtn_);
    layout->addWidget(extractBtn_);
    layout->addWidget(refreshBtn_);

    // ---- Signals ----
    connect(clearBtn_, &QPushButton::clicked, this, [this]{
        edit_->clear();
        emit searchRequested(QString());
    });
    connect(edit_, &QLineEdit::returnPressed, this, &SearchBar::onReturnPressed);
    connect(edit_, &QLineEdit::textChanged, this, &SearchBar::onTextChanged);
    // NOTE: No auto-search timer — search is triggered ONLY when the
    // user presses Enter. This gives better control and avoids
    // unnecessary queries on every keystroke.

    connect(savedBox_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int idx){
        if (idx > 0) {
            emit savedSearchSelected(savedBox_->itemText(idx));
            savedBox_->setCurrentIndex(0);
        }
    });

    connect(addFolderBtn_, &QPushButton::clicked, this, &SearchBar::addFolderRequested);
    connect(refreshBtn_,   &QPushButton::clicked, this, &SearchBar::refreshRequested);
    connect(extractBtn_,   &QPushButton::clicked, this, &SearchBar::extractRequested);
    connect(filtersBtn_,   &QPushButton::clicked, this, &SearchBar::filtersRequested);

    auto* completer = new QCompleter(this);
    completer->setModel(new QStringListModel(this));
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    edit_->setCompleter(completer);

    refreshIcons();
}

bool SearchBar::eventFilter(QObject* obj, QEvent* e) {
    return QWidget::eventFilter(obj, e);
}

void SearchBar::refreshIcons() {
    QColor textColor = qApp->palette().color(QPalette::Text);
    QColor whiteText("#ffffff");

    // Search icon inside input (muted gray, not text color).
    if (edit_) {
        edit_->setSearchIconPixmap(loadLucidePixmap("search", QColor("#9ca3af"), 16, devicePixelRatio()));
    }

    // Clear button icon (X) — gray on gray bg.
    clearBtn_->setIcon(loadLucideIcon("x", QColor("#6b7280"), 12));
    clearBtn_->setIconSize(QSize(12, 12));

    // Filters button icon (purple accent)
    filtersBtn_->setIcon(loadLucideIcon("filter", QColor("#7c3aed"), 16));
    filtersBtn_->setIconSize(QSize(16, 16));

    // Add Folder button icon (white on blue)
    addFolderBtn_->setIcon(loadLucideIcon("plus", whiteText, 16));
    addFolderBtn_->setIconSize(QSize(16, 16));

    // Extract button icon (white on purple)
    extractBtn_->setIcon(loadLucideIcon("file-text", whiteText, 16));
    extractBtn_->setIconSize(QSize(16, 16));

    // Icon buttons with colored icons
    refreshBtn_->setIcon(loadLucideIcon("refresh-cw", QColor("#059669"), 16));
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
