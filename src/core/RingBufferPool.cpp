#include "core/RingBufferPool.h"

#include <algorithm>

void RingBuffer::push(TimedSample sample)
{
    if (capacity <= 0) {
        return;
    }

    if (data.size() != capacity) {
        data.resize(capacity);
    }

    data[head] = sample;
    head = (head + 1) % capacity;
    sampleCount = std::min(sampleCount + 1, capacity);

    if (sampleCount == capacity) {
        oldest_ts = data[head].timestamp_us;
    } else if (sampleCount == 1) {
        oldest_ts = sample.timestamp_us;
    }
}

QVector<TimedSample> RingBuffer::range(qint64 from_us, qint64 to_us) const
{
    QVector<TimedSample> result;
    const int count = std::min(sampleCount, data.size());
    result.reserve(count);

    for (int i = 0; i < count; ++i) {
        const int idx = (head - count + i + data.size()) % data.size();
        const TimedSample sample = data[idx];
        if (sample.timestamp_us >= from_us && (to_us <= 0 || sample.timestamp_us <= to_us)) {
            result.push_back(sample);
        }
    }

    return result;
}

qint64 RingBuffer::newestTimestamp() const
{
    const int count = std::min(sampleCount, data.size());
    if (count == 0) {
        return 0;
    }

    const int idx = (head - 1 + data.size()) % data.size();
    return data[idx].timestamp_us;
}

RingBufferPool::RingBufferPool(int defaultCapacity)
    : m_defaultCapacity(defaultCapacity)
{
}

void RingBufferPool::push(quint16 channelIdx, TimedSample sample)
{
    QWriteLocker locker(&m_lock);
    RingBuffer& buffer = m_buffers[channelIdx];
    if (buffer.capacity != m_defaultCapacity && buffer.data.isEmpty()) {
        buffer.capacity = m_defaultCapacity;
    }
    buffer.push(sample);
}

QVector<TimedSample> RingBufferPool::replay(quint16 channelIdx, qint64 from_us) const
{
    QReadLocker locker(&m_lock);
    const auto it = m_buffers.constFind(channelIdx);
    if (it == m_buffers.constEnd()) {
        return {};
    }
    return it->range(from_us, 0);
}

qint64 RingBufferPool::newestTimestamp(quint16 channelIdx) const
{
    QReadLocker locker(&m_lock);
    const auto it = m_buffers.constFind(channelIdx);
    return it == m_buffers.constEnd() ? 0 : it->newestTimestamp();
}

QList<quint16> RingBufferPool::activeChannels() const
{
    QReadLocker locker(&m_lock);
    QList<quint16> keys = m_buffers.keys();
    std::sort(keys.begin(), keys.end());
    return keys;
}
