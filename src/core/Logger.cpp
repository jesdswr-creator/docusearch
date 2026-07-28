// ============================================================
// Logger.cpp - Async file+UI logger implementation
// ============================================================

#include "Logger.h"

#include <QDir>
#include <QStandardPaths>
#include <QMetaObject>
#include <QCoreApplication>
#include <QThread>
#include <cstdio>

namespace DocuSearch {

// Register metatype early so cross-thread signal delivery works.
static const int kLogEntryMetaId = qRegisterMetaType<DocuSearch::LogEntry>("DocuSearch::LogEntry");

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

Logger::Logger() {
    qRegisterMetaType<LogEntry>("LogEntry");
    qRegisterMetaType<LogLevel>("LogLevel");
}

Logger::~Logger() {
    shutdown();
}

void Logger::init(const QString& logDir, LogLevel minLevel, bool mirrorToStderr) {
    minLevel_.store(minLevel);
    mirrorToStderr_ = mirrorToStderr;

    QDir().mkpath(logDir);
    const QString fileName =
        QString("%1/docusearch_%2.log")
            .arg(logDir)
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd"));

    file_ = std::make_unique<QFile>(fileName);
    if (file_->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        stream_ = std::make_unique<QTextStream>(file_.get());
        stream_->setEncoding(QStringConverter::Utf8);
    }

    running_.store(true);
    // CRITICAL: workerLoop() is an infinite for(;;) loop. It must run on
    // the workerThread_, NOT the main thread. The previous code used
    // QMetaObject::invokeMethod(this, ..., QueuedConnection) which queued
    // the call to `this`'s thread (the main thread) — blocking the main
    // thread's event loop forever, causing a black window.
    //
    // Correct pattern: connect QThread::started → workerLoop. When the
    // thread starts, it invokes workerLoop directly on the worker thread
    // (not via the main thread's event loop). workerLoop blocks on the
    // queue condition variable, releasing the worker thread's CPU until
    // a log entry arrives.
    //
    // We use a DirectConnection because the connection is established
    // while `this` still lives on the main thread, but the signal fires
    // from the worker thread — DirectConnection runs the slot on the
    // thread that emits the signal (the worker thread). This is what we
    // want.
    connect(&workerThread_, &QThread::started, this, &Logger::workerLoop,
            Qt::DirectConnection);
    workerThread_.start();

    info("Logger", QString("Logging started - file: %1").arg(fileName));
}

void Logger::shutdown() {
    if (!running_.exchange(false)) return;
    stopping_.store(true);
    {
        QMutexLocker lk(&queueMutex_);
        queueCond_.wakeAll();
    }
    workerThread_.quit();
    workerThread_.wait(2000);
    if (stream_) stream_->flush();
    if (file_)   file_->close();
}

void Logger::setMinLevel(LogLevel level) { minLevel_.store(level); }

void Logger::flush() {
    // Wake the worker thread so it processes the queue.
    {
        QMutexLocker lk(&queueMutex_);
        queueCond_.wakeAll();
    }
    // Wait for the queue to drain (up to 2 seconds).
    for (int i = 0; i < 200; ++i) {
        bool empty;
        {
            QMutexLocker lk(&queueMutex_);
            empty = queue_.isEmpty();
        }
        if (empty) break;
        QThread::msleep(10);
    }
    // Final stream flush.
    if (stream_) stream_->flush();
}

void Logger::log(LogLevel level, const QString& category, const QString& message) {
    if (static_cast<int>(level) < static_cast<int>(minLevel_.load())) return;

    LogEntry e;
    e.timestamp = QDateTime::currentDateTime();
    e.level     = level;
    e.category  = category;
    e.message   = message;
    e.threadHint= QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));

    {
        QMutexLocker lk(&queueMutex_);
        queue_.enqueue(e);
        queueCond_.wakeOne();
    }
}

QString Logger::levelToString(LogLevel l) {
    switch (l) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO ";
    case LogLevel::Warn:  return "WARN ";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Fatal: return "FATAL";
    }
    return "?    ";
}

void Logger::workerLoop() {
    QThread::currentThread()->setObjectName("LoggerWorker");
    for (;;) {
        LogEntry e;
        {
            QMutexLocker lk(&queueMutex_);
            while (queue_.isEmpty() && !stopping_.load())
                queueCond_.wait(&queueMutex_);
            if (queue_.isEmpty() && stopping_.load()) return;
            e = queue_.dequeue();
        }

        const QString line = QString("[%1] [%2] [%3] %4")
            .arg(e.timestamp.toString("yyyy-MM-dd hh:mm:ss.zzz"))
            .arg(levelToString(e.level))
            .arg(e.category)
            .arg(e.message);

        if (stream_) {
            (*stream_) << line << '\n';
            stream_->flush();
        }
        if (mirrorToStderr_) {
            std::fputs(line.toLocal8Bit().constData(), stderr);
            std::fputc('\n', stderr);
        }

        // Emit to UI subscribers (queued automatically across threads)
        emit logEntry(e);
    }
}

} // namespace DocuSearch
