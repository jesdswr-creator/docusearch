// ============================================================
// Theme.cpp - Modern Fluent Design QSS for DocuSearch
// ============================================================
//
// Design tokens (light mode, from reference HTML):
//   Background       : #f0f2f5
//   Panels / cards   : #ffffff
//   Primary text     : #1a1a2e
//   Secondary text   : #6b7280
//   Muted text       : #9ca3af
//   Border           : #e5e7eb
//   Accent (blue)    : #2563eb
//   Accent hover     : #1d4ed8
//   Accent light bg  : #eff6ff
//   Accent light bord: #bfdbfe
//   Success (green)  : #059669
//   Success gradient : #059669 -> #34d399
//   Radius           : 8px (cards/buttons), 6px (small), 12px (tags)
//   Font             : 'Inter', 'Segoe UI Variable Text', 'Segoe UI', sans-serif
//
// Dark mode analogs:
//   Background       : #0f172a
//   Panels / cards   : #1e293b
//   Primary text     : #f1f5f9
//   Secondary text   : #94a3b8
//   Muted text       : #64748b
//   Border           : #334155
//   Accent (blue)    : #3b82f6
//   Accent hover     : #2563eb
//   Accent light bg  : #1e3a8a
//   Accent light bord: #1d4ed8
//   Success (green)  : #10b981
// ============================================================

#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QFile>

namespace DocuSearch {

namespace {

// ============================================================
// LIGHT THEME
// ============================================================
const char* kLightQss = R"(/* ===== Global ===== */
* {
    font-family: 'Inter', 'Segoe UI Variable Text', 'Segoe UI', 'Roboto', sans-serif;
    font-size: 13px;
    outline: 0;
}

QMainWindow, QWidget#centralWidget {
    background-color: #f0f2f5;
    color: #1a1a2e;
}

QWidget {
    color: #1a1a2e;
}
)" R"(/* ===== Title bar ===== */
QWidget#titleBar {
    background-color: #ffffff;
    border-bottom: 1px solid #e0e0e0;
}
QLabel#titleBarText {
    font-size: 14px;
    font-weight: 600;
    color: #1a1a2e;
    background: transparent;
}
QLabel#titleBarSubtitle {
    font-size: 14px;
    font-weight: 400;
    color: #6b7280;
    background: transparent;
}
QLabel#appLogo {
    background-color: #2563eb;
    color: #ffffff;
    border-radius: 8px;
    font-size: 14px;
    font-weight: 700;
    qproperty-alignment: AlignCenter;
}
QPushButton#titleBtn {
    background: transparent;
    border: none;
    border-radius: 6px;
    color: #6b7280;
    padding: 6px;
}
QPushButton#titleBtn:hover {
    background-color: #f3f4f6;
}
QPushButton#titleBtn#closeBtn:hover {
    background-color: #fee2e2;
    color: #dc2626;
}
)" R"(/* ===== Sidebar (left nav) ===== */
QWidget#sidebar {
    background-color: #ffffff;
    border-right: 1px solid #e5e7eb;
}
QListWidget#sidebar {
    background-color: #ffffff;
    border: none;
    outline: 0;
    font-size: 13.5px;
    padding: 8px 0;
}
QListWidget#sidebar::item {
    padding: 10px 16px;
    border: none;
    border-left: 3px solid transparent;
    color: #4b5563;
    font-weight: 500;
    margin: 0;
}
QListWidget#sidebar::item:hover {
    background-color: #f9fafb;
    color: #1a1a2e;
}
QListWidget#sidebar::item:selected {
    background-color: #eff6ff;
    color: #2563eb;
    border-left-color: #2563eb;
    font-weight: 600;
}
QWidget#indexedStatus {
    background-color: #ffffff;
    border-top: 1px solid #e5e7eb;
}
QLabel#indexedHeader {
    font-size: 13px;
    font-weight: 600;
    color: #059669;
    background: transparent;
}
QLabel#indexedInfo {
    font-size: 12px;
    color: #6b7280;
    background: transparent;
}
QProgressBar#indexedBar {
    background-color: #e5e7eb;
    border: none;
    border-radius: 2px;
    min-height: 4px;
    max-height: 4px;
    text-align: center;
    color: transparent;
}
QProgressBar#indexedBar::chunk {
    background-color: #059669;
    border-radius: 2px;
}
)" R"(/* ===== Search bar area ===== */
QWidget#searchBarArea {
    background-color: #ffffff;
    border-bottom: 1px solid #e5e7eb;
}

/* Search input with leading icon -- achieved via QLineEdit + QLineEdit action */
QLineEdit#searchInput {
    background-color: #ffffff;
    color: #1a1a2e;
    border: 1.5px solid #d1d5db;
    border-radius: 8px;
    padding: 9px 36px 9px 36px;
    font-size: 13.5px;
    selection-background-color: #2563eb;
    selection-color: #ffffff;
}
QLineEdit#searchInput:focus {
    border: 1.5px solid #2563eb;
}
QLineEdit#searchInput:hover {
    border-color: #9ca3af;
}

QPushButton#searchBtn {
    background-color: #2563eb;
    color: #ffffff;
    border: none;
    border-radius: 8px;
    padding: 9px 18px;
    font-size: 13.5px;
    font-weight: 600;
    text-align: left;
    padding-left: 12px;
}
QPushButton#searchBtn:hover {
    background-color: #1d4ed8;
}
QPushButton#searchBtn:pressed {
    background-color: #1e40af;
}

QPushButton#shortcutBadge {
    background-color: rgba(255,255,255,0.25);
    color: #ffffff;
    border: none;
    border-radius: 4px;
    padding: 2px 6px;
    font-size: 11px;
    font-weight: 500;
    min-width: 32px;
}

/* Toolbar / action buttons */
QPushButton#toolbarBtn {
    background-color: #ffffff;
    color: #374151;
    border: 1.5px solid #d1d5db;
    border-radius: 8px;
    padding: 8px 14px;
    font-size: 13px;
    text-align: left;
}
QPushButton#toolbarBtn:hover {
    background-color: #f9fafb;
    border-color: #9ca3af;
}
QPushButton#toolbarBtn:pressed {
    background-color: #f3f4f6;
}

QPushButton#iconBtn {
    background-color: #ffffff;
    color: #6b7280;
    border: 1.5px solid #d1d5db;
    border-radius: 8px;
    padding: 6px;
    min-width: 36px;
    min-height: 36px;
    max-width: 36px;
    max-height: 36px;
}
QPushButton#iconBtn:hover {
    background-color: #f9fafb;
    color: #374151;
}
QPushButton#iconBtn:pressed {
    background-color: #f3f4f6;
}
)" R"(/* ===== Results panel ===== */
QWidget#resultsPanel {
    background-color: #ffffff;
    border-right: 1px solid #e5e7eb;
}
QLabel#resultsTitle {
    font-size: 14px;
    font-weight: 700;
    color: #1a1a2e;
    background: transparent;
}
QLabel#resultsCount {
    font-size: 14px;
    font-weight: 400;
    color: #6b7280;
    background: transparent;
}
QComboBox#sortSelect {
    background-color: #ffffff;
    color: #374151;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    padding: 4px 8px;
    font-size: 12px;
    min-height: 22px;
}
QComboBox#sortSelect::drop-down {
    border: none;
    width: 18px;
}
QComboBox#sortSelect::down-arrow {
    image: none;
    width: 0; height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid #6b7280;
}
QComboBox#sortSelect QAbstractItemView {
    background-color: #ffffff;
    color: #1a1a2e;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    padding: 4px;
    selection-background-color: #eff6ff;
    selection-color: #2563eb;
    outline: 0;
}

/* Result items list -- QListWidget with custom item widgets */
QListWidget#resultsList {
    background-color: #ffffff;
    border: none;
    outline: 0;
    padding: 8px;
}
QListWidget#resultsList::item {
    border: 1.5px solid transparent;
    border-radius: 8px;
    margin-bottom: 4px;
    padding: 0;
}
QListWidget#resultsList::item:hover {
    background-color: #f9fafb;
}
QListWidget#resultsList::item:selected {
    background-color: #eff6ff;
    border-color: #bfdbfe;
}

QLabel#resultTitle {
    font-size: 13px;
    font-weight: 600;
    color: #1a1a2e;
    background: transparent;
}
QLabel#resultSnippet {
    font-size: 11.5px;
    color: #6b7280;
    background: transparent;
}
QLabel#resultMeta {
    font-size: 11px;
    color: #9ca3af;
    background: transparent;
}
QLabel#fileIconBadge {
    color: #ffffff;
    font-size: 10px;
    font-weight: 700;
    border-radius: 8px;
    qproperty-alignment: AlignCenter;
}
)" R"(/* ===== Viewer panel ===== */
QWidget#viewerPanel {
    background-color: #f3f4f6;
}
QWidget#viewerHeader {
    background-color: #ffffff;
    border-bottom: 1px solid #e5e7eb;
}
QLabel#viewerTitle {
    font-size: 14px;
    font-weight: 600;
    color: #1a1a2e;
    background: transparent;
}

/* Page navigation buttons */
QPushButton#pageBtn {
    background-color: #ffffff;
    color: #374151;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    padding: 0;
    min-width: 28px;
    min-height: 28px;
    max-width: 28px;
    max-height: 28px;
    font-size: 12px;
}
QPushButton#pageBtn:hover { background-color: #f3f4f6; }
QPushButton#pageBtn:pressed { background-color: #e5e7eb; }
QPushButton#pageBtn:disabled { color: #d1d5db; border-color: #e5e7eb; }

QLineEdit#pageInput {
    background-color: #ffffff;
    color: #1a1a2e;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    padding: 3px 4px;
    font-size: 13px;
    min-width: 32px;
    max-width: 32px;
    qproperty-alignment: AlignCenter;
}

QLabel#pageTotal {
    font-size: 13px;
    color: #374151;
    background: transparent;
    padding: 0 4px;
}

QPushButton#zoomBtn {
    background-color: #ffffff;
    color: #374151;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    padding: 0;
    min-width: 28px;
    min-height: 28px;
    max-width: 28px;
    max-height: 28px;
    font-size: 14px;
}
QPushButton#zoomBtn:hover { background-color: #f3f4f6; }
QPushButton#zoomBtn:pressed { background-color: #e5e7eb; }

QLabel#zoomLevel {
    font-size: 13px;
    color: #374151;
    background: transparent;
    min-width: 44px;
    qproperty-alignment: AlignCenter;
}

/* Document page (text rendering area) */
QTextEdit#documentPage {
    background-color: #ffffff;
    color: #1a1a2e;
    border: none;
    border-radius: 4px;
    padding: 40px 50px;
    font-size: 13px;
    line-height: 1.7;
    selection-background-color: #fef08a;
    selection-color: #1a1a2e;
}

/* Page thumbnails */
QLabel#thumb {
    background-color: #ffffff;
    border: 2px solid #d1d5db;
    border-radius: 4px;
    color: #6b7280;
    font-size: 11px;
    qproperty-alignment: AlignCenter;
}
QLabel#thumbActive {
    background-color: #ffffff;
    border: 2px solid #2563eb;
    border-radius: 4px;
    color: #2563eb;
    font-size: 11px;
    font-weight: 600;
    qproperty-alignment: AlignCenter;
}
QLabel#thumbNum {
    font-size: 11px;
    color: #6b7280;
    font-weight: 600;
    background: transparent;
    qproperty-alignment: AlignCenter;
}
)" R"(/* ===== Extracted text panel (bottom of viewer) ===== */
QWidget#extractedPanel {
    background-color: #ffffff;
    border-top: 1px solid #e5e7eb;
}
QPushButton#extractedTab {
    background: transparent;
    color: #6b7280;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 10px 14px;
    font-size: 12.5px;
    font-weight: 500;
    text-align: left;
}
QPushButton#extractedTab:hover {
    color: #374151;
}
QPushButton#extractedTab:checked {
    color: #2563eb;
    border-bottom-color: #2563eb;
    font-weight: 600;
}
QTextEdit#extractedContent {
    background-color: #ffffff;
    color: #374151;
    border: none;
    padding: 12px 16px;
    font-size: 12.5px;
    selection-background-color: #fef08a;
    selection-color: #1a1a2e;
}
QPushButton#extractedActionBtn {
    background-color: #ffffff;
    color: #6b7280;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    padding: 0;
    min-width: 28px;
    min-height: 28px;
    max-width: 28px;
    max-height: 28px;
    font-size: 12px;
}
QPushButton#extractedActionBtn:hover {
    background-color: #f3f4f6;
}
)" R"(/* ===== Metadata panel ===== */
QWidget#metadataPanel {
    background-color: #ffffff;
    border-left: 1px solid #e5e7eb;
}
QLabel#metadataTitle {
    font-size: 14px;
    font-weight: 700;
    color: #1a1a2e;
    background: transparent;
}
QLabel#infoIcon {
    color: #9ca3af;
    background: transparent;
    border: 1.5px solid #9ca3af;
    border-radius: 9px;
    font-size: 11px;
    qproperty-alignment: AlignCenter;
    min-width: 18px;
    min-height: 18px;
    max-width: 18px;
    max-height: 18px;
}
QPushButton#editBtn {
    background: transparent;
    border: none;
    border-radius: 6px;
    color: #6b7280;
    padding: 4px;
    min-width: 28px;
    min-height: 28px;
    max-width: 28px;
    max-height: 28px;
}
QPushButton#editBtn:hover {
    background-color: #f3f4f6;
}

QWidget#metadataSection {
    background-color: #ffffff;
    border-bottom: 1px solid #f3f4f6;
}
QLabel#metaLabel {
    font-size: 12.5px;
    color: #6b7280;
    background: transparent;
    min-width: 80px;
}
QLabel#metaValue {
    font-size: 12.5px;
    color: #1a1a2e;
    font-weight: 500;
    background: transparent;
}
QLabel#metaIconLabel {
    background: transparent;
}

/* Tags */
QWidget#tagsSection {
    background-color: #ffffff;
    border-bottom: 1px solid #f3f4f6;
}
QLabel#tagsHeader {
    font-size: 14px;
    font-weight: 700;
    color: #1a1a2e;
    background: transparent;
}
QPushButton#tagBlue {
    background-color: #dbeafe;
    color: #1d4ed8;
    border: none;
    border-radius: 12px;
    padding: 4px 12px;
    font-size: 12px;
    font-weight: 500;
}
QPushButton#tagGreen {
    background-color: #dcfce7;
    color: #15803d;
    border: none;
    border-radius: 12px;
    padding: 4px 12px;
    font-size: 12px;
    font-weight: 500;
}
QPushButton#tagYellow {
    background-color: #fef9c3;
    color: #a16207;
    border: none;
    border-radius: 12px;
    padding: 4px 12px;
    font-size: 12px;
    font-weight: 500;
}
QPushButton#tagPurple {
    background-color: #f3e8ff;
    color: #7c3aed;
    border: none;
    border-radius: 12px;
    padding: 4px 12px;
    font-size: 12px;
    font-weight: 500;
}
QPushButton#addTagBtn {
    background: transparent;
    color: #6b7280;
    border: 1.5px dashed #d1d5db;
    border-radius: 12px;
    padding: 4px 12px;
    font-size: 12px;
}
QPushButton#addTagBtn:hover {
    border-color: #9ca3af;
    color: #374151;
}

/* Notes */
QWidget#notesSection {
    background-color: #ffffff;
}
QLabel#notesHeader {
    font-size: 14px;
    font-weight: 700;
    color: #1a1a2e;
    background: transparent;
}
QTextEdit#notesContent {
    background-color: #f9fafb;
    color: #374151;
    border: 1px solid #e5e7eb;
    border-radius: 8px;
    padding: 10px 12px;
    font-size: 12.5px;
}
QLabel#notesModified {
    font-size: 11px;
    color: #9ca3af;
    background: transparent;
}
)" R"(/* ===== Status bar ===== */
QStatusBar {
    background-color: #ffffff;
    color: #6b7280;
    border-top: 1px solid #e5e7eb;
    font-size: 12px;
    padding: 0 16px;
}
QStatusBar::item { border: none; }
QLabel#statusReady {
    color: #6b7280;
    font-size: 12px;
    background: transparent;
}
QLabel#statusDot {
    background-color: #059669;
    border-radius: 4px;
    min-width: 8px;
    min-height: 8px;
    max-width: 8px;
    max-height: 8px;
}
QLabel#statusInfo {
    color: #6b7280;
    font-size: 12px;
    background: transparent;
}
QPushButton#openLocationBtn {
    background-color: #ffffff;
    color: #374151;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    padding: 4px 12px;
    font-size: 12px;
}
QPushButton#openLocationBtn:hover {
    background-color: #f3f4f6;
}
)" R"(/* ===== Generic widgets ===== */
QLineEdit {
    background-color: #ffffff;
    color: #1a1a2e;
    border: 1.5px solid #d1d5db;
    border-radius: 8px;
    padding: 8px 12px;
    font-size: 13px;
    selection-background-color: #2563eb;
    selection-color: #ffffff;
}
QLineEdit:focus { border: 1.5px solid #2563eb; }
QLineEdit:hover { border-color: #9ca3af; }

QComboBox {
    background-color: #ffffff;
    color: #1a1a2e;
    border: 1.5px solid #d1d5db;
    border-radius: 8px;
    padding: 7px 10px;
    font-size: 13px;
}
QComboBox:focus { border: 1.5px solid #2563eb; }
QComboBox::drop-down {
    border: none;
    width: 24px;
}
QComboBox::down-arrow {
    image: none;
    width: 0; height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid #6b7280;
}
QComboBox QAbstractItemView {
    background-color: #ffffff;
    color: #1a1a2e;
    border: 1px solid #d1d5db;
    border-radius: 6px;
    padding: 4px;
    selection-background-color: #eff6ff;
    selection-color: #2563eb;
    outline: 0;
}

QSpinBox, QDoubleSpinBox {
    background-color: #ffffff;
    color: #1a1a2e;
    border: 1.5px solid #d1d5db;
    border-radius: 8px;
    padding: 7px 10px;
    font-size: 13px;
}
QSpinBox:focus { border: 1.5px solid #2563eb; }

QTextEdit, QPlainTextEdit {
    background-color: #ffffff;
    color: #1a1a2e;
    border: 1px solid #e5e7eb;
    border-radius: 8px;
    padding: 8px 12px;
    font-size: 13px;
    selection-background-color: #2563eb;
    selection-color: #ffffff;
}

QPushButton {
    background-color: #ffffff;
    color: #1a1a2e;
    border: 1.5px solid #d1d5db;
    border-radius: 8px;
    padding: 8px 14px;
    font-size: 13px;
    font-weight: 500;
}
QPushButton:hover {
    background-color: #f9fafb;
    border-color: #9ca3af;
}
QPushButton:pressed { background-color: #f3f4f6; }
QPushButton:disabled {
    color: #d1d5db;
    background-color: #f9fafb;
    border-color: #e5e7eb;
}
QPushButton:default {
    background-color: #2563eb;
    border-color: #2563eb;
    color: #ffffff;
}
QPushButton:default:hover {
    background-color: #1d4ed8;
    border-color: #1d4ed8;
}

QListWidget, QTreeWidget, QTableWidget {
    background-color: #ffffff;
    color: #1a1a2e;
    border: 1px solid #e5e7eb;
    border-radius: 8px;
    outline: 0;
}
QListWidget::item {
    padding: 6px 10px;
    border-bottom: 1px solid #f3f4f6;
}
QListWidget::item:hover { background-color: #f9fafb; }
QListWidget::item:selected {
    background-color: #eff6ff;
    color: #1a1a2e;
}

QHeaderView::section {
    background-color: #f9fafb;
    color: #374151;
    padding: 8px 10px;
    border: none;
    border-bottom: 1px solid #e5e7eb;
    font-weight: 600;
    font-size: 12px;
}

QProgressBar {
    background-color: #e5e7eb;
    border: none;
    border-radius: 8px;
    text-align: center;
    color: #1a1a2e;
    min-height: 16px;
}
QProgressBar::chunk {
    background-color: #2563eb;
    border-radius: 8px;
}

QTabWidget::pane {
    border: 1px solid #e5e7eb;
    border-radius: 8px;
    background-color: #ffffff;
}
QTabBar::tab {
    background-color: transparent;
    color: #6b7280;
    padding: 8px 14px;
    border: none;
    border-bottom: 2px solid transparent;
    font-size: 12.5px;
    font-weight: 500;
}
QTabBar::tab:hover {
    color: #374151;
}
QTabBar::tab:selected {
    color: #2563eb;
    border-bottom-color: #2563eb;
    font-weight: 600;
}

QSplitter::handle {
    background-color: #e5e7eb;
}
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QSplitter::handle:hover { background-color: #2563eb; }

QScrollArea { background-color: transparent; border: none; }
QToolTip {
    background-color: #1a1a2e;
    color: #ffffff;
    border: none;
    border-radius: 6px;
    padding: 6px 10px;
    font-size: 12px;
}

QGroupBox {
    border: 1px solid #e5e7eb;
    border-radius: 8px;
    margin-top: 14px;
    padding: 16px 12px 12px 12px;
    background-color: #ffffff;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 12px;
    padding: 0 6px;
    background-color: #ffffff;
    color: #374151;
}

QCheckBox, QRadioButton { color: #1a1a2e; spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 18px;
    height: 18px;
    border: 2px solid #d1d5db;
    background: #ffffff;
    border-radius: 4px;
}
QRadioButton::indicator { border-radius: 9px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: #2563eb; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background: #2563eb;
    border-color: #2563eb;
}

QScrollBar:vertical {
    background: transparent;
    width: 8px;
    margin: 0;
}
QScrollBar:horizontal {
    background: transparent;
    height: 8px;
    margin: 0;
}
QScrollBar::handle {
    background: #d1d5db;
    border-radius: 4px;
    min-height: 32px;
    min-width: 32px;
}
QScrollBar::handle:hover { background: #9ca3af; }
QScrollBar::handle:pressed { background: #6b7280; }
QScrollBar::add-line, QScrollBar::sub-line,
QScrollBar::add-page, QScrollBar::sub-page {
    background: none;
    height: 0;
    width: 0;
}

QDialog { background-color: #f0f2f5; }
QDialogButtonBox QPushButton { min-width: 80px; }
)";

// ============================================================
// DARK THEME
// ============================================================
const char* kDarkQss = R"(/* ===== Global ===== */
* {
    font-family: 'Inter', 'Segoe UI Variable Text', 'Segoe UI', 'Roboto', sans-serif;
    font-size: 13px;
    outline: 0;
}

QMainWindow, QWidget#centralWidget {
    background-color: #0f172a;
    color: #f1f5f9;
}

QWidget {
    color: #f1f5f9;
}
)" R"(/* ===== Title bar ===== */
QWidget#titleBar {
    background-color: #1e293b;
    border-bottom: 1px solid #334155;
}
QLabel#titleBarText {
    font-size: 14px;
    font-weight: 600;
    color: #f1f5f9;
    background: transparent;
}
QLabel#titleBarSubtitle {
    font-size: 14px;
    font-weight: 400;
    color: #94a3b8;
    background: transparent;
}
QLabel#appLogo {
    background-color: #3b82f6;
    color: #ffffff;
    border-radius: 8px;
    font-size: 14px;
    font-weight: 700;
    qproperty-alignment: AlignCenter;
}
QPushButton#titleBtn {
    background: transparent;
    border: none;
    border-radius: 6px;
    color: #94a3b8;
    padding: 6px;
}
QPushButton#titleBtn:hover {
    background-color: #334155;
}
QPushButton#titleBtn#closeBtn:hover {
    background-color: #7f1d1d;
    color: #fef2f2;
}
)" R"(/* ===== Sidebar ===== */
QWidget#sidebar {
    background-color: #1e293b;
    border-right: 1px solid #334155;
}
QListWidget#sidebar {
    background-color: #1e293b;
    border: none;
    outline: 0;
    font-size: 13.5px;
    padding: 8px 0;
}
QListWidget#sidebar::item {
    padding: 10px 16px;
    border: none;
    border-left: 3px solid transparent;
    color: #94a3b8;
    font-weight: 500;
    margin: 0;
}
QListWidget#sidebar::item:hover {
    background-color: #334155;
    color: #f1f5f9;
}
QListWidget#sidebar::item:selected {
    background-color: #1e3a8a;
    color: #93c5fd;
    border-left-color: #3b82f6;
    font-weight: 600;
}
QWidget#indexedStatus {
    background-color: #1e293b;
    border-top: 1px solid #334155;
}
QLabel#indexedHeader {
    font-size: 13px;
    font-weight: 600;
    color: #10b981;
    background: transparent;
}
QLabel#indexedInfo {
    font-size: 12px;
    color: #94a3b8;
    background: transparent;
}
QProgressBar#indexedBar {
    background-color: #334155;
    border: none;
    border-radius: 2px;
    min-height: 4px;
    max-height: 4px;
    text-align: center;
    color: transparent;
}
QProgressBar#indexedBar::chunk {
    background-color: #10b981;
    border-radius: 2px;
}
)" R"(/* ===== Search bar area ===== */
QWidget#searchBarArea {
    background-color: #1e293b;
    border-bottom: 1px solid #334155;
}
QLineEdit#searchInput {
    background-color: #0f172a;
    color: #f1f5f9;
    border: 1.5px solid #334155;
    border-radius: 8px;
    padding: 9px 36px 9px 36px;
    font-size: 13.5px;
    selection-background-color: #3b82f6;
    selection-color: #ffffff;
}
QLineEdit#searchInput:focus { border: 1.5px solid #3b82f6; }
QLineEdit#searchInput:hover { border-color: #475569; }
QLineEdit#searchInput::placeholder { color: #64748b; }

QPushButton#searchBtn {
    background-color: #3b82f6;
    color: #ffffff;
    border: none;
    border-radius: 8px;
    padding: 9px 18px;
    font-size: 13.5px;
    font-weight: 600;
    text-align: left;
    padding-left: 12px;
}
QPushButton#searchBtn:hover { background-color: #2563eb; }
QPushButton#searchBtn:pressed { background-color: #1d4ed8; }

QPushButton#shortcutBadge {
    background-color: rgba(255,255,255,0.25);
    color: #ffffff;
    border: none;
    border-radius: 4px;
    padding: 2px 6px;
    font-size: 11px;
    font-weight: 500;
    min-width: 32px;
}

QPushButton#toolbarBtn {
    background-color: #1e293b;
    color: #cbd5e1;
    border: 1.5px solid #334155;
    border-radius: 8px;
    padding: 8px 14px;
    font-size: 13px;
    text-align: left;
}
QPushButton#toolbarBtn:hover {
    background-color: #334155;
    border-color: #475569;
}
QPushButton#toolbarBtn:pressed { background-color: #0f172a; }

QPushButton#iconBtn {
    background-color: #1e293b;
    color: #94a3b8;
    border: 1.5px solid #334155;
    border-radius: 8px;
    padding: 6px;
    min-width: 36px;
    min-height: 36px;
    max-width: 36px;
    max-height: 36px;
}
QPushButton#iconBtn:hover {
    background-color: #334155;
    color: #cbd5e1;
}
QPushButton#iconBtn:pressed { background-color: #0f172a; }
)" R"(/* ===== Results panel ===== */
QWidget#resultsPanel {
    background-color: #1e293b;
    border-right: 1px solid #334155;
}
QLabel#resultsTitle {
    font-size: 14px;
    font-weight: 700;
    color: #f1f5f9;
    background: transparent;
}
QLabel#resultsCount {
    font-size: 14px;
    font-weight: 400;
    color: #94a3b8;
    background: transparent;
}
QComboBox#sortSelect {
    background-color: #0f172a;
    color: #cbd5e1;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 4px 8px;
    font-size: 12px;
    min-height: 22px;
}
QComboBox#sortSelect::drop-down {
    border: none;
    width: 18px;
}
QComboBox#sortSelect::down-arrow {
    image: none;
    width: 0; height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid #94a3b8;
}
QComboBox#sortSelect QAbstractItemView {
    background-color: #1e293b;
    color: #f1f5f9;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 4px;
    selection-background-color: #1e3a8a;
    selection-color: #93c5fd;
    outline: 0;
}

QListWidget#resultsList {
    background-color: #1e293b;
    border: none;
    outline: 0;
    padding: 8px;
}
QListWidget#resultsList::item {
    border: 1.5px solid transparent;
    border-radius: 8px;
    margin-bottom: 4px;
    padding: 0;
}
QListWidget#resultsList::item:hover { background-color: #334155; }
QListWidget#resultsList::item:selected {
    background-color: #1e3a8a;
    border-color: #1d4ed8;
}

QLabel#resultTitle {
    font-size: 13px;
    font-weight: 600;
    color: #f1f5f9;
    background: transparent;
}
QLabel#resultSnippet {
    font-size: 11.5px;
    color: #94a3b8;
    background: transparent;
}
QLabel#resultMeta {
    font-size: 11px;
    color: #64748b;
    background: transparent;
}
QLabel#fileIconBadge {
    color: #ffffff;
    font-size: 10px;
    font-weight: 700;
    border-radius: 8px;
    qproperty-alignment: AlignCenter;
}
)" R"(/* ===== Viewer panel ===== */
QWidget#viewerPanel {
    background-color: #0f172a;
}
QWidget#viewerHeader {
    background-color: #1e293b;
    border-bottom: 1px solid #334155;
}
QLabel#viewerTitle {
    font-size: 14px;
    font-weight: 600;
    color: #f1f5f9;
    background: transparent;
}

QPushButton#pageBtn {
    background-color: #1e293b;
    color: #cbd5e1;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 0;
    min-width: 28px;
    min-height: 28px;
    max-width: 28px;
    max-height: 28px;
    font-size: 12px;
}
QPushButton#pageBtn:hover { background-color: #334155; }
QPushButton#pageBtn:pressed { background-color: #0f172a; }
QPushButton#pageBtn:disabled { color: #475569; border-color: #334155; }

QLineEdit#pageInput {
    background-color: #0f172a;
    color: #f1f5f9;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 3px 4px;
    font-size: 13px;
    min-width: 32px;
    max-width: 32px;
    qproperty-alignment: AlignCenter;
}

QLabel#pageTotal {
    font-size: 13px;
    color: #cbd5e1;
    background: transparent;
    padding: 0 4px;
}

QPushButton#zoomBtn {
    background-color: #1e293b;
    color: #cbd5e1;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 0;
    min-width: 28px;
    min-height: 28px;
    max-width: 28px;
    max-height: 28px;
    font-size: 14px;
}
QPushButton#zoomBtn:hover { background-color: #334155; }
QPushButton#zoomBtn:pressed { background-color: #0f172a; }

QLabel#zoomLevel {
    font-size: 13px;
    color: #cbd5e1;
    background: transparent;
    min-width: 44px;
    qproperty-alignment: AlignCenter;
}

QTextEdit#documentPage {
    background-color: #1e293b;
    color: #f1f5f9;
    border: none;
    border-radius: 4px;
    padding: 40px 50px;
    font-size: 13px;
    selection-background-color: #facc15;
    selection-color: #1a1a2e;
}

QLabel#thumb {
    background-color: #1e293b;
    border: 2px solid #334155;
    border-radius: 4px;
    color: #64748b;
    font-size: 11px;
    qproperty-alignment: AlignCenter;
}
QLabel#thumbActive {
    background-color: #1e293b;
    border: 2px solid #3b82f6;
    border-radius: 4px;
    color: #93c5fd;
    font-size: 11px;
    font-weight: 600;
    qproperty-alignment: AlignCenter;
}
QLabel#thumbNum {
    font-size: 11px;
    color: #94a3b8;
    font-weight: 600;
    background: transparent;
    qproperty-alignment: AlignCenter;
}
)" R"(/* ===== Extracted text panel ===== */
QWidget#extractedPanel {
    background-color: #1e293b;
    border-top: 1px solid #334155;
}
QPushButton#extractedTab {
    background: transparent;
    color: #94a3b8;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 10px 14px;
    font-size: 12.5px;
    font-weight: 500;
    text-align: left;
}
QPushButton#extractedTab:hover { color: #cbd5e1; }
QPushButton#extractedTab:checked {
    color: #93c5fd;
    border-bottom-color: #3b82f6;
    font-weight: 600;
}
QTextEdit#extractedContent {
    background-color: #1e293b;
    color: #cbd5e1;
    border: none;
    padding: 12px 16px;
    font-size: 12.5px;
    selection-background-color: #facc15;
    selection-color: #1a1a2e;
}
QPushButton#extractedActionBtn {
    background-color: #1e293b;
    color: #94a3b8;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 0;
    min-width: 28px;
    min-height: 28px;
    max-width: 28px;
    max-height: 28px;
    font-size: 12px;
}
QPushButton#extractedActionBtn:hover { background-color: #334155; }
)" R"(/* ===== Metadata panel ===== */
QWidget#metadataPanel {
    background-color: #1e293b;
    border-left: 1px solid #334155;
}
QLabel#metadataTitle {
    font-size: 14px;
    font-weight: 700;
    color: #f1f5f9;
    background: transparent;
}
QLabel#infoIcon {
    color: #64748b;
    background: transparent;
    border: 1.5px solid #64748b;
    border-radius: 9px;
    font-size: 11px;
    qproperty-alignment: AlignCenter;
    min-width: 18px;
    min-height: 18px;
    max-width: 18px;
    max-height: 18px;
}
QPushButton#editBtn {
    background: transparent;
    border: none;
    border-radius: 6px;
    color: #94a3b8;
    padding: 4px;
    min-width: 28px;
    min-height: 28px;
    max-width: 28px;
    max-height: 28px;
}
QPushButton#editBtn:hover { background-color: #334155; }

QWidget#metadataSection {
    background-color: #1e293b;
    border-bottom: 1px solid #0f172a;
}
QLabel#metaLabel {
    font-size: 12.5px;
    color: #94a3b8;
    background: transparent;
    min-width: 80px;
}
QLabel#metaValue {
    font-size: 12.5px;
    color: #f1f5f9;
    font-weight: 500;
    background: transparent;
}
QLabel#metaIconLabel {
    background: transparent;
}

QWidget#tagsSection {
    background-color: #1e293b;
    border-bottom: 1px solid #0f172a;
}
QLabel#tagsHeader {
    font-size: 14px;
    font-weight: 700;
    color: #f1f5f9;
    background: transparent;
}
QPushButton#tagBlue {
    background-color: #1e3a8a;
    color: #93c5fd;
    border: none;
    border-radius: 12px;
    padding: 4px 12px;
    font-size: 12px;
    font-weight: 500;
}
QPushButton#tagGreen {
    background-color: #14532d;
    color: #86efac;
    border: none;
    border-radius: 12px;
    padding: 4px 12px;
    font-size: 12px;
    font-weight: 500;
}
QPushButton#tagYellow {
    background-color: #713f12;
    color: #fde047;
    border: none;
    border-radius: 12px;
    padding: 4px 12px;
    font-size: 12px;
    font-weight: 500;
}
QPushButton#tagPurple {
    background-color: #581c87;
    color: #d8b4fe;
    border: none;
    border-radius: 12px;
    padding: 4px 12px;
    font-size: 12px;
    font-weight: 500;
}
QPushButton#addTagBtn {
    background: transparent;
    color: #94a3b8;
    border: 1.5px dashed #475569;
    border-radius: 12px;
    padding: 4px 12px;
    font-size: 12px;
}
QPushButton#addTagBtn:hover {
    border-color: #64748b;
    color: #cbd5e1;
}

QWidget#notesSection {
    background-color: #1e293b;
}
QLabel#notesHeader {
    font-size: 14px;
    font-weight: 700;
    color: #f1f5f9;
    background: transparent;
}
QTextEdit#notesContent {
    background-color: #0f172a;
    color: #cbd5e1;
    border: 1px solid #334155;
    border-radius: 8px;
    padding: 10px 12px;
    font-size: 12.5px;
}
QLabel#notesModified {
    font-size: 11px;
    color: #64748b;
    background: transparent;
}
)" R"(/* ===== Status bar ===== */
QStatusBar {
    background-color: #1e293b;
    color: #94a3b8;
    border-top: 1px solid #334155;
    font-size: 12px;
    padding: 0 16px;
}
QStatusBar::item { border: none; }
QLabel#statusReady {
    color: #94a3b8;
    font-size: 12px;
    background: transparent;
}
QLabel#statusDot {
    background-color: #10b981;
    border-radius: 4px;
    min-width: 8px;
    min-height: 8px;
    max-width: 8px;
    max-height: 8px;
}
QLabel#statusInfo {
    color: #94a3b8;
    font-size: 12px;
    background: transparent;
}
QPushButton#openLocationBtn {
    background-color: #1e293b;
    color: #cbd5e1;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 4px 12px;
    font-size: 12px;
}
QPushButton#openLocationBtn:hover { background-color: #334155; }
)" R"(/* ===== Generic widgets ===== */
QLineEdit {
    background-color: #0f172a;
    color: #f1f5f9;
    border: 1.5px solid #334155;
    border-radius: 8px;
    padding: 8px 12px;
    font-size: 13px;
    selection-background-color: #3b82f6;
    selection-color: #ffffff;
}
QLineEdit:focus { border: 1.5px solid #3b82f6; }
QLineEdit:hover { border-color: #475569; }
QLineEdit::placeholder { color: #64748b; }

QComboBox {
    background-color: #0f172a;
    color: #f1f5f9;
    border: 1.5px solid #334155;
    border-radius: 8px;
    padding: 7px 10px;
    font-size: 13px;
}
QComboBox:focus { border: 1.5px solid #3b82f6; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow {
    image: none;
    width: 0; height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid #94a3b8;
}
QComboBox QAbstractItemView {
    background-color: #1e293b;
    color: #f1f5f9;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 4px;
    selection-background-color: #1e3a8a;
    selection-color: #93c5fd;
    outline: 0;
}

QSpinBox, QDoubleSpinBox {
    background-color: #0f172a;
    color: #f1f5f9;
    border: 1.5px solid #334155;
    border-radius: 8px;
    padding: 7px 10px;
    font-size: 13px;
}
QSpinBox:focus { border: 1.5px solid #3b82f6; }

QTextEdit, QPlainTextEdit {
    background-color: #0f172a;
    color: #f1f5f9;
    border: 1px solid #334155;
    border-radius: 8px;
    padding: 8px 12px;
    font-size: 13px;
    selection-background-color: #3b82f6;
    selection-color: #ffffff;
}

QPushButton {
    background-color: #1e293b;
    color: #f1f5f9;
    border: 1.5px solid #334155;
    border-radius: 8px;
    padding: 8px 14px;
    font-size: 13px;
    font-weight: 500;
}
QPushButton:hover { background-color: #334155; border-color: #475569; }
QPushButton:pressed { background-color: #0f172a; }
QPushButton:disabled {
    color: #475569;
    background-color: #1e293b;
    border-color: #334155;
}
QPushButton:default {
    background-color: #3b82f6;
    border-color: #3b82f6;
    color: #ffffff;
}
QPushButton:default:hover { background-color: #2563eb; }

QListWidget, QTreeWidget, QTableWidget {
    background-color: #1e293b;
    color: #f1f5f9;
    border: 1px solid #334155;
    border-radius: 8px;
    outline: 0;
}
QListWidget::item {
    padding: 6px 10px;
    border-bottom: 1px solid #0f172a;
}
QListWidget::item:hover { background-color: #334155; }
QListWidget::item:selected {
    background-color: #1e3a8a;
    color: #f1f5f9;
}

QHeaderView::section {
    background-color: #0f172a;
    color: #cbd5e1;
    padding: 8px 10px;
    border: none;
    border-bottom: 1px solid #334155;
    font-weight: 600;
    font-size: 12px;
}

QProgressBar {
    background-color: #334155;
    border: none;
    border-radius: 8px;
    text-align: center;
    color: #f1f5f9;
    min-height: 16px;
}
QProgressBar::chunk {
    background-color: #3b82f6;
    border-radius: 8px;
}

QTabWidget::pane {
    border: 1px solid #334155;
    border-radius: 8px;
    background-color: #1e293b;
}
QTabBar::tab {
    background-color: transparent;
    color: #94a3b8;
    padding: 8px 14px;
    border: none;
    border-bottom: 2px solid transparent;
    font-size: 12.5px;
    font-weight: 500;
}
QTabBar::tab:hover { color: #cbd5e1; }
QTabBar::tab:selected {
    color: #93c5fd;
    border-bottom-color: #3b82f6;
    font-weight: 600;
}

QSplitter::handle { background-color: #334155; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QSplitter::handle:hover { background-color: #3b82f6; }

QScrollArea { background-color: transparent; border: none; }
QToolTip {
    background-color: #0f172a;
    color: #f1f5f9;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 6px 10px;
    font-size: 12px;
}

QGroupBox {
    border: 1px solid #334155;
    border-radius: 8px;
    margin-top: 14px;
    padding: 16px 12px 12px 12px;
    background-color: #1e293b;
    color: #f1f5f9;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 12px;
    padding: 0 6px;
    background-color: #1e293b;
    color: #cbd5e1;
}

QCheckBox, QRadioButton { color: #f1f5f9; spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 18px;
    height: 18px;
    border: 2px solid #475569;
    background: #0f172a;
    border-radius: 4px;
}
QRadioButton::indicator { border-radius: 9px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: #3b82f6; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background: #3b82f6;
    border-color: #3b82f6;
}

QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 8px; margin: 0; }
QScrollBar::handle {
    background: #475569;
    border-radius: 4px;
    min-height: 32px;
    min-width: 32px;
}
QScrollBar::handle:hover { background: #64748b; }
QScrollBar::handle:pressed { background: #94a3b8; }
QScrollBar::add-line, QScrollBar::sub-line,
QScrollBar::add-page, QScrollBar::sub-page {
    background: none;
    height: 0;
    width: 0;
}

QDialog { background-color: #0f172a; }
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
    return mode == Mode::Dark ? QColor("#3b82f6") : QColor("#2563eb");
}

} // namespace DocuSearch
