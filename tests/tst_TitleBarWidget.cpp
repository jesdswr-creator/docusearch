// ============================================================
// tst_TitleBarWidget.cpp — behavioral widget test (v1.7.24)
// ============================================================
//
// The custom frameless-window title bar was extracted from
// MainWindow.cpp's anonymous namespace into src/ui/TitleBarWidget.
// This is the repo's first WIDGET-layer test — it constructs the
// real bar on a real window and drives the two interactions users
// actually perform:
//
//   • the construction contract (fixed 44 px bar, objectName the
//     theme stylesheet targets);
//   • double-click toggles maximize/restore (the native move loop
//     itself is the platform's — startSystemMove — and is exercised
//     by every drag; here we verify the owner wiring it delegates
//     to, without simulating OS-level move loops).

#include "../src/ui/TitleBarWidget.h"

#include <QtTest/QtTest>
#include <QMainWindow>
#include <QWidget>

using namespace DocuSearch;

class tst_TitleBarWidget : public QObject {
    Q_OBJECT

private slots:
    void constructionContract() {
        QMainWindow owner;
        TitleBarWidget bar(&owner);
        // The theme stylesheet targets #titleBar — the objectName is
        // part of the contract, not an implementation detail.
        QCOMPARE(bar.objectName(), QStringLiteral("titleBar"));
        // Fixed 44 px title bar height (frameless layout relies on it).
        QCOMPARE(bar.height(), 44);
    }

    void doubleClickTogglesMaximized() {
        QMainWindow owner;
        TitleBarWidget bar(&owner, &owner);
        owner.setCentralWidget(new QWidget());
        owner.resize(640, 480);
        owner.show();
        QVERIFY(QTest::qWaitForWindowExposed(&owner));
        QVERIFY(!owner.isMaximized());

        // Double-click on the bar maximizes the OWNER (the bar has no
        // logic of its own — that is the point of the extraction).
        QTest::mouseDClick(&bar, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(owner.isMaximized(), 5000);

        // And toggles back to normal.
        QTest::mouseDClick(&bar, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!owner.isMaximized(), 5000);
    }
};

QTEST_MAIN(tst_TitleBarWidget)
#include "tst_TitleBarWidget.moc"
