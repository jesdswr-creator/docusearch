#pragma once

// ============================================================
// Logger.h — Thread-safe, asynchronous, file + UI sink logger
// ============================================================

#include <QObject>
#include <QString>
#include <QMutex>
#include <QQueue>
#include <QWaitCondition>
#include <QThread>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <atomic>
#include <memory>

namespace DocuSearch {

enum class LogLevel : int {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Fatal = 5
};

} // namespace DocuSearch

Q_DECLARE_METATYPE(DocuSearch::LogLevel)

namespace DocuSearch {

struct LogEntry {
    QDateTime   timestamp;
    LogLevel    level;
    QString     category;     // module name, e.g. "Indexer"
    QString     message;
    QString     threadHint;   // optional
};

class Logger : public QObject {
    Q_OBJECT
public:
    static Logger& instance();

    // Configure before first log call. Safe to call from main thread.
    void init(const QString& logDir, LogLevel minLevel = LogLevel::Info,
              bool mirrorToStderr = true);

    void shutdown();

    // Logging API — thread-safe, non-blocking (enqueues)
    void log(LogLevel level, const QString& category, const QString& message);

    // Convenience
    void trace(const QString& cat, const QString& msg) { log(LogLevel::Trace, cat, msg); }
    void debug(const QString& cat, const QString& msg) { log(LogLevel::Debug, cat, msg); }
    void info (const QString& cat, const QString& msg) { log(LogLevel::Info,  cat, msg); }
    void warn (const QString& cat, const QString& msg) { log(LogLevel::Warn,  cat, msg); }
    void error(const QString& cat, const QString& msg) { log(LogLevel::Error, cat, msg); }
    void fatal(const QString& cat, const QString& msg) { log(LogLevel::Fatal, cat, msg); }

    void setMinLevel(LogLevel level);
    LogLevel minLevel() const { return minLevel_.load(); }

signals:
    // UI can connect to render logs in a panel
    void logEntry(const LogEntry& entry);

private:
    Logger();
    ~Logger() override;
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    void workerLoop();

    QMutex              queueMutex_;
    QWaitCondition      queueCond_;
    QQueue<LogEntry>    queue_;
    std::atomic<bool>   running_{false};
    std::atomic<bool>   stopping_{false};
    std::atomic<LogLevel> minLevel_{LogLevel::Info};

    QThread             workerThread_;
    std::unique_ptr<QFile>       file_;
    std::unique_ptr<QTextStream> stream_;
    bool                mirrorToStderr_ = true;

    static QString levelToString(LogLevel l);
};

// Convenience macros — short to use
#define DS_TRACE(cat, msg) ::DocuSearch::Logger::instance().trace(cat, msg)
#define DS_DEBUG(cat, msg) ::DocuSearch::Logger::instance().debug(cat, msg)
#define DS_INFO(cat, msg)  ::DocuSearch::Logger::instance().info (cat, msg)
#define DS_WARN(cat, msg)  ::DocuSearch::Logger::instance().warn (cat, msg)
#define DS_ERROR(cat, msg) ::DocuSearch::Logger::instance().error(cat, msg)
#define DS_FATAL(cat, msg) ::DocuSearch::Logger::instance().fatal(cat, msg)

} // namespace DocuSearch
