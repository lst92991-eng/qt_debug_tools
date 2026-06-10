#include "sdk/DataFrame.h"

#include <QMetaType>

#include <chrono>

void registerMcuDebugMetaTypes()
{
    qRegisterMetaType<FrameDirection>("FrameDirection");
    qRegisterMetaType<ChannelSample>("ChannelSample");
    qRegisterMetaType<DataFrame>("DataFrame");
    qRegisterMetaType<QVector<ChannelSample>>("QVector<ChannelSample>");
}

qint64 currentTimestampMicros()
{
    using clock = std::chrono::system_clock;
    const auto now = clock::now().time_since_epoch();
    return static_cast<qint64>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}
