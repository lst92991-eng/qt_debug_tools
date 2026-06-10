#pragma once

#include <QHash>
#include <QList>
#include <QReadWriteLock>
#include <QVector>

struct TimedSample {
    qint64 timestamp_us = 0;
    double value = 0.0;
};

struct RingBuffer {
    QVector<TimedSample> data;
    int head = 0;
    int capacity = 1'000'000;
    qint64 oldest_ts = 0;
    int sampleCount = 0;

    void push(TimedSample sample);
    QVector<TimedSample> range(qint64 from_us, qint64 to_us) const;
    qint64 newestTimestamp() const;
};

class RingBufferPool {
public:
    explicit RingBufferPool(int defaultCapacity = 1'000'000);

    void push(quint16 channelIdx, TimedSample sample);
    QVector<TimedSample> replay(quint16 channelIdx, qint64 from_us) const;
    qint64 newestTimestamp(quint16 channelIdx) const;
    QList<quint16> activeChannels() const;

private:
    int m_defaultCapacity;
    QHash<quint16, RingBuffer> m_buffers;
    mutable QReadWriteLock m_lock;
};
