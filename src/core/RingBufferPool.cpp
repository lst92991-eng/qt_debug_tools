#include "core/RingBufferPool.h"
#include <QDataStream>
#include <QFile>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
namespace {
constexpr quint32 Magic = 0x4d434452;
constexpr quint16 Version = 2;
bool fail(QString* error, const QString& message) { if (error) *error = message; return false; }
}
void RingBuffer::push(TimedSample sample)
{
    if (capacity <= 0) return;
    if (data.size() < capacity) {
        data.push_back(sample);
        sampleCount = int(data.size());
        head = sampleCount % capacity;
        oldest_ts = data.first().timestamp_us;
    } else {
        data[head] = sample;
        head = (head + 1) % capacity;
        sampleCount = capacity;
        oldest_ts = data[head].timestamp_us;
    }
}
QVector<TimedSample> RingBuffer::range(qint64 from_us, qint64 to_us) const
{
    QVector<TimedSample> result;
    const int count = std::min(sampleCount, int(data.size()));
    if (count == 0) return result;
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        const int index = (head - count + i + int(data.size())) % int(data.size());
        const auto& sample = data[index];
        if (sample.timestamp_us >= from_us && (to_us <= 0 || sample.timestamp_us <= to_us)) result.push_back(sample);
    }
    return result;
}
qint64 RingBuffer::newestTimestamp() const
{
    if (data.isEmpty()) return 0;
    return data[(head - 1 + data.size()) % data.size()].timestamp_us;
}
RingBufferPool::RingBufferPool(int defaultCapacity)
    : m_defaultCapacity(std::clamp(defaultCapacity, 1, MaxCapacity)) {}
void RingBufferPool::rebalance()
{
    const int fairCapacity = MaxTotalSamples / std::max(1, int(m_buffers.size()));
    m_totalSamples = 0;
    for (auto it = m_buffers.begin(); it != m_buffers.end(); ++it) {
        const int limit = std::min(it->capacity, fairCapacity);
        if (it->capacity > limit) {
            const auto ordered = it->range(std::numeric_limits<qint64>::min(), 0);
            RingBuffer replacement;
            replacement.capacity = limit;
            replacement.data.reserve(std::min(limit, int(ordered.size())));
            for (qsizetype i = std::max<qsizetype>(0, ordered.size() - limit); i < ordered.size(); ++i)
                replacement.push(ordered.at(i));
            *it = std::move(replacement);
        }
        m_totalSamples += it->sampleCount;
    }
}
bool RingBufferPool::push(ChannelId channelIdx, TimedSample sample)
{
    if (!std::isfinite(sample.value) || sample.timestamp_us < 0) return false;
    QWriteLocker locker(&m_lock);
    auto it = m_buffers.find(channelIdx);
    if (it == m_buffers.end()) {
        if (m_buffers.size() >= MaxChannels) return false;
        RingBuffer buffer;
        buffer.capacity = m_defaultCapacity;
        m_buffers.insert(channelIdx, std::move(buffer));
        rebalance();
        it = m_buffers.find(channelIdx);
    }
    if (it->sampleCount < it->capacity && m_totalSamples >= MaxTotalSamples) {
        // A loaded legacy file can have a different allocation. Make it a rolling
        // history again instead of freezing reception when its quota is reached.
        rebalance();
        it = m_buffers.find(channelIdx);
    }
    const bool grows = it->sampleCount < it->capacity;
    if (grows && m_totalSamples >= MaxTotalSamples) return false;
    it->push(sample);
    if (grows) ++m_totalSamples;
    return true;
}
QVector<TimedSample> RingBufferPool::replay(ChannelId channelIdx, qint64 from_us) const
{
    QReadLocker locker(&m_lock);
    const auto it = m_buffers.constFind(channelIdx);
    return it == m_buffers.constEnd() ? QVector<TimedSample>{} : it->range(from_us, 0);
}
qint64 RingBufferPool::newestTimestamp(ChannelId channelIdx) const
{
    QReadLocker locker(&m_lock);
    const auto it = m_buffers.constFind(channelIdx);
    return it == m_buffers.constEnd() ? 0 : it->newestTimestamp();
}
QList<ChannelId> RingBufferPool::activeChannels() const
{
    QReadLocker locker(&m_lock);
    auto keys = m_buffers.keys();
    std::sort(keys.begin(), keys.end());
    return keys;
}
bool RingBufferPool::saveToFile(const QString& path, QString* error) const
{
    if (error) error->clear();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(error, file.errorString());
    QReadLocker locker(&m_lock);
    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_6_0);
    out << Magic << Version << quint32(m_buffers.size());
    auto channels = m_buffers.keys();
    std::sort(channels.begin(), channels.end());
    for (ChannelId channel : channels) {
        const auto it = m_buffers.constFind(channel);
        const auto samples = it->range(std::numeric_limits<qint64>::min(), 0);
        out << quint64(channel) << quint32(it->capacity) << quint32(samples.size());
        for (const auto& sample : samples) out << sample.timestamp_us << sample.value;
    }
    if (out.status() != QDataStream::Ok) return fail(error, QStringLiteral("Failed to write history"));
    if (!file.commit()) return fail(error, file.errorString());
    return true;
}
bool RingBufferPool::loadFromFile(const QString& path, QString* error)
{
    if (error) error->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    constexpr qint64 MaxFileBytes = 10 + qint64(MaxChannels) * 16 + qint64(MaxTotalSamples) * 16;
    if (file.size() < 10 || file.size() > MaxFileBytes) return fail(error, QStringLiteral("History file size exceeds supported limits"));
    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0, channelCount = 0;
    quint16 version = 0;
    in >> magic >> version >> channelCount;
    if (in.status() != QDataStream::Ok || magic != Magic || version < 1 || version > Version || channelCount > MaxChannels)
        return fail(error, QStringLiteral("Unsupported or invalid history header"));
    QHash<ChannelId, RingBuffer> loaded;
    quint64 total = 0;
    for (quint32 i = 0; i < channelCount; ++i) {
        ChannelId channel = 0;
        if (version == 1) { quint16 legacy = 0; in >> legacy; channel = legacy; }
        else { in >> channel; }
        quint32 capacity = 0, count = 0;
        in >> capacity >> count;
        total += count;
        if (in.status() != QDataStream::Ok || capacity == 0 || capacity > MaxCapacity || count > capacity
            || total > MaxTotalSamples || loaded.contains(channel) || quint64(file.bytesAvailable()) < quint64(count) * 16)
            return fail(error, QStringLiteral("Invalid history dimensions, duplicate channel, or truncated data"));
        RingBuffer buffer;
        buffer.capacity = int(capacity);
        buffer.data.reserve(int(count));
        for (quint32 j = 0; j < count; ++j) {
            TimedSample sample;
            in >> sample.timestamp_us >> sample.value;
            if (in.status() != QDataStream::Ok || !std::isfinite(sample.value) || sample.timestamp_us < 0)
                return fail(error, QStringLiteral("Invalid or truncated history sample"));
            buffer.push(sample);
        }
        loaded.insert(channel, std::move(buffer));
    }
    if (in.status() != QDataStream::Ok || !file.atEnd()) return fail(error, QStringLiteral("Unexpected history trailing data"));
    QWriteLocker locker(&m_lock);
    m_buffers = std::move(loaded);
    m_totalSamples = int(total);
    return true;
}
void RingBufferPool::clear()
{
    QWriteLocker locker(&m_lock);
    m_buffers.clear();
    m_totalSamples = 0;
}
