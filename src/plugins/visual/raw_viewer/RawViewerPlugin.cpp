#include "RawViewerPlugin.h"

#include <QDateTime>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

class DirectionHighlighter : public QSyntaxHighlighter {
public:
    explicit DirectionHighlighter(QTextDocument* parent)
        : QSyntaxHighlighter(parent)
    {
        m_rx.setForeground(QColor(45, 102, 214));
        m_rx.setFontWeight(QFont::Bold);
        m_tx.setForeground(QColor(24, 145, 77));
        m_tx.setFontWeight(QFont::Bold);
    }

protected:
    void highlightBlock(const QString& text) override
    {
        const int rxPos = text.indexOf(QStringLiteral(" RX "));
        if (rxPos >= 0) {
            setFormat(rxPos + 1, 2, m_rx);
        }
        const int txPos = text.indexOf(QStringLiteral(" TX "));
        if (txPos >= 0) {
            setFormat(txPos + 1, 2, m_tx);
        }
    }

private:
    QTextCharFormat m_rx;
    QTextCharFormat m_tx;
};

RawViewerPlugin::RawViewerPlugin(QWidget* parent)
    : IVisualPlugin(parent)
{
    buildUi();
    connect(&m_flushTimer, &QTimer::timeout, this, &RawViewerPlugin::flushPending);
    m_flushTimer.start(16);
}

void RawViewerPlugin::onChannelData(const DataFrame& frame)
{
    if (frame.rawPayload.isEmpty()) {
        return;
    }

    PacketEntry entry;
    entry.timestamp_us = frame.timestamp_us;
    entry.direction = frame.direction;
    entry.payload = frame.rawPayload;

    if (m_pauseButton->isChecked()) {
        appendEntry(entry);
        return;
    }

    m_pending.push_back(entry);
}

QList<quint16> RawViewerPlugin::subscribedChannels()
{
    return {};
}

qint64 RawViewerPlugin::historyFrom()
{
    return 0;
}

QString RawViewerPlugin::name() const
{
    return QStringLiteral("Raw Viewer");
}

void RawViewerPlugin::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto* toolbar = new QHBoxLayout;
    toolbar->setSpacing(6);

    m_autoScroll = new QCheckBox(tr("AutoScroll"), this);
    m_autoScroll->setChecked(true);

    m_pauseButton = new QToolButton(this);
    m_pauseButton->setText(tr("Pause"));
    m_pauseButton->setCheckable(true);

    auto* clearButton = new QPushButton(tr("Clear"), this);
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter hex pattern"));

    toolbar->addWidget(m_autoScroll);
    toolbar->addWidget(m_pauseButton);
    toolbar->addWidget(clearButton);
    toolbar->addWidget(new QLabel(tr("Filter:"), this));
    toolbar->addWidget(m_filter, 1);
    root->addLayout(toolbar);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    m_log = new QPlainTextEdit(splitter);
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_log->document()->setMaximumBlockCount(m_maxEntries);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new DirectionHighlighter(m_log->document());

    m_detail = new QPlainTextEdit(splitter);
    m_detail->setReadOnly(true);
    m_detail->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_detail->setMaximumHeight(160);
    m_detail->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    splitter->addWidget(m_log);
    splitter->addWidget(m_detail);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    connect(clearButton, &QPushButton::clicked, this, [this]() {
        m_entries.clear();
        m_pending.clear();
        m_displayedEntryIndexes.clear();
        m_log->clear();
        m_detail->clear();
    });
    connect(m_filter, &QLineEdit::textChanged, this, &RawViewerPlugin::rebuildVisibleLog);
    connect(m_log, &QPlainTextEdit::cursorPositionChanged, this, &RawViewerPlugin::updateDetailPane);
}

void RawViewerPlugin::appendEntry(const PacketEntry& entry)
{
    m_entries.push_back(entry);
    if (m_entries.size() > m_maxEntries) {
        const int removeCount = m_entries.size() - m_maxEntries;
        m_entries.remove(0, removeCount);
        for (int& index : m_displayedEntryIndexes) {
            index -= removeCount;
        }
        while (!m_displayedEntryIndexes.isEmpty() && m_displayedEntryIndexes.first() < 0) {
            m_displayedEntryIndexes.removeFirst();
        }
    }
}

void RawViewerPlugin::flushPending()
{
    if (m_pending.isEmpty() || m_pauseButton->isChecked()) {
        return;
    }

    const bool atBottom = m_log->verticalScrollBar()->value() == m_log->verticalScrollBar()->maximum();
    QString batch;
    batch.reserve(m_pending.size() * 96);

    for (const PacketEntry& entry : std::as_const(m_pending)) {
        appendEntry(entry);
        const int entryIndex = m_entries.size() - 1;
        if (matchesFilter(entry.payload)) {
            batch.append(formatLine(entry));
            batch.append(QLatin1Char('\n'));
            m_displayedEntryIndexes.push_back(entryIndex);
        }
    }
    m_pending.clear();

    if (batch.isEmpty()) {
        return;
    }

    QTextCursor cursor = m_log->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_log->setTextCursor(cursor);
    m_log->insertPlainText(batch);
    trimDisplayedIndexes();

    if (m_autoScroll->isChecked() || atBottom) {
        m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
    }
}

void RawViewerPlugin::rebuildVisibleLog()
{
    m_log->clear();
    m_detail->clear();
    m_displayedEntryIndexes.clear();

    QString batch;
    for (int i = 0; i < m_entries.size(); ++i) {
        const PacketEntry& entry = m_entries.at(i);
        if (!matchesFilter(entry.payload)) {
            continue;
        }
        batch.append(formatLine(entry));
        batch.append(QLatin1Char('\n'));
        m_displayedEntryIndexes.push_back(i);
    }

    m_log->setPlainText(batch);
    trimDisplayedIndexes();
    if (m_autoScroll->isChecked()) {
        m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
    }
}

void RawViewerPlugin::updateDetailPane()
{
    const int block = m_log->textCursor().blockNumber();
    if (block < 0 || block >= m_displayedEntryIndexes.size()) {
        m_detail->clear();
        return;
    }

    const int entryIndex = m_displayedEntryIndexes.at(block);
    if (entryIndex < 0 || entryIndex >= m_entries.size()) {
        m_detail->clear();
        return;
    }

    const PacketEntry& entry = m_entries.at(entryIndex);
    m_detail->setPlainText(formatHexDump(entry.payload));
}

bool RawViewerPlugin::matchesFilter(const QByteArray& payload) const
{
    const QByteArray needle = filterBytes();
    if (needle.isEmpty()) {
        return true;
    }
    return payload.contains(needle);
}

QString RawViewerPlugin::formatLine(const PacketEntry& entry) const
{
    const QString direction = entry.direction == FrameDirection::Transmit
        ? QStringLiteral("TX")
        : QStringLiteral("RX");
    const QString hex = formatHex(entry.payload, 32);
    const QString ascii = formatAscii(entry.payload, 32);
    return QStringLiteral("%1  %2  %3  |%4|")
        .arg(formatTimestamp(entry.timestamp_us), direction, hex.leftJustified(98, QLatin1Char(' ')), ascii);
}

QString RawViewerPlugin::formatTimestamp(qint64 timestamp_us) const
{
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(timestamp_us / 1000);
    const int micros = static_cast<int>(timestamp_us % 1000);
    return QStringLiteral("%1%2")
        .arg(dt.toString(QStringLiteral("HH:mm:ss.zzz")))
        .arg(micros, 3, 10, QLatin1Char('0'));
}

QString RawViewerPlugin::formatHex(const QByteArray& payload, int maxBytes) const
{
    const int count = maxBytes < 0 ? payload.size() : std::min(payload.size(), maxBytes);
    QStringList parts;
    parts.reserve(count + 1);
    for (int i = 0; i < count; ++i) {
        parts << QStringLiteral("%1")
                     .arg(static_cast<unsigned char>(payload.at(i)), 2, 16, QLatin1Char('0'))
                     .toUpper();
    }
    if (maxBytes >= 0 && payload.size() > maxBytes) {
        parts << QStringLiteral("...");
    }
    return parts.join(QLatin1Char(' '));
}

QString RawViewerPlugin::formatAscii(const QByteArray& payload, int maxBytes) const
{
    const int count = maxBytes < 0 ? payload.size() : std::min(payload.size(), maxBytes);
    QString ascii;
    ascii.reserve(count + (payload.size() > count ? 3 : 0));
    for (int i = 0; i < count; ++i) {
        const unsigned char c = static_cast<unsigned char>(payload.at(i));
        ascii.append(c >= 32 && c < 127 ? QLatin1Char(static_cast<char>(c)) : QLatin1Char('.'));
    }
    if (maxBytes >= 0 && payload.size() > maxBytes) {
        ascii.append(QStringLiteral("..."));
    }
    return ascii;
}

QString RawViewerPlugin::formatHexDump(const QByteArray& payload) const
{
    QString dump;
    for (int offset = 0; offset < payload.size(); offset += 16) {
        const QByteArray row = payload.mid(offset, 16);
        dump.append(QStringLiteral("%1  %2")
                        .arg(offset, 8, 16, QLatin1Char('0')).toUpper()
                        .arg(formatHex(row, -1).leftJustified(47, QLatin1Char(' '))));
        dump.append(QStringLiteral("  |%1|\n").arg(formatAscii(row, -1)));
    }
    return dump;
}

QByteArray RawViewerPlugin::filterBytes() const
{
    QString compact = m_filter->text();
    compact.remove(QRegularExpression(QStringLiteral("[\\s,;:_-]")));
    if (compact.isEmpty() || compact.size() % 2 != 0) {
        return {};
    }

    QByteArray bytes;
    bytes.reserve(compact.size() / 2);
    for (int i = 0; i < compact.size(); i += 2) {
        bool ok = false;
        const int value = compact.mid(i, 2).toInt(&ok, 16);
        if (!ok) {
            return {};
        }
        bytes.append(static_cast<char>(value));
    }
    return bytes;
}

void RawViewerPlugin::trimDisplayedIndexes()
{
    const int blockCount = m_log->document()->blockCount();
    while (m_displayedEntryIndexes.size() > blockCount) {
        m_displayedEntryIndexes.removeFirst();
    }
}
