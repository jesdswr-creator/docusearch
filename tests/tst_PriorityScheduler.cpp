// ============================================================
// tst_PriorityScheduler.cpp — Unit tests for indexer/PriorityScheduler
// ============================================================
//
// Covers: priority band assignment based on modification date and
//         extension (P1/P2/P3/P4).
//
// Uses the Qt Test framework.
// ============================================================

#include "../src/indexer/PriorityScheduler.h"
#include "../src/core/Constants.h"

#include <QtTest/QtTest>
#include <QDateTime>

using DocuSearch::IndexPriority;
using DocuSearch::PriorityScheduler;
using DocuSearch::Constants::kPriority1Days;
using DocuSearch::Constants::kPriority2Days;

class TestPriorityScheduler : public QObject {
    Q_OBJECT
private slots:

    // ---- P1: recent (≤ 30 days) ------------------------------------------

    void today_isP1() {
        const auto now = QDateTime::currentDateTime();
        QCOMPARE(PriorityScheduler::compute(now, "pdf"),  IndexPriority::P1_Recent30Days);
    }
    void yesterday_isP1() {
        const auto now = QDateTime::currentDateTime();
        const auto y   = now.addDays(-1);
        QCOMPARE(PriorityScheduler::compute(y, "docx"), IndexPriority::P1_Recent30Days);
    }
    void boundary_30days_isP1() {
        const auto now = QDateTime::currentDateTime();
        const auto d   = now.addDays(-kPriority1Days);   // exactly 30 days
        QCOMPARE(PriorityScheduler::compute(d, "txt"), IndexPriority::P1_Recent30Days);
    }

    // ---- P2: last year (31 .. 365 days) ----------------------------------

    void thirtyOneDays_isP2() {
        const auto now = QDateTime::currentDateTime();
        const auto d   = now.addDays(-(kPriority1Days + 1));
        QCOMPARE(PriorityScheduler::compute(d, "pdf"), IndexPriority::P2_LastYear);
    }
    void sixMonths_isP2() {
        const auto now = QDateTime::currentDateTime();
        const auto d   = now.addMonths(-6);
        QCOMPARE(PriorityScheduler::compute(d, "xlsx"), IndexPriority::P2_LastYear);
    }
    void boundary_365days_isP2() {
        const auto now = QDateTime::currentDateTime();
        const auto d   = now.addDays(-kPriority2Days);   // exactly 365 days
        QCOMPARE(PriorityScheduler::compute(d, "pptx"), IndexPriority::P2_LastYear);
    }

    // ---- P3: older (> 365 days) ------------------------------------------

    void oneYearAndADay_isP3() {
        const auto now = QDateTime::currentDateTime();
        const auto d   = now.addDays(-(kPriority2Days + 1));
        QCOMPARE(PriorityScheduler::compute(d, "pdf"), IndexPriority::P3_Older);
    }
    void tenYears_isP3() {
        const auto now = QDateTime::currentDateTime();
        const auto d   = now.addYears(-10);
        QCOMPARE(PriorityScheduler::compute(d, "txt"), IndexPriority::P3_Older);
    }
    void ancientDate_isP3() {
        // 1990-01-01 — clearly very old
        const QDateTime old(QDate(1990, 1, 1), QTime(0, 0, 0));
        QCOMPARE(PriorityScheduler::compute(old, "pdf"), IndexPriority::P3_Older);
    }

    // ---- P4: archives (extension override) -------------------------------

    void zip_isP4_regardlessOfDate() {
        const auto now = QDateTime::currentDateTime();
        QCOMPARE(PriorityScheduler::compute(now, "zip"),  IndexPriority::P4_Archives);
    }
    void rar_isP4() {
        const auto now = QDateTime::currentDateTime();
        QCOMPARE(PriorityScheduler::compute(now, "rar"),  IndexPriority::P4_Archives);
    }
    void sevenZ_isP4() {
        const auto now = QDateTime::currentDateTime();
        QCOMPARE(PriorityScheduler::compute(now, "7z"),   IndexPriority::P4_Archives);
    }
    void tar_isP4() {
        const auto now = QDateTime::currentDateTime();
        QCOMPARE(PriorityScheduler::compute(now, "tar"),  IndexPriority::P4_Archives);
    }
    void gz_isP4() {
        const auto now = QDateTime::currentDateTime();
        QCOMPARE(PriorityScheduler::compute(now, "gz"),   IndexPriority::P4_Archives);
    }
    void bz2_isP4() {
        const auto now = QDateTime::currentDateTime();
        QCOMPARE(PriorityScheduler::compute(now, "bz2"),  IndexPriority::P4_Archives);
    }
    void archiveExtension_caseInsensitive() {
        const auto now = QDateTime::currentDateTime();
        QCOMPARE(PriorityScheduler::compute(now, "ZIP"),  IndexPriority::P4_Archives);
        QCOMPARE(PriorityScheduler::compute(now, "Rar"),  IndexPriority::P4_Archives);
    }
};

QTEST_GUILESS_MAIN(TestPriorityScheduler)
#include "tst_PriorityScheduler.moc"
