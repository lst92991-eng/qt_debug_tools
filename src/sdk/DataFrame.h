#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QVariantMap>
#include <QVector>

#include <limits>

enum class FrameDirection {
    Receive,
    Transmit
};

struct ChannelSample {
    quint16 index = 0;
    double value = std::numeric_limits<double>::quiet_NaN();
};

struct DataFrame {
    qint64 timestamp_us = 0;
    QVector<ChannelSample> channels;
    QByteArray rawPayload;
    FrameDirection direction = FrameDirection::Receive;
    QVariantMap attributes;
};

Q_DECLARE_METATYPE(FrameDirection)
Q_DECLARE_METATYPE(ChannelSample)
Q_DECLARE_METATYPE(DataFrame)
Q_DECLARE_METATYPE(QVector<ChannelSample>)

void registerMcuDebugMetaTypes();
qint64 currentTimestampMicros();
