// ============================================================
// Theme.cpp - Dark/Light QSS (Windows 11 Edition)
// ============================================================
//
// The QSS below follows the Windows 11 Fluent Design language:
//
//   * 8 px corner radius on all interactive surfaces (buttons,
//     inputs, list items, group boxes) - matches the OS chrome.
//   * Mica-inspired background tints (subtle translucency look
//     without true Mica since Qt QSS can't paint outside the
//     client area - the Mica backdrop itself is enabled in
//     main.cpp via DwmSetWindowAttribute).
//   * Accent button style with the Windows 11 accent blue (#005FB8).
//   * 1 px hairline borders in low-contrast system colors.
//   * Hover/pressed states that fade rather than flip.
//   * Scrollbars are thin (10 px), flat, and rounded - the same
//     style used by Windows 11 File Explorer.
//   * Tab underline is 2 px accent - the same as Settings and
//     Edge on Windows 11.
// ============================================================

#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QFile>

namespace DocuSearch {

namespace {
// Windows 11 accent palette (light + dark variants)
//   Accent:       #005FB8 (Win11 light) / #4CC2FF (Win11 dark)
//   Accent hover: #0067C0 / #6EC6FF
//   Accent press: #003D92 / #29A0FF
const char* kDarkQss = R"(
/* ============================================================
   DocuSearch - Windows 11 Dark Theme
   ============================================================ */
* {
    font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Inter', sans-serif;
    font-size: 13px;
    outline: 0;
}

QMainWindow, QWidget {
    background-color: #1f1f1f;   /* Mica dark tint */
    color: #ffffff;
}

/* ---- Inputs ---- */
QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background-color: #2b2b2b;
    color: #ffffff;
    border: 1px solid #404040;
    border-radius: 8px;
    padding: 7px 10px;
    selection-background-color: #4cc2ff;
    selection-color: #000000;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus,
QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus {
    border-color: #4cc2ff;
    border-bottom: 2px solid #4cc2ff;
}
QLineEdit:hover, QPlainTextEdit:hover, QTextEdit:hover,
QComboBox:hover {
    border-color: #5a5a5a;
}
QComboBox::drop-down {
    border: none;
    width: 26px;
    subcontrol-origin: padding;
    subcontrol-position: top right;
}
QComboBox::down-arrow {
    image: none;
    width: 0; height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid #cccccc;
    margin-right: 8px;
}
QComboBox QAbstractItemView {
    background-color: #2b2b2b;
    color: #ffffff;
    border: 1px solid #404040;
    border-radius: 8px;
    padding: 4px;
    selection-background-color: #4cc2ff;
    selection-color: #000000;
    outline: 0;
}

/* ---- Buttons ---- */
QPushButton {
    background-color: #2b2b2b;
    color: #ffffff;
    border: 1px solid #404040;
    border-radius: 8px;
    padding: 7px 16px;
    min-height: 20px;
}
QPushButton:hover    { background-color: #323232; border-color: #5a5a5a; }
QPushButton:pressed  { background-color: #252525; border-color: #2b2b2b; }
QPushButton:disabled { color: #6a6a6a; background-color: #2b2b2b; border-color: #333333; }
QPushButton:default {
    background-color: #005fb8;
    border-color:     #005fb8;
    color:            #ffffff;
}
QPushButton:default:hover   { background-color: #0067c0; border-color: #0067c0; }
QPushButton:default:pressed { background-color: #003d92; border-color: #003d92; }

/* ---- Lists / tables / trees ---- */
QListWidget, QTreeWidget, QTableWidget {
    background-color: #1f1f1f;
    color: #ffffff;
    border: 1px solid #2b2b2b;
    border-radius: 8px;
    alternate-background-color: #252525;
    selection-background-color: #4cc2ff;
    selection-color: #000000;
    outline: 0;
}
QListWidget::item, QTreeWidget::item { padding: 4px 6px; border-radius: 4px; }
QListWidget::item:hover, QTreeWidget::item:hover {
    background-color: #2b2b2b;
}
QListWidget::item:selected, QTreeWidget::item:selected {
    background-color: #4cc2ff;
    color: #000000;
}

QHeaderView::section {
    background-color: #1f1f1f;
    color: #c0c0c0;
    padding: 8px 10px;
    border: none;
    border-right: 1px solid #2b2b2b;
    border-bottom: 1px solid #2b2b2b;
    font-weight: 600;
}

/* ---- Progress ---- */
QProgressBar {
    background-color: #2b2b2b;
    border: 1px solid #404040;
    border-radius: 8px;
    text-align: center;
    color: #ffffff;
    min-height: 16px;
}
QProgressBar::chunk {
    background-color: #4cc2ff;
    border-radius: 6px;
}

/* ---- Tabs ---- */
QTabWidget::pane {
    border: 1px solid #2b2b2b;
    border-radius: 8px;
    background-color: #1f1f1f;
    top: -1px;
}
QTabBar::tab {
    background-color: transparent;
    color: #c0c0c0;
    padding: 8px 16px;
    border: none;
    border-bottom: 2px solid transparent;
    margin-right: 4px;
}
QTabBar::tab:hover     { color: #ffffff; background-color: #2b2b2b; border-radius: 6px; }
QTabBar::tab:selected  {
    color: #ffffff;
    background-color: #1f1f1f;
    border-bottom: 2px solid #4cc2ff;
}

/* ---- Splitters ---- */
QSplitter::handle            { background-color: #2b2b2b; border-radius: 1px; }
QSplitter::handle:horizontal { width: 3px; }
QSplitter::handle:vertical   { height: 3px; }
QSplitter::handle:hover      { background-color: #4cc2ff; }

/* ---- Status / toolbar / scroll ---- */
QStatusBar  { background-color: #1f1f1f; color: #c0c0c0; border-top: 1px solid #2b2b2b; }
QToolBar    { background-color: #1f1f1f; border: none; padding: 4px; spacing: 4px; }
QScrollArea { background-color: #1f1f1f; border: none; }
QToolTip    { background-color: #2b2b2b; color: #ffffff; border: 1px solid #404040; border-radius: 6px; padding: 4px 8px; }

/* ---- Custom labels ---- */
QLabel#titleLabel    { font-size: 18px; font-weight: 600; color: #ffffff; }
QLabel#subtitleLabel { color: #a0a0a0; font-size: 12px; }
QLabel#sectionLabel  {
    color: #4cc2ff;
    font-weight: 600;
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 1.2px;
}

/* ---- Group box ---- */
QGroupBox {
    border: 1px solid #2b2b2b;
    border-radius: 8px;
    margin-top: 14px;
    padding: 14px;
    color: #ffffff;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 12px;
    padding: 0 6px;
    background-color: #1f1f1f;
}

/* ---- Checkboxes / radios ---- */
QCheckBox, QRadioButton { color: #ffffff; spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 18px; height: 18px;
    border: 1px solid #5a5a5a;
    background: #2b2b2b;
    border-radius: 4px;
}
QRadioButton::indicator { border-radius: 9px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: #4cc2ff; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background: #4cc2ff;
    border-color: #4cc2ff;
}

/* ---- Menu bar ---- */
QMenuBar {
    background-color: #1f1f1f;
    color: #ffffff;
    border-bottom: 1px solid #2b2b2b;
    padding: 2px;
}
QMenuBar::item { padding: 6px 12px; border-radius: 4px; }
QMenuBar::item:selected { background-color: #2b2b2b; }
QMenu {
    background-color: #2b2b2b;
    color: #ffffff;
    border: 1px solid #404040;
    border-radius: 8px;
    padding: 4px;
}
QMenu::item { padding: 6px 24px; border-radius: 4px; }
QMenu::item:selected { background-color: #4cc2ff; color: #000000; }
QMenu::separator { height: 1px; background-color: #404040; margin: 4px 8px; }

/* ---- Scrollbars (Windows 11 style: thin, flat, rounded) ---- */
QScrollBar:vertical   { background: transparent; width: 10px; margin: 4px 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px 4px; }
QScrollBar::handle {
    background: #5a5a5a;
    border-radius: 5px;
    min-height: 32px;
    min-width:  32px;
}
QScrollBar::handle:hover   { background: #6a6a6a; }
QScrollBar::handle:pressed { background: #4cc2ff; }
QScrollBar::add-line, QScrollBar::sub-line,
QScrollBar::add-page,  QScrollBar::sub-page { background: none; height: 0; width: 0; }
)";

const char* kLightQss = R"(
/* ============================================================
   DocuSearch - Windows 11 Light Theme
   ============================================================ */
* {
    font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Inter', sans-serif;
    font-size: 13px;
    outline: 0;
}

QMainWindow, QWidget {
    background-color: #f3f3f3;   /* Mica light tint */
    color: #1f1f1f;
}

QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background-color: #ffffff;
    color: #1f1f1f;
    border: 1px solid #d1d1d1;
    border-radius: 8px;
    padding: 7px 10px;
    selection-background-color: #005fb8;
    selection-color: #ffffff;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus,
QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus {
    border-color: #005fb8;
    border-bottom: 2px solid #005fb8;
}
QLineEdit:hover, QPlainTextEdit:hover, QTextEdit:hover, QComboBox:hover {
    border-color: #b0b0b0;
}
QComboBox::drop-down {
    border: none;
    width: 26px;
    subcontrol-origin: padding;
    subcontrol-position: top right;
}
QComboBox::down-arrow {
    image: none;
    width: 0; height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid #444444;
    margin-right: 8px;
}
QComboBox QAbstractItemView {
    background-color: #ffffff;
    color: #1f1f1f;
    border: 1px solid #d1d1d1;
    border-radius: 8px;
    padding: 4px;
    selection-background-color: #005fb8;
    selection-color: #ffffff;
    outline: 0;
}

QPushButton {
    background-color: #ffffff;
    color: #1f1f1f;
    border: 1px solid #d1d1d1;
    border-radius: 8px;
    padding: 7px 16px;
    min-height: 20px;
}
QPushButton:hover    { background-color: #f5f5f5; border-color: #b0b0b0; }
QPushButton:pressed  { background-color: #ebebeb; border-color: #c0c0c0; }
QPushButton:disabled { color: #a0a0a0; background-color: #fafafa; border-color: #e0e0e0; }
QPushButton:default {
    background-color: #005fb8;
    border-color:     #005fb8;
    color:            #ffffff;
}
QPushButton:default:hover   { background-color: #0067c0; border-color: #0067c0; }
QPushButton:default:pressed { background-color: #003d92; border-color: #003d92; }

QListWidget, QTreeWidget, QTableWidget {
    background-color: #ffffff;
    color: #1f1f1f;
    border: 1px solid #e0e0e0;
    border-radius: 8px;
    alternate-background-color: #f7f7f7;
    selection-background-color: #005fb8;
    selection-color: #ffffff;
    outline: 0;
}
QListWidget::item, QTreeWidget::item { padding: 4px 6px; border-radius: 4px; }
QListWidget::item:hover, QTreeWidget::item:hover { background-color: #f3f3f3; }

QHeaderView::section {
    background-color: #fafafa;
    color: #4a4a4a;
    padding: 8px 10px;
    border: none;
    border-right: 1px solid #e0e0e0;
    border-bottom: 1px solid #e0e0e0;
    font-weight: 600;
}

QProgressBar {
    background-color: #ffffff;
    border: 1px solid #d1d1d1;
    border-radius: 8px;
    text-align: center;
    color: #1f1f1f;
    min-height: 16px;
}
QProgressBar::chunk {
    background-color: #005fb8;
    border-radius: 6px;
}

QTabWidget::pane {
    border: 1px solid #e0e0e0;
    border-radius: 8px;
    background-color: #ffffff;
    top: -1px;
}
QTabBar::tab {
    background-color: transparent;
    color: #5a5a5a;
    padding: 8px 16px;
    border: none;
    border-bottom: 2px solid transparent;
    margin-right: 4px;
}
QTabBar::tab:hover     { color: #1f1f1f; background-color: #f3f3f3; border-radius: 6px; }
QTabBar::tab:selected  {
    color: #1f1f1f;
    background-color: #ffffff;
    border-bottom: 2px solid #005fb8;
}

QSplitter::handle            { background-color: #e0e0e0; border-radius: 1px; }
QSplitter::handle:horizontal { width: 3px; }
QSplitter::handle:vertical   { height: 3px; }
QSplitter::handle:hover      { background-color: #005fb8; }

QStatusBar  { background-color: #f3f3f3; color: #5a5a5a; border-top: 1px solid #e0e0e0; }
QToolBar    { background-color: #f3f3f3; border: none; padding: 4px; spacing: 4px; }
QScrollArea { background-color: #f3f3f3; border: none; }
QToolTip    { background-color: #ffffff; color: #1f1f1f; border: 1px solid #d1d1d1; border-radius: 6px; padding: 4px 8px; }

QLabel#titleLabel    { font-size: 18px; font-weight: 600; color: #1f1f1f; }
QLabel#subtitleLabel { color: #5a5a5a; font-size: 12px; }
QLabel#sectionLabel  {
    color: #005fb8;
    font-weight: 600;
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 1.2px;
}

QGroupBox {
    border: 1px solid #e0e0e0;
    border-radius: 8px;
    margin-top: 14px;
    padding: 14px;
    color: #1f1f1f;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 12px;
    padding: 0 6px;
    background-color: #f3f3f3;
}

QCheckBox, QRadioButton { color: #1f1f1f; spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 18px; height: 18px;
    border: 1px solid #6a6a6a;
    background: #ffffff;
    border-radius: 4px;
}
QRadioButton::indicator { border-radius: 9px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: #005fb8; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background: #005fb8;
    border-color: #005fb8;
}

QMenuBar {
    background-color: #f3f3f3;
    color: #1f1f1f;
    border-bottom: 1px solid #e0e0e0;
    padding: 2px;
}
QMenuBar::item { padding: 6px 12px; border-radius: 4px; }
QMenuBar::item:selected { background-color: #e0e0e0; }
QMenu {
    background-color: #ffffff;
    color: #1f1f1f;
    border: 1px solid #d1d1d1;
    border-radius: 8px;
    padding: 4px;
}
QMenu::item { padding: 6px 24px; border-radius: 4px; }
QMenu::item:selected { background-color: #005fb8; color: #ffffff; }
QMenu::separator { height: 1px; background-color: #e0e0e0; margin: 4px 8px; }

QScrollBar:vertical   { background: transparent; width: 10px; margin: 4px 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px 4px; }
QScrollBar::handle {
    background: #b0b0b0;
    border-radius: 5px;
    min-height: 32px;
    min-width:  32px;
}
QScrollBar::handle:hover   { background: #909090; }
QScrollBar::handle:pressed { background: #005fb8; }
QScrollBar::add-line, QScrollBar::sub-line,
QScrollBar::add-page,  QScrollBar::sub-page { background: none; height: 0; width: 0; }
)";
} // namespace

void Theme::apply(Mode mode) {
    qApp->setStyleSheet(stylesheet(mode));
}

QString Theme::stylesheet(Mode mode) {
    return QString::fromUtf8(mode == Mode::Dark ? kDarkQss : kLightQss);
}

QColor Theme::accent(Mode mode) {
    // Windows 11 system accent colors
    return mode == Mode::Dark ? QColor("#4cc2ff") : QColor("#005fb8");
}

} // namespace DocuSearch
