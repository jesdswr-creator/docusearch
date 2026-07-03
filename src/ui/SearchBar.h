#pragma once

// ============================================================
// SearchBar.h - Top search bar matching reference HTML design
// ============================================================
//
// Layout (left to right):
//   [icon: search] [input field...............] [x clear]
//   [Search Btn (Ctrl+K badge)]
//   [Saved Searches ▾]
//   [Filters]
//   <stretch>
//   [+ Add Folder]
//   [icon: refresh] [icon: list] [icon: grid] [icon: more]
//
// The search input has a leading search icon and a trailing clear
// button. The clear button is only visible when the input has text.
// ============================================================

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>

class QTimer;

namespace DocuSearch {

class SearchBar : public QWidget {
    Q_OBJECT
public:
    explicit SearchBar(QWidget* parent = nullptr);

    QString text() const;
    void setText(const QString& s);
    void setPlaceholder(const QString& s);
    void setSavedSearches(const QStringList& names);

    // Re-render all icons with the current palette's text color.
    // Call after a theme toggle so dark/light icons stay readable.
    void refreshIcons();

signals:
    void searchRequested(const QString& query);
    void savedSearchSelected(const QString& name);
    void addFolderRequested();
    void refreshRequested();
    void filtersRequested();
    void advancedFiltersToggled(bool on);

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private slots:
    void onReturnPressed();
    void onTextChanged(const QString& s);

private:
    // Custom QLineEdit that paints a leading search icon.
    // Implementation is in the .cpp file (SearchBarSearchLineEdit).
    QLineEdit*       edit_             = nullptr;
    QPushButton*     clearBtn_         = nullptr;
    QWidget*         searchBtnWidget_  = nullptr;
    QLabel*          searchIconLbl_    = nullptr;
    QComboBox*       savedBox_         = nullptr;
    QPushButton*     filtersBtn_       = nullptr;
    QPushButton*     addFolderBtn_     = nullptr;
    QPushButton*     refreshBtn_       = nullptr;
    QPushButton*     listBtn_          = nullptr;
    QPushButton*     gridBtn_          = nullptr;
    QPushButton*     moreBtn_          = nullptr;
    QTimer*          autoSearchTimer_  = nullptr;
};

} // namespace DocuSearch
