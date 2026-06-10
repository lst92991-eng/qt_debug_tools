#pragma once

#include "sdk/IVisualPlugin.h"

#include <QCheckBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVector>

class RawViewerPlugin : public IVisualPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IVisualPlugin_iid FILE "raw_viewer.json")
    Q_INTERFACES(IVisualPlugin)

public:
    explicit RawViewerPlugin(QWidget* parent = nullptr);

    void onChannelData(const DataFrame& frame) override;
    QList<quint16> subscribedChannels() override;
    qint64 historyFrom() override;
    QString name() const override;

private:
    struct PacketEntry {
        qint64 timestamp_us = 0;
        FrameDirection direction = FrameDirection::Receive;
        QByteArray payload;
    };

    void buildUi();
    void appendEntry(const PacketEntry& entry);
    void flushPending();
    void rebuildVisibleLog();
    void updateDetailPane();
    bool matchesFilter(const QByteArray& payload) const;
    QString formatLine(const PacketEntry& entry) const;
    QString formatTimestamp(qint64 timestamp_us) const;
    QString formatHex(const QByteArray& payload, int maxBytes = -1) const;
    QString formatAscii(const QByteArray& payload, int maxBytes = -1) const;
    QString formatHexDump(const QByteArray& payload) const;
    QByteArray filterBytes() const;
    void trimDisplayedIndexes();

    QPlainTextEdit* m_log = nullptr;
    QPlainTextEdit* m_detail = nullptr;
    QCheckBox* m_autoScroll = nullptr;
    QToolButton* m_pauseButton = nullptr;
    QLineEdit* m_filter = nullptr;
    QTimer m_flushTimer;
    QVector<PacketEntry> m_entries;
    QVector<PacketEntry> m_pending;
    QVector<int> m_displayedEntryIndexes;
    int m_maxEntries = 50000;
};
