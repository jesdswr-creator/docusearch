// ============================================================
// Theme.cpp - Fluent Design (Windows 11 native look)
// Ported 1:1 from /home/z/my-project/upload/Pasted Content_1787568567336.txt
//
// Two themes:
//   DARK  — mica #1c1c1c, surface #262626, accent #4cc2ff
//   LIGHT — mica #f2f1ee, surface #faf9f7, accent #0067c0
//
// Layout: 44px titlebar + 58px toolbar + (250px sidebar | 1fr results | 400px preview) + 34px statusbar
// Fonts:  Inter / Segoe UI Variable Text (primary), JetBrains Mono / Cascadia Code (mono)
// ============================================================

#include "Theme.h"
#include <QApplication>
#include <QPalette>
#include <QFile>

namespace DocuSearch {

namespace {

// ============================================================
// DARK THEME  (mica #1c1c1c, accent #4cc2ff)
// ============================================================
const char* kQssDark = R"(
* {
    font-family: 'Inter', 'Segoe UI Variable Text', 'Segoe UI', sans-serif;
    font-size: 13px;
    outline: 0;
    color: #f5f5f5;
}

QMainWindow, QWidget {
    background-color: #1c1c1c;
    color: #f5f5f5;
}

/* ===== TITLE BAR ===== */
QWidget#titleBar {
    background-color: #262626;
    border-bottom: 1px solid rgba(255,255,255,0.085);
}
QLabel#titleBarText {
    font-size: 13px; font-weight: 700; color: #f5f5f5; background: transparent;
}
QLabel#titleBarSubtitle {
    font-size: 11.5px; color: #7d7d7d; background: transparent;
}
QLabel#appLogo {
    background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 #0078d4, stop:1 #00b7c3);
    color: #fff; border-radius: 6px;
    font-size: 11px; font-weight: 700; qproperty-alignment: AlignCenter;
}
QPushButton#titleBtn {
    background: transparent; border: none; border-radius: 4px;
    color: #a9a9a9; padding: 4px;
}
QPushButton#titleBtn:hover { background-color: rgba(255,255,255,0.055); color: #f5f5f5; }
QPushButton#titleBtn#closeBtn:hover { background-color: #c42b1c; color: #fff; }

/* ===== SIDEBAR ===== */
QWidget#sidebar {
    background-color: rgba(30,30,30,0.72);
    border-right: 1px solid rgba(255,255,255,0.085);
}
QListWidget#sidebar {
    background-color: transparent; border: none; outline: 0;
    font-size: 12.5px; padding: 6px 0;
}
QListWidget#sidebar::item {
    padding: 7px 9px; border: none; border-radius: 7px;
    color: #a9a9a9; font-weight: 500; margin: 1px 4px;
}
QListWidget#sidebar::item:hover { background-color: rgba(255,255,255,0.055); color: #f5f5f5; }
QListWidget#sidebar::item:selected {
    background-color: rgba(76,194,255,0.14); color: #f5f5f5; font-weight: 600;
    border-left: 3px solid #4cc2ff;
}
QWidget#indexedStatus {
    background-color: transparent; border-top: 1px solid rgba(255,255,255,0.085);
}
QLabel#indexedHeader {
    font-size: 10px; font-weight: 700; color: #6ccb9f; background: transparent;
    text-transform: uppercase; letter-spacing: 1px;
}
QLabel#indexedInfo { font-size: 11px; color: #a9a9a9; background: transparent; }
QProgressBar#indexedBar {
    background-color: rgba(255,255,255,0.09); border: none; border-radius: 2px;
    min-height: 4px; max-height: 4px; text-align: center; color: transparent;
}
QProgressBar#indexedBar::chunk { background-color: #4cc2ff; border-radius: 2px; }

/* ===== SEARCH BAR ===== */
QWidget#searchBarArea {
    background-color: #262626; border-bottom: 1px solid rgba(255,255,255,0.085);
}
QLineEdit#searchInput {
    background-color: #2d2d2d; color: #f5f5f5;
    border: 1px solid rgba(255,255,255,0.16); border-radius: 8px;
    padding: 7px 36px; font-size: 13px;
    selection-background-color: #4cc2ff; selection-color: #003049;
}
QLineEdit#searchInput:focus {
    border: 1px solid #4cc2ff;
    background-color: #2d2d2d;
}

/* ===== Buttons ===== */
QPushButton#addFolderBtn {
    background-color: #4cc2ff; color: #003049; border: none;
    border-radius: 7px; padding: 0 13px; font-size: 12.5px; font-weight: 600;
    min-height: 36px;
}
QPushButton#addFolderBtn:hover { background-color: #67d4ff; }
QPushButton#searchBtn {
    background-color: #4cc2ff; color: #003049; border: none;
    border-radius: 7px; padding: 0 16px; font-size: 12.5px; font-weight: 600;
    min-height: 36px;
}
QPushButton#searchBtn:hover { background-color: #67d4ff; }
QPushButton#extractBtn {
    background-color: #4cc2ff; color: #003049; border: none;
    border-radius: 7px; padding: 0 13px; font-size: 12.5px; font-weight: 600;
    min-height: 36px;
}
QPushButton#extractBtn:hover { background-color: #67d4ff; }
QPushButton#refreshBtn {
    background-color: transparent; color: #a9a9a9; border: none;
    border-radius: 7px; padding: 6px; min-width: 36px; min-height: 36px;
    max-width: 36px; max-height: 36px;
}
QPushButton#refreshBtn:hover { background-color: rgba(255,255,255,0.055); color: #f5f5f5; }

/* ===== Results panel ===== */
QWidget#resultsPanel {
    background-color: transparent; border-right: 1px solid rgba(255,255,255,0.085);
}
QLabel#resultsTitle { font-size: 14px; font-weight: 700; color: #f5f5f5; background: transparent; }
QLabel#resultsCount { font-size: 11.5px; color: #7d7d7d; background: transparent; }
QComboBox#sortSelect {
    background-color: #2d2d2d; color: #f5f5f5;
    border: 1px solid rgba(255,255,255,0.16);
    border-radius: 6px; padding: 5px 8px; font-size: 11.5px;
}
QListWidget#resultsList {
    background-color: transparent; border: none; outline: 0; padding: 2px 12px 18px;
}
QListWidget#resultsList::item {
    border: 1px solid transparent; border-radius: 9px; margin-bottom: 2px;
}
QListWidget#resultsList::item:hover { background-color: rgba(255,255,255,0.055); }
QListWidget#resultsList::item:selected {
    background-color: rgba(76,194,255,0.14);
    border-color: rgba(76,194,255,0.35);
}
QLabel#resultTitle { font-size: 13px; font-weight: 600; color: #f5f5f5; background: transparent; }
QLabel#resultSnippet { font-size: 12px; color: #a9a9a9; background: transparent; }
QLabel#resultMeta { font-size: 10.5px; color: #7d7d7d; background: transparent; }
QLabel#fileIconBadge { color: #fff; font-size: 9px; font-weight: 700; border-radius: 6px; }

/* ===== Viewer ===== */
QWidget#viewerPanel { background-color: #262626; }
QWidget#viewerHeader {
    background-color: #262626; border-bottom: 1px solid rgba(255,255,255,0.085);
}
QLabel#viewerTitle { font-size: 13px; font-weight: 700; color: #f5f5f5; background: transparent; }
QPushButton#pageBtn {
    background-color: transparent; color: #a9a9a9; border: 1px solid rgba(255,255,255,0.16);
    border-radius: 6px; min-width: 26px; min-height: 26px; max-width: 26px; max-height: 26px;
    font-size: 13px; font-weight: 700;
}
QPushButton#pageBtn:hover { background-color: rgba(255,255,255,0.055); color: #f5f5f5; }
QPushButton#pageBtn:disabled { color: #4d4d4d; }
QPushButton#zoomBtn {
    background-color: transparent; color: #a9a9a9; border: 1px solid rgba(255,255,255,0.16);
    border-radius: 6px; min-width: 26px; min-height: 26px; max-width: 26px; max-height: 26px;
    font-size: 13px; font-weight: 700;
}
QPushButton#zoomBtn:hover { background-color: rgba(255,255,255,0.055); color: #f5f5f5; }
QLabel#zoomLevel { font-size: 11px; color: #a9a9a9; background: transparent; min-width: 36px; qproperty-alignment: AlignCenter; }
QTextEdit#documentPage {
    background-color: #f7f4ec; color: #2a2a2a; border: none;
    padding: 22px 24px; font-size: 13px; border-radius: 8px;
}

/* ===== Extracted panel ===== */
QWidget#extractedPanel {
    background-color: #262626; border-top: 1px solid rgba(255,255,255,0.085);
}
QWidget#extractedTabHeader {
    background-color: #262626; border-bottom: 1px solid rgba(255,255,255,0.085);
}
QPushButton#extractedTab {
    background: transparent; color: #a9a9a9; border: none;
    border-bottom: 3px solid transparent; padding: 8px 13px;
    font-size: 12px; font-weight: 600; border-radius: 7px 7px 0 0;
}
QPushButton#extractedTab:hover { color: #f5f5f5; }
QPushButton#extractedTab:checked { color: #f5f5f5; border-bottom-color: #4cc2ff; }
QTextEdit#extractedContent {
    background-color: #2d2d2d; color: #a9a9a9; border: none;
    padding: 8px 12px; font-size: 12px;
}
QPushButton#openBtn {
    background-color: #4cc2ff; color: #003049; border: none;
    border-radius: 6px; padding: 5px 12px; font-size: 12px; font-weight: 600;
}
QPushButton#openBtn:hover { background-color: #67d4ff; }
QPushButton#ocrBtn {
    background-color: #6ccb9f; color: #003b22; border: none;
    border-radius: 6px; padding: 5px 12px; font-size: 12px; font-weight: 600;
}
QPushButton#ocrBtn:hover { background-color: #87dab2; }
QPushButton#copyBtn {
    background-color: #4cc2ff; color: #003049; border: none;
    border-radius: 6px; padding: 4px 10px; font-size: 11px;
}
QPushButton#copyBtn:hover { background-color: #67d4ff; }
QPushButton#downloadBtn {
    background-color: #6ccb9f; color: #003b22; border: none;
    border-radius: 6px; padding: 4px 10px; font-size: 11px;
}
QPushButton#downloadBtn:hover { background-color: #87dab2; }

/* ===== Metadata ===== */
QWidget#metadataPanel {
    background-color: #262626; border-left: 1px solid rgba(255,255,255,0.085);
}
QLabel#metadataTitle { font-size: 13px; font-weight: 700; color: #f5f5f5; background: transparent; }
QLabel#metaLabel { font-size: 11px; color: #7d7d7d; background: transparent; min-width: 70px; }
QLabel#metaValue { font-size: 11.5px; color: #f5f5f5; font-weight: 500; background: transparent; }

/* ===== Tags ===== */
QWidget#tagsSection { background-color: transparent; border-bottom: 1px solid rgba(255,255,255,0.085); }
QLabel#tagsHeader { font-size: 13px; font-weight: 700; color: #f5f5f5; background: transparent; }
QPushButton#tagBlue {
    background-color: rgba(76,194,255,0.14); color: #4cc2ff;
    border: 1px solid rgba(76,194,255,0.30);
    border-radius: 999px; padding: 4px 9px; font-size: 11px; font-weight: 600;
}
QPushButton#tagGreen {
    background-color: rgba(108,203,159,0.14); color: #6ccb9f;
    border: 1px solid rgba(108,203,159,0.30);
    border-radius: 999px; padding: 4px 9px; font-size: 11px; font-weight: 600;
}
QPushButton#tagYellow {
    background-color: rgba(255,207,107,0.14); color: #ffcf6b;
    border: 1px solid rgba(255,207,107,0.30);
    border-radius: 999px; padding: 4px 9px; font-size: 11px; font-weight: 600;
}
QPushButton#tagPurple {
    background-color: rgba(119,25,170,0.20); color: #c586d4;
    border: 1px solid rgba(197,134,212,0.30);
    border-radius: 999px; padding: 4px 9px; font-size: 11px; font-weight: 600;
}
QPushButton#addTagBtn {
    background: transparent; color: #7d7d7d;
    border: 1px dashed rgba(255,255,255,0.16);
    border-radius: 999px; padding: 4px 10px; font-size: 11px;
}
QPushButton#addTagBtn:hover { border-color: #4cc2ff; color: #f5f5f5; }

/* ===== Notes ===== */
QWidget#notesSection { background-color: transparent; }
QLabel#notesHeader { font-size: 13px; font-weight: 700; color: #f5f5f5; background: transparent; }
QTextEdit#notesContent {
    background-color: #2d2d2d; color: #f5f5f5;
    border: 1px solid rgba(255,255,255,0.085);
    border-radius: 9px; padding: 11px 12px; font-size: 12px;
}
QTextEdit#notesContent:focus { border: 1px solid #4cc2ff; }
QLabel#notesModified { font-size: 10.5px; color: #7d7d7d; background: transparent; }

/* ===== Status bar ===== */
QStatusBar {
    background-color: #262626; color: #a9a9a9;
    border-top: 1px solid rgba(255,255,255,0.085);
    font-size: 11px; font-family: 'JetBrains Mono','Cascadia Code',Consolas,monospace;
}
QStatusBar::item { border: none; }
QLabel#statusDot {
    background-color: #6ccb9f; border-radius: 3px;
    min-width: 7px; min-height: 7px; max-width: 7px; max-height: 7px;
}
QLabel#statusReady { color: #a9a9a9; font-size: 11px; background: transparent; }
QLabel#statusInfo { color: #a9a9a9; font-size: 11px; background: transparent; }
QPushButton#openLocationBtn {
    background-color: transparent; color: #a9a9a9;
    border: 1px solid rgba(255,255,255,0.16);
    border-radius: 4px; padding: 3px 10px; font-size: 11px;
}
QPushButton#openLocationBtn:hover { background-color: rgba(255,255,255,0.055); color: #f5f5f5; }

/* ===== AI switch control ===== */
QWidget#aiControlWidget {
    background-color: transparent;
    border: 1px solid rgba(255,255,255,0.16);
    border-radius: 999px;
    padding: 2px 4px;
}
QLabel#aiIconLbl { background: transparent; color: #4cc2ff; font-size: 14px; }
QLabel#aiStateLbl { background: transparent; color: #67d4ff; font-size: 11px; font-weight: 800; min-width: 24px; }

/* ===== Generic widgets ===== */
QLineEdit {
    background-color: #2d2d2d; color: #f5f5f5;
    border: 1px solid rgba(255,255,255,0.16); border-radius: 8px;
    padding: 7px 12px; font-size: 13px;
}
QLineEdit:focus { border: 1px solid #4cc2ff; }
QComboBox {
    background-color: #2d2d2d; color: #f5f5f5;
    border: 1px solid rgba(255,255,255,0.16);
    border-radius: 8px; padding: 6px 10px;
}
QComboBox:focus { border: 1px solid #4cc2ff; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox::down-arrow {
    image: none; width: 0; height: 0;
    border-left: 4px solid transparent; border-right: 4px solid transparent;
    border-top: 5px solid #a9a9a9;
}
QComboBox QAbstractItemView {
    background-color: #2d2d2d; color: #f5f5f5;
    border: 1px solid rgba(255,255,255,0.085); border-radius: 6px;
    padding: 4px;
    selection-background-color: rgba(76,194,255,0.14);
    selection-color: #4cc2ff; outline: 0;
}
QPushButton {
    background-color: transparent; color: #f5f5f5;
    border: 1px solid rgba(255,255,255,0.16);
    border-radius: 7px; padding: 7px 13px; font-size: 12.5px;
}
QPushButton:hover { background-color: rgba(255,255,255,0.055); }
QListWidget {
    background-color: #262626; color: #f5f5f5;
    border: 1px solid rgba(255,255,255,0.085); border-radius: 8px; outline: 0;
}
QListWidget::item { padding: 5px 10px; border-bottom: 1px solid rgba(255,255,255,0.05); }
QListWidget::item:hover { background-color: rgba(255,255,255,0.055); }
QListWidget::item:selected { background-color: rgba(76,194,255,0.14); color: #f5f5f5; }
QTextEdit, QPlainTextEdit {
    background-color: #2d2d2d; color: #f5f5f5;
    border: 1px solid rgba(255,255,255,0.085); border-radius: 8px;
    padding: 8px 12px; font-size: 13px;
}
QGroupBox {
    border: 1px solid rgba(255,255,255,0.085); border-radius: 8px;
    margin-top: 14px; padding: 16px 12px 12px 12px;
    background-color: #262626; font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin; subcontrol-position: top left; left: 12px;
    padding: 0 6px; background-color: #262626; color: #a9a9a9;
}
QCheckBox, QRadioButton { color: #f5f5f5; spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 16px; height: 16px;
    border: 1.5px solid rgba(255,255,255,0.16);
    background: #2d2d2d; border-radius: 4px;
}
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background: #4cc2ff; border-color: #4cc2ff;
}
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar:horizontal { background: transparent; height: 10px; }
QScrollBar::handle {
    background: rgba(255,255,255,0.22); border-radius: 8px;
    min-height: 30px; min-width: 30px;
}
QScrollBar::handle:hover { background: rgba(255,255,255,0.40); }
QScrollBar::add-line, QScrollBar::sub-line,
QScrollBar::add-page, QScrollBar::sub-page { background: none; height: 0; width: 0; }
QSplitter::handle { background-color: rgba(255,255,255,0.085); }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QSplitter::handle:hover { background-color: rgba(76,194,255,0.30); }
QTabWidget::pane {
    border: 1px solid rgba(255,255,255,0.085); border-radius: 8px;
    background-color: #262626;
}
QTabBar::tab {
    background-color: transparent; color: #a9a9a9;
    padding: 8px 13px; border: none; border-bottom: 3px solid transparent;
    font-size: 12px; font-weight: 600;
}
QTabBar::tab:hover { color: #f5f5f5; }
QTabBar::tab:selected { color: #f5f5f5; border-bottom-color: #4cc2ff; }
QScrollArea { background-color: transparent; border: none; }
QToolTip {
    background-color: #2d2d2d; color: #f5f5f5;
    border: 1px solid rgba(255,255,255,0.085);
    border-radius: 6px; padding: 6px 10px; font-size: 12px;
}
QDialog { background-color: #1c1c1c; }
QMenu {
    background-color: #2d2d2d; color: #f5f5f5;
    border: 1px solid rgba(255,255,255,0.16); border-radius: 8px; padding: 6px;
}
QMenu::item { padding: 6px 12px; border-radius: 5px; }
QMenu::item:selected { background-color: rgba(76,194,255,0.14); color: #4cc2ff; }
QMenu::separator { height: 1px; background: rgba(255,255,255,0.085); margin: 4px 8px; }
)";

// ============================================================
// LIGHT THEME  (mica #f2f1ee, accent #0067c0)
// ============================================================
const char* kQssLight = R"(
* {
    font-family: 'Inter', 'Segoe UI Variable Text', 'Segoe UI', sans-serif;
    font-size: 13px;
    outline: 0;
    color: #1b1b1b;
}

QMainWindow, QWidget {
    background-color: #f2f1ee;
    color: #1b1b1b;
}

/* ===== TITLE BAR ===== */
QWidget#titleBar {
    background-color: #faf9f7;
    border-bottom: 1px solid rgba(0,0,0,0.08);
}
QLabel#titleBarText {
    font-size: 13px; font-weight: 700; color: #1b1b1b; background: transparent;
}
QLabel#titleBarSubtitle {
    font-size: 11.5px; color: #8a8a8a; background: transparent;
}
QLabel#appLogo {
    background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 #0078d4, stop:1 #00b7c3);
    color: #fff; border-radius: 6px;
    font-size: 11px; font-weight: 700; qproperty-alignment: AlignCenter;
}
QPushButton#titleBtn {
    background: transparent; border: none; border-radius: 4px;
    color: #5c5c5c; padding: 4px;
}
QPushButton#titleBtn:hover { background-color: rgba(0,0,0,0.045); color: #1b1b1b; }
QPushButton#titleBtn#closeBtn:hover { background-color: #c42b1c; color: #fff; }

/* ===== SIDEBAR ===== */
QWidget#sidebar {
    background-color: rgba(250,249,247,0.75);
    border-right: 1px solid rgba(0,0,0,0.08);
}
QListWidget#sidebar {
    background-color: transparent; border: none; outline: 0;
    font-size: 12.5px; padding: 6px 0;
}
QListWidget#sidebar::item {
    padding: 7px 9px; border: none; border-radius: 7px;
    color: #5c5c5c; font-weight: 500; margin: 1px 4px;
}
QListWidget#sidebar::item:hover { background-color: rgba(0,0,0,0.045); color: #1b1b1b; }
QListWidget#sidebar::item:selected {
    background-color: rgba(0,103,192,0.10); color: #1b1b1b; font-weight: 600;
    border-left: 3px solid #0067c0;
}
QWidget#indexedStatus {
    background-color: transparent; border-top: 1px solid rgba(0,0,0,0.08);
}
QLabel#indexedHeader {
    font-size: 10px; font-weight: 700; color: #0f7b4a; background: transparent;
    text-transform: uppercase; letter-spacing: 1px;
}
QLabel#indexedInfo { font-size: 11px; color: #5c5c5c; background: transparent; }
QProgressBar#indexedBar {
    background-color: rgba(0,0,0,0.08); border: none; border-radius: 2px;
    min-height: 4px; max-height: 4px; text-align: center; color: transparent;
}
QProgressBar#indexedBar::chunk { background-color: #0067c0; border-radius: 2px; }

/* ===== SEARCH BAR ===== */
QWidget#searchBarArea {
    background-color: #faf9f7; border-bottom: 1px solid rgba(0,0,0,0.08);
}
QLineEdit#searchInput {
    background-color: #ffffff; color: #1b1b1b;
    border: 1px solid rgba(0,0,0,0.16); border-radius: 8px;
    padding: 7px 36px; font-size: 13px;
    selection-background-color: #0067c0; selection-color: #fff;
}
QLineEdit#searchInput:focus {
    border: 1px solid #0067c0;
    background-color: #ffffff;
}

/* ===== Buttons ===== */
QPushButton#addFolderBtn {
    background-color: #0067c0; color: #fff; border: none;
    border-radius: 7px; padding: 0 13px; font-size: 12.5px; font-weight: 600;
    min-height: 36px;
}
QPushButton#addFolderBtn:hover { background-color: #005a9e; }
QPushButton#searchBtn {
    background-color: #0067c0; color: #fff; border: none;
    border-radius: 7px; padding: 0 16px; font-size: 12.5px; font-weight: 600;
    min-height: 36px;
}
QPushButton#searchBtn:hover { background-color: #005a9e; }
QPushButton#extractBtn {
    background-color: #0067c0; color: #fff; border: none;
    border-radius: 7px; padding: 0 13px; font-size: 12.5px; font-weight: 600;
    min-height: 36px;
}
QPushButton#extractBtn:hover { background-color: #005a9e; }
QPushButton#refreshBtn {
    background-color: transparent; color: #5c5c5c; border: none;
    border-radius: 7px; padding: 6px; min-width: 36px; min-height: 36px;
    max-width: 36px; max-height: 36px;
}
QPushButton#refreshBtn:hover { background-color: rgba(0,0,0,0.045); color: #1b1b1b; }

/* ===== Results panel ===== */
QWidget#resultsPanel {
    background-color: transparent; border-right: 1px solid rgba(0,0,0,0.08);
}
QLabel#resultsTitle { font-size: 14px; font-weight: 700; color: #1b1b1b; background: transparent; }
QLabel#resultsCount { font-size: 11.5px; color: #8a8a8a; background: transparent; }
QComboBox#sortSelect {
    background-color: #ffffff; color: #1b1b1b;
    border: 1px solid rgba(0,0,0,0.16);
    border-radius: 6px; padding: 5px 8px; font-size: 11.5px;
}
QListWidget#resultsList {
    background-color: transparent; border: none; outline: 0; padding: 2px 12px 18px;
}
QListWidget#resultsList::item {
    border: 1px solid transparent; border-radius: 9px; margin-bottom: 2px;
}
QListWidget#resultsList::item:hover { background-color: rgba(0,0,0,0.045); }
QListWidget#resultsList::item:selected {
    background-color: rgba(0,103,192,0.10);
    border-color: rgba(0,103,192,0.35);
}
QLabel#resultTitle { font-size: 13px; font-weight: 600; color: #1b1b1b; background: transparent; }
QLabel#resultSnippet { font-size: 12px; color: #5c5c5c; background: transparent; }
QLabel#resultMeta { font-size: 10.5px; color: #8a8a8a; background: transparent; }
QLabel#fileIconBadge { color: #fff; font-size: 9px; font-weight: 700; border-radius: 6px; }

/* ===== Viewer ===== */
QWidget#viewerPanel { background-color: #faf9f7; }
QWidget#viewerHeader {
    background-color: #faf9f7; border-bottom: 1px solid rgba(0,0,0,0.08);
}
QLabel#viewerTitle { font-size: 13px; font-weight: 700; color: #1b1b1b; background: transparent; }
QPushButton#pageBtn {
    background-color: transparent; color: #5c5c5c; border: 1px solid rgba(0,0,0,0.16);
    border-radius: 6px; min-width: 26px; min-height: 26px; max-width: 26px; max-height: 26px;
    font-size: 13px; font-weight: 700;
}
QPushButton#pageBtn:hover { background-color: rgba(0,0,0,0.045); color: #1b1b1b; }
QPushButton#pageBtn:disabled { color: #c2c2c2; }
QPushButton#zoomBtn {
    background-color: transparent; color: #5c5c5c; border: 1px solid rgba(0,0,0,0.16);
    border-radius: 6px; min-width: 26px; min-height: 26px; max-width: 26px; max-height: 26px;
    font-size: 13px; font-weight: 700;
}
QPushButton#zoomBtn:hover { background-color: rgba(0,0,0,0.045); color: #1b1b1b; }
QLabel#zoomLevel { font-size: 11px; color: #5c5c5c; background: transparent; min-width: 36px; qproperty-alignment: AlignCenter; }
QTextEdit#documentPage {
    background-color: #ffffff; color: #222; border: none;
    padding: 22px 24px; font-size: 13px; border-radius: 8px;
}

/* ===== Extracted panel ===== */
QWidget#extractedPanel {
    background-color: #faf9f7; border-top: 1px solid rgba(0,0,0,0.08);
}
QWidget#extractedTabHeader {
    background-color: #faf9f7; border-bottom: 1px solid rgba(0,0,0,0.08);
}
QPushButton#extractedTab {
    background: transparent; color: #5c5c5c; border: none;
    border-bottom: 3px solid transparent; padding: 8px 13px;
    font-size: 12px; font-weight: 600; border-radius: 7px 7px 0 0;
}
QPushButton#extractedTab:hover { color: #1b1b1b; }
QPushButton#extractedTab:checked { color: #1b1b1b; border-bottom-color: #0067c0; }
QTextEdit#extractedContent {
    background-color: #f4f4f4; color: #5c5c5c; border: none;
    padding: 8px 12px; font-size: 12px;
}
QPushButton#openBtn {
    background-color: #0067c0; color: #fff; border: none;
    border-radius: 6px; padding: 5px 12px; font-size: 12px; font-weight: 600;
}
QPushButton#openBtn:hover { background-color: #005a9e; }
QPushButton#ocrBtn {
    background-color: #0f7b4a; color: #fff; border: none;
    border-radius: 6px; padding: 5px 12px; font-size: 12px; font-weight: 600;
}
QPushButton#ocrBtn:hover { background-color: #0c653c; }
QPushButton#copyBtn {
    background-color: #0067c0; color: #fff; border: none;
    border-radius: 6px; padding: 4px 10px; font-size: 11px;
}
QPushButton#copyBtn:hover { background-color: #005a9e; }
QPushButton#downloadBtn {
    background-color: #0f7b4a; color: #fff; border: none;
    border-radius: 6px; padding: 4px 10px; font-size: 11px;
}
QPushButton#downloadBtn:hover { background-color: #0c653c; }

/* ===== Metadata ===== */
QWidget#metadataPanel {
    background-color: #faf9f7; border-left: 1px solid rgba(0,0,0,0.08);
}
QLabel#metadataTitle { font-size: 13px; font-weight: 700; color: #1b1b1b; background: transparent; }
QLabel#metaLabel { font-size: 11px; color: #8a8a8a; background: transparent; min-width: 70px; }
QLabel#metaValue { font-size: 11.5px; color: #1b1b1b; font-weight: 500; background: transparent; }

/* ===== Tags ===== */
QWidget#tagsSection { background-color: transparent; border-bottom: 1px solid rgba(0,0,0,0.08); }
QLabel#tagsHeader { font-size: 13px; font-weight: 700; color: #1b1b1b; background: transparent; }
QPushButton#tagBlue {
    background-color: rgba(0,103,192,0.10); color: #0067c0;
    border: 1px solid rgba(0,103,192,0.30);
    border-radius: 999px; padding: 4px 9px; font-size: 11px; font-weight: 600;
}
QPushButton#tagGreen {
    background-color: rgba(15,123,74,0.10); color: #0f7b4a;
    border: 1px solid rgba(15,123,74,0.30);
    border-radius: 999px; padding: 4px 9px; font-size: 11px; font-weight: 600;
}
QPushButton#tagYellow {
    background-color: rgba(138,91,0,0.10); color: #8a5b00;
    border: 1px solid rgba(138,91,0,0.30);
    border-radius: 999px; padding: 4px 9px; font-size: 11px; font-weight: 600;
}
QPushButton#tagPurple {
    background-color: rgba(119,25,170,0.10); color: #7719aa;
    border: 1px solid rgba(119,25,170,0.30);
    border-radius: 999px; padding: 4px 9px; font-size: 11px; font-weight: 600;
}
QPushButton#addTagBtn {
    background: transparent; color: #8a8a8a;
    border: 1px dashed rgba(0,0,0,0.16);
    border-radius: 999px; padding: 4px 10px; font-size: 11px;
}
QPushButton#addTagBtn:hover { border-color: #0067c0; color: #1b1b1b; }

/* ===== Notes ===== */
QWidget#notesSection { background-color: transparent; }
QLabel#notesHeader { font-size: 13px; font-weight: 700; color: #1b1b1b; background: transparent; }
QTextEdit#notesContent {
    background-color: #ffffff; color: #1b1b1b;
    border: 1px solid rgba(0,0,0,0.08);
    border-radius: 9px; padding: 11px 12px; font-size: 12px;
}
QTextEdit#notesContent:focus { border: 1px solid #0067c0; }
QLabel#notesModified { font-size: 10.5px; color: #8a8a8a; background: transparent; }

/* ===== Status bar ===== */
QStatusBar {
    background-color: #faf9f7; color: #5c5c5c;
    border-top: 1px solid rgba(0,0,0,0.08);
    font-size: 11px; font-family: 'JetBrains Mono','Cascadia Code',Consolas,monospace;
}
QStatusBar::item { border: none; }
QLabel#statusDot {
    background-color: #0f7b4a; border-radius: 3px;
    min-width: 7px; min-height: 7px; max-width: 7px; max-height: 7px;
}
QLabel#statusReady { color: #5c5c5c; font-size: 11px; background: transparent; }
QLabel#statusInfo { color: #5c5c5c; font-size: 11px; background: transparent; }
QPushButton#openLocationBtn {
    background-color: transparent; color: #5c5c5c;
    border: 1px solid rgba(0,0,0,0.16);
    border-radius: 4px; padding: 3px 10px; font-size: 11px;
}
QPushButton#openLocationBtn:hover { background-color: rgba(0,0,0,0.045); color: #1b1b1b; }

/* ===== AI switch control ===== */
QWidget#aiControlWidget {
    background-color: transparent;
    border: 1px solid rgba(0,0,0,0.16);
    border-radius: 999px;
    padding: 2px 4px;
}
QLabel#aiIconLbl { background: transparent; color: #0067c0; font-size: 14px; }
QLabel#aiStateLbl { background: transparent; color: #005a9e; font-size: 11px; font-weight: 800; min-width: 24px; }

/* ===== Generic widgets ===== */
QLineEdit {
    background-color: #ffffff; color: #1b1b1b;
    border: 1px solid rgba(0,0,0,0.16); border-radius: 8px;
    padding: 7px 12px; font-size: 13px;
}
QLineEdit:focus { border: 1px solid #0067c0; }
QComboBox {
    background-color: #ffffff; color: #1b1b1b;
    border: 1px solid rgba(0,0,0,0.16);
    border-radius: 8px; padding: 6px 10px;
}
QComboBox:focus { border: 1px solid #0067c0; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox::down-arrow {
    image: none; width: 0; height: 0;
    border-left: 4px solid transparent; border-right: 4px solid transparent;
    border-top: 5px solid #5c5c5c;
}
QComboBox QAbstractItemView {
    background-color: #ffffff; color: #1b1b1b;
    border: 1px solid rgba(0,0,0,0.08); border-radius: 6px;
    padding: 4px;
    selection-background-color: rgba(0,103,192,0.10);
    selection-color: #0067c0; outline: 0;
}
QPushButton {
    background-color: transparent; color: #1b1b1b;
    border: 1px solid rgba(0,0,0,0.16);
    border-radius: 7px; padding: 7px 13px; font-size: 12.5px;
}
QPushButton:hover { background-color: rgba(0,0,0,0.045); }
QListWidget {
    background-color: #ffffff; color: #1b1b1b;
    border: 1px solid rgba(0,0,0,0.08); border-radius: 8px; outline: 0;
}
QListWidget::item { padding: 5px 10px; border-bottom: 1px solid rgba(0,0,0,0.04); }
QListWidget::item:hover { background-color: rgba(0,0,0,0.045); }
QListWidget::item:selected { background-color: rgba(0,103,192,0.10); color: #1b1b1b; }
QTextEdit, QPlainTextEdit {
    background-color: #ffffff; color: #1b1b1b;
    border: 1px solid rgba(0,0,0,0.08); border-radius: 8px;
    padding: 8px 12px; font-size: 13px;
}
QGroupBox {
    border: 1px solid rgba(0,0,0,0.08); border-radius: 8px;
    margin-top: 14px; padding: 16px 12px 12px 12px;
    background-color: #faf9f7; font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin; subcontrol-position: top left; left: 12px;
    padding: 0 6px; background-color: #faf9f7; color: #5c5c5c;
}
QCheckBox, QRadioButton { color: #1b1b1b; spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 16px; height: 16px;
    border: 1.5px solid rgba(0,0,0,0.16);
    background: #ffffff; border-radius: 4px;
}
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background: #0067c0; border-color: #0067c0;
}
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar:horizontal { background: transparent; height: 10px; }
QScrollBar::handle {
    background: rgba(0,0,0,0.25); border-radius: 8px;
    min-height: 30px; min-width: 30px;
}
QScrollBar::handle:hover { background: rgba(0,0,0,0.40); }
QScrollBar::add-line, QScrollBar::sub-line,
QScrollBar::add-page, QScrollBar::sub-page { background: none; height: 0; width: 0; }
QSplitter::handle { background-color: rgba(0,0,0,0.08); }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QSplitter::handle:hover { background-color: rgba(0,103,192,0.30); }
QTabWidget::pane {
    border: 1px solid rgba(0,0,0,0.08); border-radius: 8px;
    background-color: #faf9f7;
}
QTabBar::tab {
    background-color: transparent; color: #5c5c5c;
    padding: 8px 13px; border: none; border-bottom: 3px solid transparent;
    font-size: 12px; font-weight: 600;
}
QTabBar::tab:hover { color: #1b1b1b; }
QTabBar::tab:selected { color: #1b1b1b; border-bottom-color: #0067c0; }
QScrollArea { background-color: transparent; border: none; }
QToolTip {
    background-color: #1b1b1b; color: #ffffff;
    border: 1px solid rgba(0,0,0,0.16);
    border-radius: 6px; padding: 6px 10px; font-size: 12px;
}
QDialog { background-color: #f2f1ee; }
QMenu {
    background-color: #ffffff; color: #1b1b1b;
    border: 1px solid rgba(0,0,0,0.16); border-radius: 8px; padding: 6px;
}
QMenu::item { padding: 6px 12px; border-radius: 5px; }
QMenu::item:selected { background-color: rgba(0,103,192,0.10); color: #0067c0; }
QMenu::separator { height: 1px; background: rgba(0,0,0,0.08); margin: 4px 8px; }
)";

} // namespace

void Theme::apply(Mode mode) {
    qApp->setStyleSheet(stylesheet(mode));
    // Also tweak the QPalette so non-QSS-aware widgets (system dialogs,
    // tooltips, OS chrome) pick up the right colors.
    QPalette p;
    if (mode == Mode::Dark) {
        p.setColor(QPalette::Window,          QColor("#1c1c1c"));
        p.setColor(QPalette::WindowText,      QColor("#f5f5f5"));
        p.setColor(QPalette::Base,            QColor("#262626"));
        p.setColor(QPalette::AlternateBase,   QColor("#2d2d2d"));
        p.setColor(QPalette::Text,            QColor("#f5f5f5"));
        p.setColor(QPalette::Button,          QColor("#2d2d2d"));
        p.setColor(QPalette::ButtonText,     QColor("#f5f5f5"));
        p.setColor(QPalette::Highlight,       QColor("#4cc2ff"));
        p.setColor(QPalette::HighlightedText, QColor("#003049"));
        p.setColor(QPalette::ToolTipBase,     QColor("#2d2d2d"));
        p.setColor(QPalette::ToolTipText,    QColor("#f5f5f5"));
    } else {
        p.setColor(QPalette::Window,          QColor("#f2f1ee"));
        p.setColor(QPalette::WindowText,      QColor("#1b1b1b"));
        p.setColor(QPalette::Base,            QColor("#ffffff"));
        p.setColor(QPalette::AlternateBase,   QColor("#faf9f7"));
        p.setColor(QPalette::Text,            QColor("#1b1b1b"));
        p.setColor(QPalette::Button,          QColor("#ffffff"));
        p.setColor(QPalette::ButtonText,     QColor("#1b1b1b"));
        p.setColor(QPalette::Highlight,       QColor("#0067c0"));
        p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        p.setColor(QPalette::ToolTipBase,     QColor("#1b1b1b"));
        p.setColor(QPalette::ToolTipText,    QColor("#ffffff"));
    }
    qApp->setPalette(p);
}

QString Theme::stylesheet(Mode mode) {
    return QString::fromUtf8(mode == Mode::Dark ? kQssDark : kQssLight);
}

QColor Theme::accent(Mode mode) {
    return (mode == Mode::Dark) ? QColor("#4cc2ff") : QColor("#0067c0");
}

namespace {
Theme::PastelPalette g_active;
bool g_activeSet = false;
}

const Theme::PastelPalette& Theme::active() {
    static PastelPalette fallback;
    if (g_activeSet) return g_active;
    // Fallback = Fluent Dark (matches the dark QSS above).
    fallback.bg = "#1c1c1c"; fallback.surface = "#262626"; fallback.surface2 = "#2d2d2d";
    fallback.field = "#2d2d2d"; fallback.border = "rgba(255,255,255,0.085)"; fallback.hover = "rgba(255,255,255,0.055)";
    fallback.text = "#f5f5f5"; fallback.muted = "#a9a9a9";
    fallback.primary = "#4cc2ff"; fallback.primaryStrong = "#67d4ff";
    fallback.primarySoft = "rgba(76,194,255,0.14)"; fallback.primaryBorder = "rgba(76,194,255,0.30)";
    fallback.themeLabel = "Fluent Dark"; fallback.index = 0;
    return fallback;
}

void Theme::setActive(const PastelPalette& p) {
    g_active = p;
    g_activeSet = true;
}

} // namespace DocuSearch
