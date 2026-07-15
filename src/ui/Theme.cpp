// ============================================================
// Theme.cpp - Complete revamp: light-only, clean, professional
// ============================================================

#include "Theme.h"
#include <QApplication>
#include <QPalette>
#include <QFile>

namespace DocuSearch {

namespace {

// Single light theme — no dark mode.
// Design: clean white surfaces, subtle gray borders, blue accents.
// Inspired by: Notion, Linear, Figma — modern SaaS UIs.
const char* kQss = R"(
* {
    font-family: 'Segoe UI Variable Text', 'Segoe UI', sans-serif;
    font-size: 13px;
    outline: 0;
}

QMainWindow, QWidget {
    background-color: #ffffff;
    color: #1a1a1a;
}

/* ===== Title bar — subtle blue tint for visual interest ===== */
QWidget#titleBar {
    background-color: #f8fafc;
    border-bottom: 2px solid #2563eb;
}
QLabel#titleBarText {
    font-size: 13px; font-weight: 700; color: #1a1a1a; background: transparent;
}
QLabel#titleBarSubtitle {
    font-size: 12px; color: #999; background: transparent;
}
QLabel#appLogo {
    background-color: #2563eb; color: #fff; border-radius: 6px;
    font-size: 11px; font-weight: 700; qproperty-alignment: AlignCenter;
}
QPushButton#titleBtn {
    background: transparent; border: none; border-radius: 4px;
    color: #666; padding: 4px;
}
QPushButton#titleBtn:hover { background-color: #f5f5f5; }
QPushButton#titleBtn#closeBtn:hover { background-color: #ef4444; color: #fff; }

/* ===== Sidebar — light gray with blue accent border ===== */
QWidget#sidebar {
    background-color: #f8fafc;
    border-right: 2px solid #e2e8f0;
}
QListWidget#sidebar {
    background-color: #fafafa; border: none; outline: 0;
    font-size: 13px; padding: 6px 0;
}
QListWidget#sidebar::item {
    padding: 7px 14px; border: none; color: #555; font-weight: 500;
}
QListWidget#sidebar::item:hover { background-color: #f0f0f0; color: #1a1a1a; }
QListWidget#sidebar::item:selected {
    background-color: #eff6ff; color: #2563eb; font-weight: 600;
    border-left: 3px solid #2563eb;
}
QWidget#indexedStatus {
    background-color: #fafafa; border-top: 1px solid #e5e5e5;
}
QLabel#indexedHeader {
    font-size: 11px; font-weight: 700; color: #16a34a; background: transparent;
    text-transform: uppercase; letter-spacing: 0.5px;
}
QLabel#indexedInfo { font-size: 11px; color: #888; background: transparent; }
QProgressBar#indexedBar {
    background-color: #e5e5e5; border: none; border-radius: 2px;
    min-height: 3px; max-height: 3px; text-align: center; color: transparent;
}
QProgressBar#indexedBar::chunk { background-color: #16a34a; border-radius: 2px; }

/* ===== Search bar ===== */
QWidget#searchBarArea {
    background-color: #ffffff; border-bottom: 1px solid #e5e5e5;
}
QLineEdit#searchInput {
    background-color: #f9fafb; color: #1a1a1a;
    border: 1px solid #e5e5e5; border-radius: 8px;
    padding: 7px 36px; font-size: 13px;
    selection-background-color: #2563eb; selection-color: #fff;
}
QLineEdit#searchInput:focus { border: 1px solid #2563eb; background-color: #fff; }

/* ===== Buttons ===== */
QPushButton#addFolderBtn {
    background-color: #2563eb; color: #fff; border: none;
    border-radius: 8px; padding: 7px 14px; font-size: 12px; font-weight: 600;
}
QPushButton#addFolderBtn:hover { background-color: #1d4ed8; }
QPushButton#searchBtn {
    background-color: #2563eb; color: #fff; border: none;
    border-radius: 8px; padding: 7px 16px; font-size: 12px; font-weight: 600;
}
QPushButton#searchBtn:hover { background-color: #1d4ed8; }
QPushButton#extractBtn {
    background-color: #7c3aed; color: #fff; border: none;
    border-radius: 8px; padding: 7px 14px; font-size: 12px; font-weight: 600;
}
QPushButton#extractBtn:hover { background-color: #6d28d9; }
QPushButton#refreshBtn {
    background-color: #fff; color: #666; border: 1px solid #e5e5e5;
    border-radius: 8px; padding: 6px; min-width: 34px; min-height: 34px;
    max-width: 34px; max-height: 34px;
}
QPushButton#refreshBtn:hover { background-color: #f9fafb; border-color: #d1d5db; }

/* ===== Results panel ===== */
QWidget#resultsPanel { background-color: #fff; border-right: 1px solid #e5e5e5; }
QLabel#resultsTitle { font-size: 13px; font-weight: 700; color: #1a1a1a; background: transparent; }
QLabel#resultsCount { font-size: 12px; color: #888; background: transparent; }
QComboBox#sortSelect {
    background-color: #f9fafb; color: #555; border: 1px solid #e5e5e5;
    border-radius: 6px; padding: 3px 8px; font-size: 11px;
}
QListWidget#resultsList { background-color: #fff; border: none; outline: 0; padding: 4px; }
QListWidget#resultsList::item { border: 1px solid transparent; border-radius: 8px; margin-bottom: 2px; }
QListWidget#resultsList::item:hover { background-color: #f9fafb; }
QListWidget#resultsList::item:selected { background-color: #eff6ff; border-color: #dbeafe; }
QLabel#resultTitle { font-size: 13px; font-weight: 600; color: #1a1a1a; background: transparent; }
QLabel#resultSnippet { font-size: 11px; color: #777; background: transparent; }
QLabel#resultMeta { font-size: 10px; color: #aaa; background: transparent; }
QLabel#fileIconBadge { color: #fff; font-size: 9px; font-weight: 700; border-radius: 6px; }

/* ===== Viewer ===== */
QWidget#viewerPanel { background-color: #f9fafb; }
QWidget#viewerHeader { background-color: #f8fafc; border-bottom: 2px solid #e2e8f0; }
QLabel#viewerTitle { font-size: 13px; font-weight: 600; color: #1a1a1a; background: transparent; }
QPushButton#pageBtn {
    background-color: #fff; color: #555; border: 1px solid #e5e5e5;
    border-radius: 6px; min-width: 26px; min-height: 26px; max-width: 26px; max-height: 26px;
    font-size: 13px; font-weight: 700;
}
QPushButton#pageBtn:hover { background-color: #f9fafb; }
QPushButton#pageBtn:disabled { color: #ccc; }
QPushButton#zoomBtn {
    background-color: #fff; color: #555; border: 1px solid #e5e5e5;
    border-radius: 6px; min-width: 26px; min-height: 26px; max-width: 26px; max-height: 26px;
    font-size: 13px; font-weight: 700;
}
QPushButton#zoomBtn:hover { background-color: #f9fafb; }
QLabel#zoomLevel { font-size: 11px; color: #777; background: transparent; min-width: 36px; qproperty-alignment: AlignCenter; }
QTextEdit#documentPage { background-color: #fff; color: #1a1a1a; border: none; padding: 24px; font-size: 13px; }

/* ===== Extracted panel ===== */
QWidget#extractedPanel { background-color: #fff; border-top: 1px solid #e5e5e5; }
QPushButton#extractedTab {
    background: transparent; color: #888; border: none;
    border-bottom: 2px solid transparent; padding: 7px 12px;
    font-size: 12px; font-weight: 500;
}
QPushButton#extractedTab:checked { color: #2563eb; border-bottom-color: #2563eb; font-weight: 600; }
QTextEdit#extractedContent { background-color: #f9fafb; color: #555; border: none; padding: 8px 12px; font-size: 12px; }
QPushButton#openBtn { background-color: #2563eb; color: #fff; border: none; border-radius: 6px; padding: 5px 12px; font-size: 12px; font-weight: 600; }
QPushButton#openBtn:hover { background-color: #1d4ed8; }
QPushButton#ocrBtn { background-color: #16a34a; color: #fff; border: none; border-radius: 6px; padding: 5px 12px; font-size: 12px; font-weight: 600; }
QPushButton#ocrBtn:hover { background-color: #15803d; }
QPushButton#copyBtn { background-color: #2563eb; color: #fff; border: none; border-radius: 6px; padding: 4px 10px; font-size: 11px; }
QPushButton#copyBtn:hover { background-color: #1d4ed8; }
QPushButton#downloadBtn { background-color: #16a34a; color: #fff; border: none; border-radius: 6px; padding: 4px 10px; font-size: 11px; }
QPushButton#downloadBtn:hover { background-color: #15803d; }

/* ===== Metadata ===== */
QWidget#metadataPanel { background-color: #fff; border-left: 1px solid #e5e5e5; }
QLabel#metadataTitle { font-size: 13px; font-weight: 700; color: #1a1a1a; background: transparent; }
QLabel#metaLabel { font-size: 11px; color: #888; background: transparent; min-width: 70px; }
QLabel#metaValue { font-size: 12px; color: #1a1a1a; font-weight: 500; background: transparent; }

/* ===== Tags ===== */
QWidget#tagsSection { background-color: #fff; border-bottom: 1px solid #f5f5f5; }
QLabel#tagsHeader { font-size: 13px; font-weight: 700; color: #1a1a1a; background: transparent; }
QPushButton#tagBlue { background-color: #eff6ff; color: #2563eb; border: none; border-radius: 10px; padding: 3px 10px; font-size: 11px; }
QPushButton#tagGreen { background-color: #f0fdf4; color: #16a34a; border: none; border-radius: 10px; padding: 3px 10px; font-size: 11px; }
QPushButton#tagYellow { background-color: #fefce8; color: #ca8a04; border: none; border-radius: 10px; padding: 3px 10px; font-size: 11px; }
QPushButton#tagPurple { background-color: #faf5ff; color: #9333ea; border: none; border-radius: 10px; padding: 3px 10px; font-size: 11px; }
QPushButton#addTagBtn { background: transparent; color: #888; border: 1px dashed #d1d5db; border-radius: 10px; padding: 3px 10px; font-size: 11px; }
QPushButton#addTagBtn:hover { border-color: #bbb; color: #555; }

/* ===== Notes ===== */
QWidget#notesSection { background-color: #fff; }
QLabel#notesHeader { font-size: 13px; font-weight: 700; color: #1a1a1a; background: transparent; }
QTextEdit#notesContent { background-color: #f9fafb; color: #555; border: 1px solid #e5e5e5; border-radius: 8px; padding: 8px 10px; font-size: 12px; }
QLabel#notesModified { font-size: 10px; color: #aaa; background: transparent; }

/* ===== Status bar ===== */
QStatusBar { background-color: #fafafa; color: #888; border-top: 1px solid #e5e5e5; font-size: 11px; }
QStatusBar::item { border: none; }
QLabel#statusDot { background-color: #16a34a; border-radius: 3px; min-width: 6px; min-height: 6px; max-width: 6px; max-height: 6px; }
QLabel#statusReady { color: #888; font-size: 11px; background: transparent; }
QLabel#statusInfo { color: #888; font-size: 11px; background: transparent; }
QPushButton#openLocationBtn { background-color: #fff; color: #555; border: 1px solid #e5e5e5; border-radius: 4px; padding: 3px 10px; font-size: 11px; }
QPushButton#openLocationBtn:hover { background-color: #f9fafb; }

/* ===== Generic ===== */
QLineEdit { background-color: #fff; color: #1a1a1a; border: 1px solid #e5e5e5; border-radius: 8px; padding: 7px 12px; font-size: 13px; }
QLineEdit:focus { border: 1px solid #2563eb; }
QComboBox { background-color: #fff; color: #1a1a1a; border: 1px solid #e5e5e5; border-radius: 8px; padding: 6px 10px; }
QComboBox:focus { border: 1px solid #2563eb; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox::down-arrow { image: none; width: 0; height: 0; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid #888; }
QComboBox QAbstractItemView { background-color: #fff; color: #1a1a1a; border: 1px solid #e5e5e5; border-radius: 6px; padding: 4px; selection-background-color: #eff6ff; selection-color: #2563eb; outline: 0; }
QPushButton { background-color: #fff; color: #1a1a1a; border: 1px solid #e5e5e5; border-radius: 8px; padding: 7px 14px; font-size: 13px; }
QPushButton:hover { background-color: #f9fafb; }
QListWidget { background-color: #fff; color: #1a1a1a; border: 1px solid #e5e5e5; border-radius: 8px; outline: 0; }
QListWidget::item { padding: 5px 10px; border-bottom: 1px solid #f5f5f5; }
QListWidget::item:hover { background-color: #f9fafb; }
QListWidget::item:selected { background-color: #eff6ff; color: #1a1a1a; }
QTextEdit, QPlainTextEdit { background-color: #fff; color: #1a1a1a; border: 1px solid #e5e5e5; border-radius: 8px; padding: 8px 12px; font-size: 13px; }
QGroupBox { border: 1px solid #e5e5e5; border-radius: 8px; margin-top: 14px; padding: 16px 12px 12px 12px; background-color: #fff; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px; padding: 0 6px; background-color: #fff; color: #555; }
QCheckBox, QRadioButton { color: #1a1a1a; spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; border: 2px solid #d1d5db; background: #fff; border-radius: 4px; }
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: #2563eb; border-color: #2563eb; }
QScrollBar:vertical { background: transparent; width: 6px; }
QScrollBar:horizontal { background: transparent; height: 6px; }
QScrollBar::handle { background: #d1d5db; border-radius: 3px; min-height: 30px; min-width: 30px; }
QScrollBar::handle:hover { background: #aaa; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; height: 0; width: 0; }
QSplitter::handle { background-color: #e5e5e5; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QTabWidget::pane { border: 1px solid #e5e5e5; border-radius: 8px; background-color: #fff; }
QTabBar::tab { background-color: transparent; color: #888; padding: 7px 14px; border: none; border-bottom: 2px solid transparent; font-size: 12px; font-weight: 500; }
QTabBar::tab:selected { color: #2563eb; border-bottom-color: #2563eb; }
QScrollArea { background-color: transparent; border: none; }
QToolTip { background-color: #1a1a1a; color: #fff; border: none; border-radius: 6px; padding: 6px 10px; font-size: 12px; }
QDialog { background-color: #fff; }
)";

} // namespace

void Theme::apply(Mode) {
    // Light-only — ignore dark mode setting.
    qApp->setStyleSheet(QString::fromUtf8(kQss));
}

QString Theme::stylesheet(Mode) {
    return QString::fromUtf8(kQss);
}

QColor Theme::accent(Mode) {
    return QColor("#2563eb");
}

} // namespace DocuSearch
