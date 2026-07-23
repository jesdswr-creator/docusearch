#pragma once

// ============================================================
// SearchBar.h - Top search bar matching reference HTML design
// ============================================================
//
// Layout (left to right):
//   [icon: search] [input field...............] [x clear]
//   [Saved Searches ▾]
//   [Filters]  [+ Add Folder]
//   [icon: refresh] [icon: list] [icon: grid] [icon: more]
//
// The search input has a leading search icon and a trailing clear
// button. The clear button is only visible when the input has text.
// Search is triggered automatically on typing (300ms debounce) or
// pressing Enter — no separate "Search" button is needed.
// ============================================================

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>

class QTimer;

namespace DocuSearch {

// Forward declaration — full definition is in SearchBar.cpp.
class SearchBarSearchLineEdit;

class SearchBar : public QWidget {
    Q_OBJECT
public:
    explicit SearchBar(QWidget* parent = nullptr);

    QString text() const;
    void setText(const QString& s);
    void setPlaceholder(const QString& s);
    void setSavedSearches(const QStringList& names);

    void refreshIcons();

signals:
    void searchRequested(const QString& query);
    void savedSearchSelected(const QString& name);
    void addFolderRequested();
    void refreshRequested();
    void extractRequested();
    void filtersRequested();

public:
    // Phase 1.4: Update extract button text to show extraction state.
    void setExtracting(bool running) {
        if (extractBtn_) {
            extractBtn_->setText(running ? "Cancel" : "Extract");
        }
    }

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private slots:
    void onReturnPressed();
    void onTextChanged(const QString& s);

private:
    SearchBarSearchLineEdit* edit_    = nullptr;
    QPushButton*     clearBtn_         = nullptr;
    QPushButton*     searchBtn_       = nullptr;
    QComboBox*       savedBox_         = nullptr;
    QPushButton*     filtersBtn_       = nullptr;
    QPushButton*     addFolderBtn_     = nullptr;
    QPushButton*     extractBtn_       = nullptr;
    QPushButton*     refreshBtn_       = nullptr;
    QPushButton*     listBtn_          = nullptr;
    QPushButton*     gridBtn_          = nullptr;
    QPushButton*     moreBtn_          = nullptr;
};

} // namespace DocuSearch
