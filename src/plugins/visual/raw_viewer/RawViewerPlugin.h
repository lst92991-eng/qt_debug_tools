#pragma once
#include "sdk/IVisualPlugin.h"
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QTimer>
#include <QToolButton>
#include <deque>
struct RawPacketEntry {
    qint64 timestamp_us = 0;
    FrameDirection direction = FrameDirection::Receive;
    QByteArray payload;
    QVariantMap attributes;
    QString line;
};
class RawPacketModel;
class RawPacketFilter;
class RawViewerPlugin : public IVisualPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IVisualPlugin_iid FILE "raw_viewer.json")
    Q_INTERFACES(IVisualPlugin)
public:
    explicit RawViewerPlugin(QWidget* parent = nullptr);
    void onChannelData(const DataFrame& frame) override;
    QList<ChannelId> subscribedChannels() override;
    qint64 historyFrom() override;
    QString name() const override;
    void clearHistory() override;
    void clearNumericHistory() override {} // Loading numeric history must not erase packet evidence.
private:
    void buildUi();
    void flushPending();
    void updateDetailPane();
    void updateFilter();
    void updateStatistics();
    void exportLog();
    QString formatLine(const RawPacketEntry& entry) const;
    QString formatHex(const QByteArray& payload, int maxBytes = -1) const;
    QString formatAscii(const QByteArray& payload, int maxBytes = -1) const;
    QString formatHexDump(const QByteArray& payload) const;
    RawPacketModel* m_model = nullptr;
    RawPacketFilter* m_proxy = nullptr;
    QCheckBox* m_autoScroll = nullptr;
    QToolButton* m_pauseButton = nullptr;
    QLineEdit* m_filter = nullptr;
    QListView* m_log = nullptr;
    QPlainTextEdit* m_detail = nullptr;
    QLabel* m_statistics = nullptr;
    QTimer m_flushTimer;
    std::deque<RawPacketEntry> m_pending;
    qsizetype m_pendingBytes = 0;
    quint64 m_dropped = 0;
};
