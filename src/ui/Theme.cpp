// ============================================================
// Theme.cpp - Fresh modern UI design
// ============================================================

#include "Theme.h"
#include <QApplication>
#include <QPalette>
#include <QFile>

namespace DocuSearch {

namespace {

const char* kLightQss = R"(
* {
    font-family: 'Segoe UI Variable Text', 'Segoe UI', sans-serif;
    font-size: 13px;
    outline: 0;
}
QMainWindow, QWidget#centralWidget {
    background-color: #f5f7fa;
    color: #1e293b;
}

/* Title bar */
QWidget#titleBar {
    background-color: #1e293b;
    border-bottom: 1px solid #0f172a;
}
QLabel#titleBarText {
    font-size: 13px; font-weight: 600; color: #f1f5f9; background: transparent;
}
QLabel#titleBarSubtitle {
    font-size: 12px; color: #94a3b8; background: transparent;
}
QLabel#appLogo {
    background-color: #3b82f6; color: #ffffff; border-radius: 6px;
    font-size: 12px; font-weight: 700; qproperty-alignment: AlignCenter;
}
QPushButton#titleBtn {
    background: transparent; border: none; border-radius: 4px;
    color: #94a3b8; padding: 4px;
}
QPushButton#titleBtn:hover { background-color: #334155; }
QPushButton#titleBtn#closeBtn:hover { background-color: #dc2626; color: #ffffff; }

/* Sidebar */
QWidget#sidebar { background-color: #1e293b; }
QListWidget#sidebar {
    background-color: #1e293b; border: none; outline: 0;
    font-size: 13px; padding: 4px 0;
}
QListWidget#sidebar::item {
    padding: 8px 14px; border: none; border-left: 3px solid transparent;
    color: #94a3b8; font-weight: 500;
}
QListWidget#sidebar::item:hover { background-color: #334155; color: #e2e8f0; }
QListWidget#sidebar::item:selected {
    background-color: #3b82f6; color: #ffffff;
    border-left-color: #60a5fa; font-weight: 600;
}
QWidget#indexedStatus {
    background-color: #0f172a; border-top: 1px solid #334155;
}
QLabel#indexedHeader {
    font-size: 11px; font-weight: 600; color: #4ade80; background: transparent;
    text-transform: uppercase; letter-spacing: 1px;
}
QLabel#indexedInfo { font-size: 12px; color: #94a3b8; background: transparent; }
QProgressBar#indexedBar {
    background-color: #334155; border: none; border-radius: 2px;
    min-height: 3px; max-height: 3px; text-align: center; color: transparent;
}
QProgressBar#indexedBar::chunk { background-color: #4ade80; border-radius: 2px; }

/* Search bar */
QWidget#searchBarArea {
    background-color: #ffffff; border-bottom: 1px solid #e2e8f0;
}
QLineEdit#searchInput {
    background-color: #f8fafc; color: #1e293b;
    border: 1.5px solid #e2e8f0; border-radius: 8px;
    padding: 8px 36px; font-size: 13px;
    selection-background-color: #3b82f6; selection-color: #ffffff;
}
QLineEdit#searchInput:focus { border: 1.5px solid #3b82f6; background-color: #ffffff; }

/* Buttons */
QPushButton#addFolderBtn {
    background-color: #3b82f6; color: #ffffff; border: none;
    border-radius: 8px; padding: 8px 16px; font-size: 13px; font-weight: 600;
}
QPushButton#addFolderBtn:hover { background-color: #2563eb; }

QPushButton#refreshBtn {
    background-color: #f1f5f9; color: #475569; border: 1.5px solid #e2e8f0;
    border-radius: 8px; padding: 6px; min-width: 36px; min-height: 36px;
    max-width: 36px; max-height: 36px;
}
QPushButton#refreshBtn:hover { background-color: #e2e8f0; border-color: #cbd5e1; }

/* Results panel */
QWidget#resultsPanel { background-color: #ffffff; border-right: 1px solid #e2e8f0; }
QLabel#resultsTitle { font-size: 13px; font-weight: 700; color: #1e293b; background: transparent; }
QLabel#resultsCount { font-size: 12px; color: #64748b; background: transparent; }
QComboBox#sortSelect {
    background-color: #f8fafc; color: #475569; border: 1px solid #e2e8f0;
    border-radius: 6px; padding: 3px 8px; font-size: 12px;
}
QListWidget#resultsList {
    background-color: #ffffff; border: none; outline: 0; padding: 4px;
}
QListWidget#resultsList::item {
    border: 1px solid transparent; border-radius: 8px; margin-bottom: 2px;
}
QListWidget#resultsList::item:hover { background-color: #f8fafc; }
QListWidget#resultsList::item:selected {
    background-color: #eff6ff; border-color: #bfdbfe;
}
QLabel#resultTitle { font-size: 13px; font-weight: 600; color: #1e293b; background: transparent; }
QLabel#resultSnippet { font-size: 11px; color: #64748b; background: transparent; }
QLabel#resultMeta { font-size: 10px; color: #94a3b8; background: transparent; }
QLabel#fileIconBadge { color: #ffffff; font-size: 9px; font-weight: 700; border-radius: 6px; }

/* Viewer */
QWidget#viewerPanel { background-color: #f8fafc; }
QWidget#viewerHeader {
    background-color: #ffffff; border-bottom: 1px solid #e2e8f0;
}
QLabel#viewerTitle { font-size: 13px; font-weight: 600; color: #1e293b; background: transparent; }
QPushButton#pageBtn {
    background-color: #f1f5f9; color: #475569; border: 1px solid #e2e8f0;
    border-radius: 6px; min-width: 28px; min-height: 28px; max-width: 28px; max-height: 28px;
    font-size: 14px; font-weight: 700;
}
QPushButton#pageBtn:hover { background-color: #e2e8f0; }
QPushButton#pageBtn:disabled { color: #cbd5e1; background-color: #f8fafc; }
QPushButton#zoomBtn {
    background-color: #f1f5f9; color: #475569; border: 1px solid #e2e8f0;
    border-radius: 6px; min-width: 28px; min-height: 28px; max-width: 28px; max-height: 28px;
    font-size: 14px; font-weight: 700;
}
QPushButton#zoomBtn:hover { background-color: #e2e8f0; }
QLabel#zoomLevel { font-size: 12px; color: #64748b; background: transparent; min-width: 40px; qproperty-alignment: AlignCenter; }
QTextEdit#documentPage {
    background-color: #ffffff; color: #1e293b; border: none; padding: 20px;
    font-size: 13px;
}

/* Extracted panel */
QWidget#extractedPanel { background-color: #ffffff; border-top: 1px solid #e2e8f0; }
QPushButton#extractedTab {
    background: transparent; color: #64748b; border: none;
    border-bottom: 2px solid transparent; padding: 8px 12px;
    font-size: 12px; font-weight: 500;
}
QPushButton#extractedTab:checked { color: #3b82f6; border-bottom-color: #3b82f6; font-weight: 600; }
QTextEdit#extractedContent {
    background-color: #f8fafc; color: #475569; border: none;
    padding: 8px 12px; font-size: 12px;
}
QPushButton#openBtn {
    background-color: #3b82f6; color: #ffffff; border: none;
    border-radius: 6px; padding: 6px 14px; font-size: 12px; font-weight: 600;
}
QPushButton#openBtn:hover { background-color: #2563eb; }
QPushButton#ocrBtn {
    background-color: #10b981; color: #ffffff; border: none;
    border-radius: 6px; padding: 6px 14px; font-size: 12px; font-weight: 600;
}
QPushButton#ocrBtn:hover { background-color: #059669; }
QPushButton#copyBtn {
    background-color: #3b82f6; color: #ffffff; border: none;
    border-radius: 6px; padding: 5px 12px; font-size: 12px;
}
QPushButton#copyBtn:hover { background-color: #2563eb; }
QPushButton#downloadBtn {
    background-color: #10b981; color: #ffffff; border: none;
    border-radius: 6px; padding: 5px 12px; font-size: 12px;
}
QPushButton#downloadBtn:hover { background-color: #059669; }

/* Metadata */
QWidget#metadataPanel { background-color: #ffffff; border-left: 1px solid #e2e8f0; }
QLabel#metadataTitle { font-size: 13px; font-weight: 700; color: #1e293b; background: transparent; }
QLabel#metaLabel { font-size: 11px; color: #64748b; background: transparent; min-width: 70px; }
QLabel#metaValue { font-size: 12px; color: #1e293b; font-weight: 500; background: transparent; }

/* Tags */
QWidget#tagsSection { background-color: #ffffff; border-bottom: 1px solid #f1f5f9; }
QLabel#tagsHeader { font-size: 13px; font-weight: 700; color: #1e293b; background: transparent; }
QPushButton#tagBlue { background-color: #dbeafe; color: #1d4ed8; border: none; border-radius: 10px; padding: 3px 10px; font-size: 11px; }
QPushButton#tagGreen { background-color: #dcfce7; color: #15803d; border: none; border-radius: 10px; padding: 3px 10px; font-size: 11px; }
QPushButton#tagYellow { background-color: #fef9c3; color: #a16207; border: none; border-radius: 10px; padding: 3px 10px; font-size: 11px; }
QPushButton#tagPurple { background-color: #f3e8ff; color: #7c3aed; border: none; border-radius: 10px; padding: 3px 10px; font-size: 11px; }
QPushButton#addTagBtn { background: transparent; color: #64748b; border: 1px dashed #cbd5e1; border-radius: 10px; padding: 3px 10px; font-size: 11px; }

/* Notes */
QWidget#notesSection { background-color: #ffffff; }
QLabel#notesHeader { font-size: 13px; font-weight: 700; color: #1e293b; background: transparent; }
QTextEdit#notesContent {
    background-color: #f8fafc; color: #475569; border: 1px solid #e2e8f0;
    border-radius: 8px; padding: 8px 10px; font-size: 12px;
}
QLabel#notesModified { font-size: 10px; color: #94a3b8; background: transparent; }

/* Status bar */
QStatusBar { background-color: #1e293b; color: #94a3b8; border-top: 1px solid #0f172a; font-size: 11px; }
QStatusBar::item { border: none; }
QLabel#statusDot { background-color: #4ade80; border-radius: 3px; min-width: 6px; min-height: 6px; max-width: 6px; max-height: 6px; }
QLabel#statusReady { color: #94a3b8; font-size: 11px; background: transparent; }
QLabel#statusInfo { color: #94a3b8; font-size: 11px; background: transparent; }
QPushButton#openLocationBtn {
    background-color: #334155; color: #e2e8f0; border: none;
    border-radius: 4px; padding: 3px 10px; font-size: 11px;
}
QPushButton#openLocationBtn:hover { background-color: #475569; }

/* Generic */
QLineEdit { background-color: #ffffff; color: #1e293b; border: 1.5px solid #e2e8f0; border-radius: 8px; padding: 8px 12px; font-size: 13px; }
QLineEdit:focus { border: 1.5px solid #3b82f6; }
QComboBox { background-color: #ffffff; color: #1e293b; border: 1.5px solid #e2e8f0; border-radius: 8px; padding: 6px 10px; }
QComboBox:focus { border: 1.5px solid #3b82f6; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { image: none; width: 0; height: 0; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid #64748b; }
QComboBox QAbstractItemView { background-color: #ffffff; color: #1e293b; border: 1px solid #e2e8f0; border-radius: 6px; padding: 4px; selection-background-color: #eff6ff; selection-color: #3b82f6; outline: 0; }
QPushButton { background-color: #f1f5f9; color: #1e293b; border: 1.5px solid #e2e8f0; border-radius: 8px; padding: 8px 14px; font-size: 13px; }
QPushButton:hover { background-color: #e2e8f0; }
QListWidget { background-color: #ffffff; color: #1e293b; border: 1px solid #e2e8f0; border-radius: 8px; outline: 0; }
QListWidget::item { padding: 6px 10px; border-bottom: 1px solid #f1f5f9; }
QListWidget::item:hover { background-color: #f8fafc; }
QListWidget::item:selected { background-color: #eff6ff; color: #1e293b; }
QTextEdit, QPlainTextEdit { background-color: #ffffff; color: #1e293b; border: 1px solid #e2e8f0; border-radius: 8px; padding: 8px 12px; font-size: 13px; }
QGroupBox { border: 1px solid #e2e8f0; border-radius: 8px; margin-top: 14px; padding: 16px 12px 12px 12px; background-color: #ffffff; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px; padding: 0 6px; background-color: #ffffff; color: #475569; }
QCheckBox, QRadioButton { color: #1e293b; spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; border: 2px solid #cbd5e1; background: #ffffff; border-radius: 4px; }
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: #3b82f6; border-color: #3b82f6; }
QScrollBar:vertical { background: transparent; width: 6px; }
QScrollBar:horizontal { background: transparent; height: 6px; }
QScrollBar::handle { background: #cbd5e1; border-radius: 3px; min-height: 30px; min-width: 30px; }
QScrollBar::handle:hover { background: #94a3b8; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; height: 0; width: 0; }
QSplitter::handle { background-color: #e2e8f0; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QTabWidget::pane { border: 1px solid #e2e8f0; border-radius: 8px; background-color: #ffffff; }
QTabBar::tab { background-color: transparent; color: #64748b; padding: 8px 14px; border: none; border-bottom: 2px solid transparent; font-size: 12px; font-weight: 500; }
QTabBar::tab:selected { color: #3b82f6; border-bottom-color: #3b82f6; }
QScrollArea { background-color: transparent; border: none; }
QToolTip { background-color: #1e293b; color: #f1f5f9; border: none; border-radius: 6px; padding: 6px 10px; font-size: 12px; }
QDialog { background-color: #f5f7fa; }
)";

const char* kDarkQss = R"(
* {
    font-family: 'Segoe UI Variable Text', 'Segoe UI', sans-serif;
    font-size: 13px;
    outline: 0;
}
QMainWindow, QWidget#centralWidget {
    background-color: #0f172a;
    color: #e2e8f0;
}

/* Title bar */
QWidget#titleBar { background-color: #020617; border-bottom: 1px solid #1e293b; }
QLabel#titleBarText { font-size: 13px; font-weight: 600; color: #f1f5f9; background: transparent; }
QLabel#titleBarSubtitle { font-size: 12px; color: #64748b; background: transparent; }
QLabel#appLogo { background-color: #3b82f6; color: #ffffff; border-radius: 6px; font-size: 12px; font-weight: 700; qproperty-alignment: AlignCenter; }
QPushButton#titleBtn { background: transparent; border: none; border-radius: 4px; color: #64748b; padding: 4px; }
QPushButton#titleBtn:hover { background-color: #1e293b; }
QPushButton#titleBtn#closeBtn:hover { background-color: #dc2626; color: #ffffff; }

/* Sidebar */
QWidget#sidebar { background-color: #020617; }
QListWidget#sidebar { background-color: #020617; border: none; outline: 0; font-size: 13px; padding: 4px 0; }
QListWidget#sidebar::item { padding: 8px 14px; border: none; border-left: 3px solid transparent; color: #64748b; font-weight: 500; }
QListWidget#sidebar::item:hover { background-color: #1e293b; color: #cbd5e1; }
QListWidget#sidebar::item:selected { background-color: #3b82f6; color: #ffffff; border-left-color: #60a5fa; font-weight: 600; }
QWidget#indexedStatus { background-color: #020617; border-top: 1px solid #1e293b; }
QLabel#indexedHeader { font-size: 11px; font-weight: 600; color: #4ade80; background: transparent; text-transform: uppercase; letter-spacing: 1px; }
QLabel#indexedInfo { font-size: 12px; color: #64748b; background: transparent; }
QProgressBar#indexedBar { background-color: #1e293b; border: none; border-radius: 2px; min-height: 3px; max-height: 3px; text-align: center; color: transparent; }
QProgressBar#indexedBar::chunk { background-color: #4ade80; border-radius: 2px; }

/* Search bar */
QWidget#searchBarArea { background-color: #1e293b; border-bottom: 1px solid #0f172a; }
QLineEdit#searchInput { background-color: #0f172a; color: #e2e8f0; border: 1.5px solid #334155; border-radius: 8px; padding: 8px 36px; font-size: 13px; selection-background-color: #3b82f6; selection-color: #ffffff; }
QLineEdit#searchInput:focus { border: 1.5px solid #3b82f6; }
QLineEdit#searchInput::placeholder { color: #475569; }

/* Buttons */
QPushButton#addFolderBtn { background-color: #3b82f6; color: #ffffff; border: none; border-radius: 8px; padding: 8px 16px; font-size: 13px; font-weight: 600; }
QPushButton#addFolderBtn:hover { background-color: #2563eb; }
QPushButton#refreshBtn { background-color: #1e293b; color: #64748b; border: 1.5px solid #334155; border-radius: 8px; padding: 6px; min-width: 36px; min-height: 36px; max-width: 36px; max-height: 36px; }
QPushButton#refreshBtn:hover { background-color: #334155; }

/* Results panel */
QWidget#resultsPanel { background-color: #1e293b; border-right: 1px solid #0f172a; }
QLabel#resultsTitle { font-size: 13px; font-weight: 700; color: #f1f5f9; background: transparent; }
QLabel#resultsCount { font-size: 12px; color: #64748b; background: transparent; }
QComboBox#sortSelect { background-color: #0f172a; color: #94a3b8; border: 1px solid #334155; border-radius: 6px; padding: 3px 8px; font-size: 12px; }
QListWidget#resultsList { background-color: #1e293b; border: none; outline: 0; padding: 4px; }
QListWidget#resultsList::item { border: 1px solid transparent; border-radius: 8px; margin-bottom: 2px; }
QListWidget#resultsList::item:hover { background-color: #334155; }
QListWidget#resultsList::item:selected { background-color: #1e3a8a; border-color: #3b82f6; }
QLabel#resultTitle { font-size: 13px; font-weight: 600; color: #f1f5f9; background: transparent; }
QLabel#resultSnippet { font-size: 11px; color: #64748b; background: transparent; }
QLabel#resultMeta { font-size: 10px; color: #475569; background: transparent; }
QLabel#fileIconBadge { color: #ffffff; font-size: 9px; font-weight: 700; border-radius: 6px; }

/* Viewer */
QWidget#viewerPanel { background-color: #0f172a; }
QWidget#viewerHeader { background-color: #1e293b; border-bottom: 1px solid #0f172a; }
QLabel#viewerTitle { font-size: 13px; font-weight: 600; color: #f1f5f9; background: transparent; }
QPushButton#pageBtn { background-color: #1e293b; color: #94a3b8; border: 1px solid #334155; border-radius: 6px; min-width: 28px; min-height: 28px; max-width: 28px; max-height: 28px; font-size: 14px; font-weight: 700; }
QPushButton#pageBtn:hover { background-color: #334155; }
QPushButton#pageBtn:disabled { color: #334155; background-color: #0f172a; }
QPushButton#zoomBtn { background-color: #1e293b; color: #94a3b8; border: 1px solid #334155; border-radius: 6px; min-width: 28px; min-height: 28px; max-width: 28px; max-height: 28px; font-size: 14px; font-weight: 700; }
QPushButton#zoomBtn:hover { background-color: #334155; }
QLabel#zoomLevel { font-size: 12px; color: #64748b; background: transparent; min-width: 40px; qproperty-alignment: AlignCenter; }
QTextEdit#documentPage { background-color: #1e293b; color: #e2e8f0; border: none; padding: 20px; font-size: 13px; }

/* Extracted panel */
QWidget#extractedPanel { background-color: #1e293b; border-top: 1px solid #0f172a; }
QPushButton#extractedTab { background: transparent; color: #64748b; border: none; border-bottom: 2px solid transparent; padding: 8px 12px; font-size: 12px; font-weight: 500; }
QPushButton#extractedTab:checked { color: #60a5fa; border-bottom-color: #3b82f6; font-weight: 600; }
QTextEdit#extractedContent { background-color: #0f172a; color: #94a3b8; border: none; padding: 8px 12px; font-size: 12px; }
QPushButton#openBtn { background-color: #3b82f6; color: #ffffff; border: none; border-radius: 6px; padding: 6px 14px; font-size: 12px; font-weight: 600; }
QPushButton#openBtn:hover { background-color: #2563eb; }
QPushButton#ocrBtn { background-color: #10b981; color: #ffffff; border: none; border-radius: 6px; padding: 6px 14px; font-size: 12px; font-weight: 600; }
QPushButton#ocrBtn:hover { background-color: #059669; }
QPushButton#copyBtn { background-color: #3b82f6; color: #ffffff; border: none; border-radius: 6px; padding: 5px 12px; font-size: 12px; }
QPushButton#copyBtn:hover { background-color: #2563eb; }
QPushButton#downloadBtn { background-color: #10b981; color: #ffffff; border: none; border-radius: 6px; padding: 5px 12px; font-size: 12px; }
QPushButton#downloadBtn:hover { background-color: #059669; }

/* Metadata */
QWidget#metadataPanel { background-color: #1e293b; border-left: 1px solid #0f172a; }
QLabel#metadataTitle { font-size: 13px; font-weight: 700; color: #f1f5f9; background: transparent; }
QLabel#metaLabel { font-size: 11px; color: #64748b; background: transparent; min-width: 70px; }
QLabel#metaValue { font-size: 12px; color: #e2e8f0; font-weight: 500; background: transparent; }

/* Tags */
QWidget#tagsSection { background-color: #1e293b; border-bottom: 1px solid #0f172a; }
QLabel#tagsHeader { font-size: 13px; font-weight: 700; color: #f1f5f9; background: transparent; }
QPushButton#tagBlue { background-color: #1e3a8a; color: #93c5fd; border: none; border-radius: 10px; padding: 3px 10px; font-size: 11px; }
QPushButton#tagGreen { background-color: #14532d; color: #86efac; border: none; border-radius: 10px; padding: 3px 10px; font-size: 11px; }
QPushButton#tagYellow { background-color: #713f12; color: #fde047; border: none; border-radius: 10px; padding: 3px 10px; font-size: 11px; }
QPushButton#tagPurple { background-color: #581c87; color: #d8b4fe; border: none; border-radius: 10px; padding: 3px 10px; font-size: 11px; }
QPushButton#addTagBtn { background: transparent; color: #64748b; border: 1px dashed #334155; border-radius: 10px; padding: 3px 10px; font-size: 11px; }

/* Notes */
QWidget#notesSection { background-color: #1e293b; }
QLabel#notesHeader { font-size: 13px; font-weight: 700; color: #f1f5f9; background: transparent; }
QTextEdit#notesContent { background-color: #0f172a; color: #94a3b8; border: 1px solid #334155; border-radius: 8px; padding: 8px 10px; font-size: 12px; }
QLabel#notesModified { font-size: 10px; color: #475569; background: transparent; }

/* Status bar */
QStatusBar { background-color: #020617; color: #64748b; border-top: 1px solid #1e293b; font-size: 11px; }
QStatusBar::item { border: none; }
QLabel#statusDot { background-color: #4ade80; border-radius: 3px; min-width: 6px; min-height: 6px; max-width: 6px; max-height: 6px; }
QLabel#statusReady { color: #64748b; font-size: 11px; background: transparent; }
QLabel#statusInfo { color: #64748b; font-size: 11px; background: transparent; }
QPushButton#openLocationBtn { background-color: #1e293b; color: #cbd5e1; border: none; border-radius: 4px; padding: 3px 10px; font-size: 11px; }
QPushButton#openLocationBtn:hover { background-color: #334155; }

/* Generic */
QLineEdit { background-color: #0f172a; color: #e2e8f0; border: 1.5px solid #334155; border-radius: 8px; padding: 8px 12px; font-size: 13px; }
QLineEdit:focus { border: 1.5px solid #3b82f6; }
QLineEdit::placeholder { color: #475569; }
QComboBox { background-color: #0f172a; color: #e2e8f0; border: 1.5px solid #334155; border-radius: 8px; padding: 6px 10px; }
QComboBox:focus { border: 1.5px solid #3b82f6; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { image: none; width: 0; height: 0; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid #64748b; }
QComboBox QAbstractItemView { background-color: #1e293b; color: #e2e8f0; border: 1px solid #334155; border-radius: 6px; padding: 4px; selection-background-color: #1e3a8a; selection-color: #60a5fa; outline: 0; }
QPushButton { background-color: #1e293b; color: #e2e8f0; border: 1.5px solid #334155; border-radius: 8px; padding: 8px 14px; font-size: 13px; }
QPushButton:hover { background-color: #334155; }
QListWidget { background-color: #1e293b; color: #e2e8f0; border: 1px solid #334155; border-radius: 8px; outline: 0; }
QListWidget::item { padding: 6px 10px; border-bottom: 1px solid #0f172a; }
QListWidget::item:hover { background-color: #334155; }
QListWidget::item:selected { background-color: #1e3a8a; color: #ffffff; }
QTextEdit, QPlainTextEdit { background-color: #0f172a; color: #e2e8f0; border: 1px solid #334155; border-radius: 8px; padding: 8px 12px; font-size: 13px; }
QGroupBox { border: 1px solid #334155; border-radius: 8px; margin-top: 14px; padding: 16px 12px 12px 12px; background-color: #1e293b; color: #e2e8f0; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px; padding: 0 6px; background-color: #1e293b; color: #94a3b8; }
QCheckBox, QRadioButton { color: #e2e8f0; spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; border: 2px solid #475569; background: #0f172a; border-radius: 4px; }
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: #3b82f6; border-color: #3b82f6; }
QScrollBar:vertical { background: transparent; width: 6px; }
QScrollBar:horizontal { background: transparent; height: 6px; }
QScrollBar::handle { background: #475569; border-radius: 3px; min-height: 30px; min-width: 30px; }
QScrollBar::handle:hover { background: #64748b; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; height: 0; width: 0; }
QSplitter::handle { background-color: #1e293b; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QTabWidget::pane { border: 1px solid #334155; border-radius: 8px; background-color: #1e293b; }
QTabBar::tab { background-color: transparent; color: #64748b; padding: 8px 14px; border: none; border-bottom: 2px solid transparent; font-size: 12px; font-weight: 500; }
QTabBar::tab:selected { color: #60a5fa; border-bottom-color: #3b82f6; }
QScrollArea { background-color: transparent; border: none; }
QToolTip { background-color: #020617; color: #e2e8f0; border: 1px solid #334155; border-radius: 6px; padding: 6px 10px; font-size: 12px; }
QDialog { background-color: #0f172a; }
)";

} // namespace

void Theme::apply(Mode mode) {
    qApp->setStyleSheet(stylesheet(mode));
}

QString Theme::stylesheet(Mode mode) {
    return QString::fromUtf8(mode == Mode::Dark ? kDarkQss : kLightQss);
}

QColor Theme::accent(Mode mode) {
    return mode == Mode::Dark ? QColor("#3b82f6") : QColor("#3b82f6");
}

} // namespace DocuSearch
