// ============================================================
// Theme.cpp - Complete UI redesign based on reference design
// ============================================================
//
// Design spec from reference image:
// - Light theme: #F8F9FA background, #FFFFFF panels
// - Accent: #0D6EFD (blue), #0B5ED7 (hover)
// - Text: #212529 (primary), #6C757D (secondary)
// - Borders: #E9ECEF, radius 8px
// - Selected: #E7F1FF with blue left border
// - Hover: #F1F3F5
// - Font: Segoe UI Variable / Roboto, 13px base
// ============================================================

#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QFile>

namespace DocuSearch {

namespace {

// ============================================================
// LIGHT THEME (default)
// ============================================================
const char* kLightQss = R"(
/* ===== Global ===== */
* {
    font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Roboto', sans-serif;
    font-size: 13px;
    outline: 0;
}

QMainWindow, QWidget {
    background-color: #F8F9FA;
    color: #212529;
}

/* ===== Search bar ===== */
QLineEdit {
    background-color: #FFFFFF;
    color: #212529;
    border: 1px solid #CED4DA;
    border-radius: 8px;
    padding: 8px 12px;
    font-size: 14px;
    selection-background-color: #0D6EFD;
    selection-color: #FFFFFF;
}
QLineEdit:focus {
    border: 2px solid #0D6EFD;
    padding: 7px 11px;
}
QLineEdit:hover {
    border-color: #ADB5BD;
}

/* ===== Combo box ===== */
QComboBox {
    background-color: #FFFFFF;
    color: #212529;
    border: 1px solid #CED4DA;
    border-radius: 8px;
    padding: 7px 10px;
}
QComboBox:focus { border: 2px solid #0D6EFD; }
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
    border-top: 5px solid #495057;
    margin-right: 8px;
}
QComboBox QAbstractItemView {
    background-color: #FFFFFF;
    color: #212529;
    border: 1px solid #CED4DA;
    border-radius: 8px;
    padding: 4px;
    selection-background-color: #0D6EFD;
    selection-color: #FFFFFF;
    outline: 0;
}

/* ===== Spin box ===== */
QSpinBox, QDoubleSpinBox {
    background-color: #FFFFFF;
    color: #212529;
    border: 1px solid #CED4DA;
    border-radius: 8px;
    padding: 7px 10px;
}
QSpinBox:focus { border: 2px solid #0D6EFD; }

/* ===== Text edit ===== */
QTextEdit, QPlainTextEdit {
    background-color: #FFFFFF;
    color: #212529;
    border: 1px solid #E9ECEF;
    border-radius: 8px;
    padding: 12px;
    font-size: 14px;
    selection-background-color: #0D6EFD;
    selection-color: #FFFFFF;
}
QTextEdit:focus, QPlainTextEdit:focus {
    border: 2px solid #0D6EFD;
}

/* ===== Buttons ===== */
QPushButton {
    background-color: #FFFFFF;
    color: #212529;
    border: 1px solid #CED4DA;
    border-radius: 8px;
    padding: 8px 16px;
    min-height: 20px;
    font-size: 14px;
    font-weight: 500;
}
QPushButton:hover {
    background-color: #F1F3F5;
    border-color: #ADB5BD;
}
QPushButton:pressed {
    background-color: #E9ECEF;
}
QPushButton:disabled {
    color: #ADB5BD;
    background-color: #F8F9FA;
    border-color: #E9ECEF;
}
QPushButton:default {
    background-color: #0D6EFD;
    border-color: #0D6EFD;
    color: #FFFFFF;
}
QPushButton:default:hover {
    background-color: #0B5ED7;
    border-color: #0B5ED7;
}
QPushButton:default:pressed {
    background-color: #0A58CA;
    border-color: #0A58CA;
}

/* ===== List widgets ===== */
QListWidget, QTreeWidget, QTableWidget {
    background-color: #FFFFFF;
    color: #212529;
    border: 1px solid #E9ECEF;
    border-radius: 8px;
    alternate-background-color: #F8F9FA;
    selection-background-color: #E7F1FF;
    selection-color: #212529;
    outline: 0;
}
QListWidget::item, QTreeWidget::item {
    padding: 6px 10px;
    border-bottom: 1px solid #F1F3F5;
    border-radius: 4px;
}
QListWidget::item:hover, QTreeWidget::item:hover {
    background-color: #F1F3F5;
}
QListWidget::item:selected, QTreeWidget::item:selected {
    background-color: #E7F1FF;
    color: #212529;
    border-left: 3px solid #0D6EFD;
}

/* ===== Table header ===== */
QHeaderView::section {
    background-color: #F8F9FA;
    color: #495057;
    padding: 8px 10px;
    border: none;
    border-bottom: 1px solid #E9ECEF;
    font-weight: 600;
    font-size: 12px;
}

/* ===== Progress bar ===== */
QProgressBar {
    background-color: #E9ECEF;
    border: none;
    border-radius: 8px;
    text-align: center;
    color: #212529;
    min-height: 16px;
}
QProgressBar::chunk {
    background-color: #0D6EFD;
    border-radius: 8px;
}

/* ===== Tabs ===== */
QTabWidget::pane {
    border: 1px solid #E9ECEF;
    border-radius: 8px;
    background-color: #FFFFFF;
    top: -1px;
}
QTabBar::tab {
    background-color: transparent;
    color: #6C757D;
    padding: 8px 16px;
    border: none;
    border-bottom: 2px solid transparent;
    margin-right: 4px;
    font-size: 14px;
    font-weight: 500;
}
QTabBar::tab:hover {
    color: #212529;
    background-color: #F1F3F5;
    border-radius: 6px;
}
QTabBar::tab:selected {
    color: #0D6EFD;
    background-color: #FFFFFF;
    border-bottom: 2px solid #0D6EFD;
}

/* ===== Splitters ===== */
QSplitter::handle {
    background-color: #E9ECEF;
    border-radius: 1px;
}
QSplitter::handle:horizontal { width: 3px; }
QSplitter::handle:vertical { height: 3px; }
QSplitter::handle:hover { background-color: #0D6EFD; }

/* ===== Toolbar ===== */
QToolBar {
    background-color: #FFFFFF;
    border: none;
    border-bottom: 1px solid #E9ECEF;
    padding: 4px;
    spacing: 4px;
}
QToolButton {
    padding: 6px 12px;
    border-radius: 6px;
    background: transparent;
    border: none;
    font-size: 13px;
    color: #212529;
    spacing: 6px;
}
QToolButton:hover { background-color: #F1F3F5; }
QToolButton:pressed { background-color: #E9ECEF; }
QToolBar::separator { width: 1px; background-color: #E9ECEF; margin: 4px; }

/* ===== Status / scroll / tooltip ===== */
QStatusBar {
    background-color: #F8F9FA;
    color: #6C757D;
    border-top: 1px solid #E9ECEF;
    font-size: 12px;
}
QScrollArea { background-color: transparent; border: none; }
QToolTip {
    background-color: #212529;
    color: #FFFFFF;
    border: none;
    border-radius: 6px;
    padding: 6px 10px;
    font-size: 12px;
}

/* ===== Group box ===== */
QGroupBox {
    border: 1px solid #E9ECEF;
    border-radius: 8px;
    margin-top: 14px;
    padding: 16px 12px 12px 12px;
    color: #212529;
    font-weight: 600;
    font-size: 14px;
    background-color: #FFFFFF;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 12px;
    padding: 0 6px;
    background-color: #FFFFFF;
    color: #495057;
}

/* ===== Checkboxes / radios ===== */
QCheckBox, QRadioButton {
    color: #212529;
    spacing: 8px;
    font-size: 14px;
}
QCheckBox::indicator, QRadioButton::indicator {
    width: 18px;
    height: 18px;
    border: 2px solid #ADB5BD;
    background: #FFFFFF;
    border-radius: 4px;
}
QRadioButton::indicator { border-radius: 9px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover {
    border-color: #0D6EFD;
}
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background: #0D6EFD;
    border-color: #0D6EFD;
}

/* ===== Menu ===== */
QMenuBar {
    background-color: #FFFFFF;
    color: #212529;
    border-bottom: 1px solid #E9ECEF;
    padding: 2px;
}
QMenuBar::item { padding: 6px 12px; border-radius: 4px; }
QMenuBar::item:selected { background-color: #F1F3F5; }
QMenu {
    background-color: #FFFFFF;
    color: #212529;
    border: 1px solid #E9ECEF;
    border-radius: 8px;
    padding: 4px;
}
QMenu::item { padding: 6px 24px; border-radius: 4px; }
QMenu::item:selected {
    background-color: #0D6EFD;
    color: #FFFFFF;
}
QMenu::separator { height: 1px; background-color: #E9ECEF; margin: 4px 8px; }

/* ===== Scrollbars ===== */
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 4px 2px;
}
QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 2px 4px;
}
QScrollBar::handle {
    background: #CED4DA;
    border-radius: 5px;
    min-height: 32px;
    min-width: 32px;
}
QScrollBar::handle:hover { background: #ADB5BD; }
QScrollBar::handle:pressed { background: #0D6EFD; }
QScrollBar::add-line, QScrollBar::sub-line,
QScrollBar::add-page, QScrollBar::sub-page {
    background: none;
    height: 0;
    width: 0;
}

/* ===== Sidebar (left navigation) ===== */
QListWidget#sidebar {
    background-color: #F8F9FA;
    border: none;
    border-right: 1px solid #E9ECEF;
    outline: 0;
    font-size: 14px;
}
QListWidget#sidebar::item {
    padding: 10px 16px;
    border: none;
    border-radius: 0;
    color: #495057;
}
QListWidget#sidebar::item:hover {
    background-color: #E9ECEF;
}
QListWidget#sidebar::item:selected {
    background-color: #E7F1FF;
    color: #0D6EFD;
    border-left: 3px solid #0D6EFD;
}
QWidget#statusSection {
    background-color: #F8F9FA;
    border-top: 1px solid #E9ECEF;
    padding: 12px;
}

/* ===== Custom labels ===== */
QLabel#titleLabel { font-size: 18px; font-weight: 600; color: #212529; }
QLabel#subtitleLabel { color: #6C757D; font-size: 12px; }
QLabel#sectionLabel {
    color: #0D6EFD;
    font-weight: 600;
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 1.2px;
}
QLabel#phaseLabel {
    color: #FFFFFF;
    background-color: #0D6EFD;
    padding: 6px 10px;
    border-radius: 4px;
}

/* ===== Dialog ===== */
QDialog {
    background-color: #F8F9FA;
}
QDialogButtonBox QPushButton {
    min-width: 80px;
}
)";

// ============================================================
// DARK THEME
// ============================================================
const char* kDarkQss = R"(
/* ===== Global ===== */
* {
    font-family: 'Segoe UI Variable Text', 'Segoe UI', 'Roboto', sans-serif;
    font-size: 13px;
    outline: 0;
}

QMainWindow, QWidget {
    background-color: #1A1A1A;
    color: #E0E0E0;
}

/* ===== Search bar ===== */
QLineEdit {
    background-color: #2B2B2B;
    color: #E0E0E0;
    border: 1px solid #404040;
    border-radius: 8px;
    padding: 8px 12px;
    font-size: 14px;
    selection-background-color: #4CC2FF;
    selection-color: #000000;
}
QLineEdit:focus {
    border: 2px solid #4CC2FF;
    padding: 7px 11px;
}
QLineEdit:hover { border-color: #5A5A5A; }

/* ===== Combo box ===== */
QComboBox {
    background-color: #2B2B2B;
    color: #E0E0E0;
    border: 1px solid #404040;
    border-radius: 8px;
    padding: 7px 10px;
}
QComboBox:focus { border: 2px solid #4CC2FF; }
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
    border-top: 5px solid #CCCCCC;
    margin-right: 8px;
}
QComboBox QAbstractItemView {
    background-color: #2B2B2B;
    color: #E0E0E0;
    border: 1px solid #404040;
    border-radius: 8px;
    padding: 4px;
    selection-background-color: #4CC2FF;
    selection-color: #000000;
    outline: 0;
}

/* ===== Spin box ===== */
QSpinBox, QDoubleSpinBox {
    background-color: #2B2B2B;
    color: #E0E0E0;
    border: 1px solid #404040;
    border-radius: 8px;
    padding: 7px 10px;
}
QSpinBox:focus { border: 2px solid #4CC2FF; }

/* ===== Text edit ===== */
QTextEdit, QPlainTextEdit {
    background-color: #2B2B2B;
    color: #E0E0E0;
    border: 1px solid #404040;
    border-radius: 8px;
    padding: 12px;
    font-size: 14px;
    selection-background-color: #4CC2FF;
    selection-color: #000000;
}
QTextEdit:focus, QPlainTextEdit:focus {
    border: 2px solid #4CC2FF;
}

/* ===== Buttons ===== */
QPushButton {
    background-color: #2B2B2B;
    color: #E0E0E0;
    border: 1px solid #404040;
    border-radius: 8px;
    padding: 8px 16px;
    min-height: 20px;
    font-size: 14px;
    font-weight: 500;
}
QPushButton:hover { background-color: #323232; border-color: #5A5A5A; }
QPushButton:pressed { background-color: #252525; }
QPushButton:disabled { color: #6A6A6A; background-color: #2B2B2B; border-color: #333333; }
QPushButton:default {
    background-color: #4CC2FF;
    border-color: #4CC2FF;
    color: #000000;
}
QPushButton:default:hover { background-color: #6EC6FF; border-color: #6EC6FF; }
QPushButton:default:pressed { background-color: #29A0FF; border-color: #29A0FF; }

/* ===== List widgets ===== */
QListWidget, QTreeWidget, QTableWidget {
    background-color: #252525;
    color: #E0E0E0;
    border: 1px solid #333333;
    border-radius: 8px;
    alternate-background-color: #2B2B2B;
    selection-background-color: #005FB8;
    selection-color: #FFFFFF;
    outline: 0;
}
QListWidget::item, QTreeWidget::item {
    padding: 6px 10px;
    border-bottom: 1px solid #333333;
    border-radius: 4px;
}
QListWidget::item:hover, QTreeWidget::item:hover { background-color: #2B2B2B; }
QListWidget::item:selected, QTreeWidget::item:selected {
    background-color: #005FB8;
    color: #FFFFFF;
    border-left: 3px solid #4CC2FF;
}

/* ===== Table header ===== */
QHeaderView::section {
    background-color: #1A1A1A;
    color: #A0A0A0;
    padding: 8px 10px;
    border: none;
    border-bottom: 1px solid #333333;
    font-weight: 600;
    font-size: 12px;
}

/* ===== Progress bar ===== */
QProgressBar {
    background-color: #2B2B2B;
    border: 1px solid #404040;
    border-radius: 8px;
    text-align: center;
    color: #E0E0E0;
    min-height: 16px;
}
QProgressBar::chunk {
    background-color: #4CC2FF;
    border-radius: 8px;
}

/* ===== Tabs ===== */
QTabWidget::pane {
    border: 1px solid #333333;
    border-radius: 8px;
    background-color: #1A1A1A;
    top: -1px;
}
QTabBar::tab {
    background-color: transparent;
    color: #A0A0A0;
    padding: 8px 16px;
    border: none;
    border-bottom: 2px solid transparent;
    margin-right: 4px;
    font-size: 14px;
    font-weight: 500;
}
QTabBar::tab:hover {
    color: #E0E0E0;
    background-color: #2B2B2B;
    border-radius: 6px;
}
QTabBar::tab:selected {
    color: #4CC2FF;
    background-color: #1A1A1A;
    border-bottom: 2px solid #4CC2FF;
}

/* ===== Splitters ===== */
QSplitter::handle { background-color: #333333; border-radius: 1px; }
QSplitter::handle:horizontal { width: 3px; }
QSplitter::handle:vertical { height: 3px; }
QSplitter::handle:hover { background-color: #4CC2FF; }

/* ===== Toolbar ===== */
QToolBar {
    background-color: #1A1A1A;
    border: none;
    border-bottom: 1px solid #333333;
    padding: 4px;
    spacing: 4px;
}
QToolButton {
    padding: 6px 12px;
    border-radius: 6px;
    background: transparent;
    border: none;
    font-size: 13px;
    color: #E0E0E0;
    spacing: 6px;
}
QToolButton:hover { background-color: #323232; }
QToolButton:pressed { background-color: #252525; }
QToolBar::separator { width: 1px; background-color: #404040; margin: 4px; }

/* ===== Status / scroll / tooltip ===== */
QStatusBar {
    background-color: #1A1A1A;
    color: #A0A0A0;
    border-top: 1px solid #333333;
    font-size: 12px;
}
QScrollArea { background-color: transparent; border: none; }
QToolTip {
    background-color: #2B2B2B;
    color: #E0E0E0;
    border: 1px solid #404040;
    border-radius: 6px;
    padding: 6px 10px;
    font-size: 12px;
}

/* ===== Group box ===== */
QGroupBox {
    border: 1px solid #333333;
    border-radius: 8px;
    margin-top: 14px;
    padding: 16px 12px 12px 12px;
    color: #E0E0E0;
    font-weight: 600;
    font-size: 14px;
    background-color: #252525;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 12px;
    padding: 0 6px;
    background-color: #1A1A1A;
    color: #A0A0A0;
}

/* ===== Checkboxes / radios ===== */
QCheckBox, QRadioButton { color: #E0E0E0; spacing: 8px; font-size: 14px; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 18px;
    height: 18px;
    border: 2px solid #5A5A5A;
    background: #2B2B2B;
    border-radius: 4px;
}
QRadioButton::indicator { border-radius: 9px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: #4CC2FF; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background: #4CC2FF;
    border-color: #4CC2FF;
}

/* ===== Menu ===== */
QMenuBar {
    background-color: #1A1A1A;
    color: #E0E0E0;
    border-bottom: 1px solid #333333;
    padding: 2px;
}
QMenuBar::item { padding: 6px 12px; border-radius: 4px; }
QMenuBar::item:selected { background-color: #2B2B2B; }
QMenu {
    background-color: #2B2B2B;
    color: #E0E0E0;
    border: 1px solid #404040;
    border-radius: 8px;
    padding: 4px;
}
QMenu::item { padding: 6px 24px; border-radius: 4px; }
QMenu::item:selected { background-color: #4CC2FF; color: #000000; }
QMenu::separator { height: 1px; background-color: #404040; margin: 4px 8px; }

/* ===== Scrollbars ===== */
QScrollBar:vertical { background: transparent; width: 10px; margin: 4px 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px 4px; }
QScrollBar::handle {
    background: #5A5A5A;
    border-radius: 5px;
    min-height: 32px;
    min-width: 32px;
}
QScrollBar::handle:hover { background: #6A6A6A; }
QScrollBar::handle:pressed { background: #4CC2FF; }
QScrollBar::add-line, QScrollBar::sub-line,
QScrollBar::add-page, QScrollBar::sub-page { background: none; height: 0; width: 0; }

/* ===== Sidebar (left navigation) ===== */
QListWidget#sidebar {
    background-color: #161616;
    border: none;
    border-right: 1px solid #333333;
    outline: 0;
    font-size: 14px;
}
QListWidget#sidebar::item {
    padding: 10px 16px;
    border: none;
    border-radius: 0;
    color: #A0A0A0;
}
QListWidget#sidebar::item:hover {
    background-color: #252525;
}
QListWidget#sidebar::item:selected {
    background-color: #005FB8;
    color: #FFFFFF;
    border-left: 3px solid #4CC2FF;
}
QWidget#statusSection {
    background-color: #161616;
    border-top: 1px solid #333333;
    padding: 12px;
}

/* ===== Custom labels ===== */
QLabel#titleLabel { font-size: 18px; font-weight: 600; color: #FFFFFF; }
QLabel#subtitleLabel { color: #A0A0A0; font-size: 12px; }
QLabel#sectionLabel {
    color: #4CC2FF;
    font-weight: 600;
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 1.2px;
}
QLabel#phaseLabel {
    color: #FFFFFF;
    background-color: #005FB8;
    padding: 6px 10px;
    border-radius: 4px;
}

/* ===== Dialog ===== */
QDialog { background-color: #1A1A1A; }
QDialogButtonBox QPushButton { min-width: 80px; }
)";

} // namespace

void Theme::apply(Mode mode) {
    qApp->setStyleSheet(stylesheet(mode));
}

QString Theme::stylesheet(Mode mode) {
    return QString::fromUtf8(mode == Mode::Dark ? kDarkQss : kLightQss);
}

QColor Theme::accent(Mode mode) {
    return mode == Mode::Dark ? QColor("#4CC2FF") : QColor("#0D6EFD");
}

} // namespace DocuSearch
