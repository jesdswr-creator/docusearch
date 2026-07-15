// ============================================================
// TagsNotesPane.cpp - Tags + Notes panel matching reference design
// ============================================================

#include "TagsNotesPane.h"
#include "IconUtils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QApplication>
#include <QPalette>
#include <QDateTime>
#include <QMouseEvent>
#include <QPushButton>
#include <QLayout>
#include <QList>
#include <functional>

namespace DocuSearch {

// A simple flow layout that wraps child widgets to the next row when
// they exceed the available width. Used to lay out tag pills.
// Defined inside namespace DocuSearch to match the forward declaration
// in TagsNotesPane.h.
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr) : QLayout(parent) {}
    ~FlowLayout() override {
        while (auto* item = takeAt(0)) delete item;
    }

    void addItem(QLayoutItem* item) override { items_.append(item); }
    int count() const override { return items_.size(); }
    QLayoutItem* itemAt(int index) const override {
        return (index >= 0 && index < items_.size()) ? items_.at(index) : nullptr;
    }
    QLayoutItem* takeAt(int index) override {
        return (index >= 0 && index < items_.size()) ? items_.takeAt(index) : nullptr;
    }

    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    QSize sizeHint() const override { return minimumSize(); }

    QSize minimumSize() const override {
        QSize size;
        const int m = contentsMargins().left();
        for (auto* it : items_)
            size = size.expandedTo(it->minimumSize());
        return size + QSize(2 * m, 2 * m);
    }

    void setGeometry(const QRect& rect) override {
        QLayout::setGeometry(rect);
        if (items_.isEmpty()) return;
        const int m = contentsMargins().left();
        int x = rect.x() + m;
        int y = rect.y() + m;
        const int lineHeight = 28;
        const int spacing = 6;
        for (auto* it : items_) {
            const int nextX = x + it->sizeHint().width() + spacing;
            if (nextX - spacing > rect.right() - m && x > rect.x() + m) {
                x = rect.x() + m;
                y += lineHeight + spacing;
            }
            it->setGeometry(QRect(QPoint(x, y), it->sizeHint()));
            x += it->sizeHint().width() + spacing;
        }
    }

private:
    QList<QLayoutItem*> items_;
};

// A clickable tag pill. Right-click triggers the onRemove callback.
// We use std::function instead of Qt signals/slots because Q_OBJECT
// in a .cpp file is awkward (needs MOC processing) and the new-style
// connect requires the sender class to have Q_OBJECT.
class TagPill : public QPushButton {
public:
    using RemoveCallback = std::function<void()>;

    explicit TagPill(const QString& text, const QString& colorClass,
                     RemoveCallback onRemove, QWidget* parent = nullptr)
        : QPushButton(text, parent), colorClass_(colorClass), onRemove_(std::move(onRemove)) {
        setObjectName(colorClass);
        setCursor(Qt::PointingHandCursor);
        setToolTip("Right-click to remove");
    }

    QString colorClass() const { return colorClass_; }

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::RightButton && onRemove_) {
            onRemove_();
            e->accept();
        } else {
            QPushButton::mousePressEvent(e);
        }
    }

private:
    QString colorClass_;
    RemoveCallback onRemove_;
};

TagsNotesPane::TagsNotesPane(QWidget* parent) : QWidget(parent) {
    setObjectName("tagsNotesPanel");
    

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ---- Tags section ----
    auto* tagsSection = new QWidget(this);
    tagsSection->setObjectName("tagsSection");
    auto* tagsLay = new QVBoxLayout(tagsSection);
    tagsLay->setContentsMargins(16, 12, 16, 12);
    tagsLay->setSpacing(10);

    auto* tagsHeaderRow = new QWidget(tagsSection);
    auto* thLay = new QHBoxLayout(tagsHeaderRow);
    thLay->setContentsMargins(0, 0, 0, 0);
    thLay->setSpacing(6);
    tagsHeaderLbl_ = new QLabel("Tags", tagsHeaderRow);
    tagsHeaderLbl_->setObjectName("tagsHeader");
    auto* tagsIconLbl = new QLabel(tagsHeaderRow);
    tagsIconLbl->setPixmap(loadLucidePixmap("tag", QColor("#6b7280"), 16, devicePixelRatio()));
    thLay->addWidget(tagsHeaderLbl_);
    thLay->addWidget(tagsIconLbl);
    thLay->addStretch();
    tagsLay->addWidget(tagsHeaderRow);

    // Tag pills container with flow layout
    tagsContainer_ = new QWidget(tagsSection);
    tagsLayout_ = new FlowLayout(tagsContainer_);
    tagsLay->addWidget(tagsContainer_);

    // Add Tag button + hidden input
    addTagBtn_ = new QPushButton("+ Add Tag", tagsSection);
    addTagBtn_->setObjectName("addTagBtn");
    addTagBtn_->setCursor(Qt::PointingHandCursor);
    tagsLay->addWidget(addTagBtn_);

    tagInput_ = new QLineEdit(tagsSection);
    tagInput_->setPlaceholderText("Type tag name and press Enter");
    tagInput_->setMaximumHeight(28);
    tagInput_->hide();
    tagsLay->addWidget(tagInput_);

    outer->addWidget(tagsSection);

    // ---- Notes section ----
    auto* notesSection = new QWidget(this);
    notesSection->setObjectName("notesSection");
    auto* notesLay = new QVBoxLayout(notesSection);
    notesLay->setContentsMargins(16, 12, 16, 12);
    notesLay->setSpacing(10);

    auto* notesHeaderRow = new QWidget(notesSection);
    auto* nhLay = new QHBoxLayout(notesHeaderRow);
    nhLay->setContentsMargins(0, 0, 0, 0);
    nhLay->setSpacing(6);
    notesHeaderLbl_ = new QLabel("Notes", notesHeaderRow);
    notesHeaderLbl_->setObjectName("notesHeader");
    auto* notesIconLbl = new QLabel(notesHeaderRow);
    notesIconLbl->setPixmap(loadLucidePixmap("sticky-note", QColor("#6b7280"), 16, devicePixelRatio()));
    nhLay->addWidget(notesHeaderLbl_);
    nhLay->addWidget(notesIconLbl);
    nhLay->addStretch();
    notesLay->addWidget(notesHeaderRow);

    noteEdit_ = new QTextEdit(notesSection);
    noteEdit_->setObjectName("notesContent");
    noteEdit_->setPlaceholderText("Add notes about this file...");
    noteEdit_->setMinimumHeight(80);
    noteEdit_->setMaximumHeight(160);
    notesLay->addWidget(noteEdit_);

    notesModifiedLbl_ = new QLabel(notesSection);
    notesModifiedLbl_->setObjectName("notesModified");
    notesModifiedLbl_->setText("Modified: -");
    notesLay->addWidget(notesModifiedLbl_);

    outer->addWidget(notesSection);
    outer->addStretch();

    // ---- Connections ----
    connect(addTagBtn_, &QPushButton::clicked, this, [this]{
        tagInput_->show();
        tagInput_->setFocus();
    });
    connect(tagInput_, &QLineEdit::returnPressed, this, &TagsNotesPane::onAddTag);
    connect(noteEdit_, &QTextEdit::textChanged, this, &TagsNotesPane::onNoteEdited);
}

void TagsNotesPane::setFileId(qint64 id) { fileId_ = id; }

void TagsNotesPane::setTags(const QStringList& tags) {
    currentTags_ = tags;
    rebuildTagPills();
}

void TagsNotesPane::setNote(const QString& note) {
    noteEdit_->blockSignals(true);
    noteEdit_->setPlainText(note);
    noteEdit_->blockSignals(false);
    notesModifiedLbl_->setText("Modified: -");
}

QStringList TagsNotesPane::tags() const {
    return currentTags_;
}

QString TagsNotesPane::note() const {
    return noteEdit_->toPlainText();
}

void TagsNotesPane::onAddTag() {
    const QString t = tagInput_->text().trimmed();
    tagInput_->clear();
    tagInput_->hide();
    if (t.isEmpty() || fileId_ == 0) return;
    if (currentTags_.contains(t, Qt::CaseInsensitive)) return;
    currentTags_.append(t);
    rebuildTagPills();
    emit tagAdded(fileId_, t);
}

void TagsNotesPane::onRemoveTag() {
    // No-op — tag removal is now handled via the TagPill callback directly.
    // This slot is kept for backward compatibility (in case other code calls it).
}

void TagsNotesPane::onNoteEdited() {
    if (fileId_ == 0) return;
    const QString note = noteEdit_->toPlainText();
    emit noteChanged(fileId_, note);
    notesModifiedLbl_->setText("Modified: " + QDateTime::currentDateTime().toString("dd MMM yyyy hh:mm AP"));
}

void TagsNotesPane::rebuildTagPills() {
    // Clear existing pills
    while (auto* item = tagsLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    // Build new pills with rotating color classes.
    // Each pill carries its own remove callback (right-click to remove).
    const QStringList colorClasses = {"tagBlue", "tagGreen", "tagYellow", "tagPurple"};
    for (int i = 0; i < currentTags_.size(); ++i) {
        const QString t = currentTags_[i];
        const QString cls = colorClasses[i % colorClasses.size()];
        // Capture t by value so the callback removes the right tag.
        auto onRemove = [this, t]() {
            currentTags_.removeAll(t);
            rebuildTagPills();
            emit tagRemoved(fileId_, t);
        };
        auto* pill = new TagPill(t, cls, onRemove, tagsContainer_);
        tagsLayout_->addWidget(pill);
    }
}

QString TagsNotesPane::colorForTag(int index) const {
    const QStringList colors = {"tagBlue", "tagGreen", "tagYellow", "tagPurple"};
    return colors[index % colors.size()];
}

void TagsNotesPane::refreshIcons() {
    // Tag/note icons are static (gray) — set in the constructor.
    // No palette-dependent icons here.
}

} // namespace DocuSearch
