// ============================================================
// Logger.cpp — Async file+UI logger implementation
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
    // Move ownership of the workerLoop into a dedicated QThread
    workerThread_.start();
    QMetaObject::invokeMethod(this, [this]{ this->workerLoop(); }, Qt::QueuedConnection);

    info("Logger", QString("Logging started — file: %1").arg(fileName));
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
