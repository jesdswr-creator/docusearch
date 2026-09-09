// ============================================================
// TitleBarWidget.h — custom frameless-window title bar
// ============================================================
//
// v1.7.24: extracted from MainWindow.cpp's anonymous namespace into
// its own file. The class only ever used the QWidget surface of its
// owner (windowHandle(), isMaximized(), showNormal/showMaximized) —
// so it now takes a plain QWidget*, has no MainWindow dependency at
// all, and compiles standalone (a fresh TU including this header is
// the include-hygiene check; the phantom-declaration class of bug
// cannot hide in an anon namespace nobody includes).

#pragma once

#include <QWidget>

class QMouseEvent;

namespace DocuSearch {

class TitleBarWidget : public QWidget {
public:
    explicit TitleBarWidget(QWidget* windowOwner, QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    QWidget* windowOwner_ = nullptr;
};

} // namespace DocuSearch
