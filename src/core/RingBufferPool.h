#pragma once
#include "sdk/DataFrame.h"
#include <QHash>
#include <QList>
#include <QReadWriteLock>
#include <QString>
#include <QVector>
struct TimedSample { qint64 timestamp_us = 0; double value = 0.0; };
struct RingBuffer {
    QVector<TimedSample> data;
    int head = 0;
    int capacity = 20000;
    qint64 oldest_ts = 0;
    int sampleCount = 0;
    void push(TimedSample sample);
    QVector<TimedSample> range(qint64 from_us, qint64 to_us) const;
    qint64 newestTimestamp() const;
};
class RingBufferPool {
public:
    static constexpr int MaxChannels = 1024;
    static constexpr int MaxTotalSamples = 1000000;
    static constexpr int MaxCapacity = 1000000;
    explicit RingBufferPool(int defaultCapacity = 20000);
    bool push(ChannelId channelIdx, TimedSample sample);
    QVector<TimedSample> replay(ChannelId channelIdx, qint64 from_us) const;
    qint64 newestTimestamp(ChannelId channelIdx) const;
    QList<ChannelId> activeChannels() const;
    bool saveToFile(const QString& path, QString* errorMessage = nullptr) const;
    bool loadFromFile(const QString& path, QString* errorMessage = nullptr);
    void clear();
private:
    void rebalance(); // Caller holds the write lock; discard oldest samples, not new input.
    int m_defaultCapacity;
    int m_totalSamples = 0;
    QHash<ChannelId, RingBuffer> m_buffers;
    mutable QReadWriteLock m_lock;
};
