#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>

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

signals:
    void searchRequested(const QString& query);
    void savedSearchSelected(const QString& name);
    void advancedFiltersToggled(bool on);

private slots:
    void onReturnPressed();
    void onTextChanged(const QString& s);

private:
    QLineEdit*  edit_;
    QPushButton* searchBtn_;
    QComboBox*  savedBox_;
    QPushButton* filterBtn_;
    QTimer*     autoSearchTimer_;
};

} // namespace DocuSearch
