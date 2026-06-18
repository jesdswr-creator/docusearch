#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QTextEdit>
#include <QPushButton>

namespace DocuSearch {

class TagsNotesPane : public QWidget {
    Q_OBJECT
public:
    explicit TagsNotesPane(QWidget* parent = nullptr);

    void setTags(const QStringList& tags);
    void setNote(const QString& note);
    void setFileId(qint64 id);

    QStringList tags() const;
    QString note() const;

signals:
    void tagAdded(qint64 fileId, const QString& tag);
    void tagRemoved(qint64 fileId, const QString& tag);
    void noteChanged(qint64 fileId, const QString& note);

private slots:
    void onAddTag();
    void onRemoveTag();
    void onNoteEdited();

private:
    qint64      fileId_ = 0;
    QLineEdit*  tagInput_;
    QListWidget* tagList_;
    QTextEdit*  noteEdit_;
    QPushButton* addTagBtn_;
    QPushButton* rmTagBtn_;
};

} // namespace DocuSearch
