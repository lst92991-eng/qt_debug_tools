#pragma once
#include <QByteArray>
#include <QMetaObject>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QString>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <utility>

// Owns all blocking USB transfers. GUI calls only enqueue(), requestStop(), stop().
// The transport keeps its native handles alive until stop() has joined this worker.
class UsbIoWorker {
public:
    struct ReadResult { QByteArray bytes; QString error; };
    using Read = std::function<ReadResult()>;
    using Write = std::function<QString(const QByteArray&, const std::atomic_bool&)>;
    explicit UsbIoWorker(QObject* owner) : m_owner(owner) {}
    ~UsbIoWorker() { stop(); }
    UsbIoWorker(const UsbIoWorker&) = delete;
    UsbIoWorker& operator=(const UsbIoWorker&) = delete;
    bool running() const { return m_running.load(); }
    qint64 enqueue(const QByteArray& bytes)
    {
        if (bytes.isEmpty()) return 0;
        QMutexLocker lock(&m_mutex);
        if (!m_running || bytes.size() > MaxCommandBytes || m_tx.size() >= 1024
            || m_queuedBytes + m_inflightBytes + bytes.size() > MaxQueueBytes) return -1;
        m_tx.push_back(bytes);
        m_queuedBytes += bytes.size();
        return bytes.size();
    }
    void start(Read read, Write write, std::function<void(const QByteArray&)> receive,
               std::function<void(const QString&)> error, std::function<void()> stopped)
    {
        stop();
        m_running = true;
        m_state = std::make_shared<State>();
        const auto state = m_state;
        m_thread = QThread::create([this, state, read, write, receive, error, stopped]() {
            const auto post = [this, state](std::function<void()> fn) {
                QMetaObject::invokeMethod(m_owner, [state, fn = std::move(fn)]() {
                    if (state->valid) fn();
                }, Qt::QueuedConnection);
            };
            while (m_running) {
                QByteArray command;
                {
                    QMutexLocker lock(&m_mutex);
                    if (!m_tx.empty()) {
                        command = std::move(m_tx.front());
                        m_tx.pop_front();
                        m_queuedBytes -= command.size();
                        m_inflightBytes = command.size();
                    }
                }
                if (!command.isEmpty()) {
                    const QString message = write(command, m_running);
                    { QMutexLocker lock(&m_mutex); m_inflightBytes = 0; }
                    if (!message.isEmpty()) { post([error, message]() { error(message); }); break; }
                }
                if (!m_running) break;
                const ReadResult result = read();
                if (!result.bytes.isEmpty()) {
                    const qsizetype size = result.bytes.size();
                    const qsizetype old = state->rxBytes.fetch_add(size);
                    if (old + size > MaxQueueBytes) {
                        state->rxBytes.fetch_sub(size);
                        post([error]() { error(QStringLiteral("USB RX queue overflow; connection stopped to avoid silent data loss")); });
                        break;
                    }
                    QMetaObject::invokeMethod(m_owner, [state, receive, bytes = result.bytes]() {
                        state->rxBytes.fetch_sub(bytes.size());
                        if (state->valid) receive(bytes);
                    }, Qt::QueuedConnection);
                }
                if (!result.error.isEmpty()) { post([error, message = result.error]() { error(message); }); break; }
                if (result.bytes.isEmpty()) QThread::msleep(1);
            }
            m_running = false;
            { QMutexLocker lock(&m_mutex); m_tx.clear(); m_queuedBytes = 0; m_inflightBytes = 0; }
            post(stopped);
        });
        m_thread->setObjectName(QStringLiteral("usb-io-worker"));
        m_thread->start();
    }
    void requestStop()
    {
        if (m_state) m_state->valid = false;
        m_running = false;
    }
    void stop()
    {
        requestStop();
        if (m_thread) { m_thread->wait(); delete m_thread; m_thread = nullptr; }
        QMutexLocker lock(&m_mutex);
        m_tx.clear();
        m_queuedBytes = 0;
        m_inflightBytes = 0;
    }
    static constexpr qsizetype MaxCommandBytes = 64 * 1024;
    static constexpr qsizetype MaxQueueBytes = 1024 * 1024;
private:
    struct State { std::atomic_bool valid{true}; std::atomic<qsizetype> rxBytes{0}; };
    QObject* m_owner;
    QThread* m_thread = nullptr;
    std::atomic_bool m_running{false};
    std::shared_ptr<State> m_state;
    QMutex m_mutex;
    std::deque<QByteArray> m_tx;
    qsizetype m_queuedBytes = 0;
    qsizetype m_inflightBytes = 0;
};
