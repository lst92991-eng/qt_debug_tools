#include "RawViewerPlugin.h"
#include <QAbstractListModel>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>
namespace {
constexpr int MaxEntries = 50000;
constexpr qsizetype MaxBytes = 8 * 1024 * 1024;
}
// Row removal and selection remapping are owned by Qt, not a parallel line-index array.
class RawPacketModel : public QAbstractListModel {
public:
    explicit RawPacketModel(QObject* parent) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = QModelIndex()) const override { return parent.isValid() ? 0 : int(entries.size()); }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
        const auto& entry = entries[size_t(index.row())];
        if (role == Qt::DisplayRole) return entry.line;
        if (role == Qt::UserRole) return entry.payload;
        if (role == Qt::ForegroundRole) return entry.direction == FrameDirection::Transmit ? QColor(24,145,77) : QColor(45,102,214);
        return {};
    }
    void append(std::deque<RawPacketEntry>& pending) {
        if (pending.empty()) return;
        const int oldCount = rowCount();
        beginInsertRows({}, oldCount, oldCount + int(pending.size()) - 1);
        for (auto& entry : pending) { bytes += entry.payload.size(); entries.push_back(std::move(entry)); }
        pending.clear();
        endInsertRows();
        qsizetype retained = bytes;
        size_t count = 0;
        while (count < entries.size() && (entries.size() - count > MaxEntries || retained > MaxBytes)) retained -= entries[count++].payload.size();
        if (count) {
            beginRemoveRows({}, 0, int(count) - 1);
            for (size_t i = 0; i < count; ++i) entries.pop_front();
            bytes = retained;
            evicted += count;
            endRemoveRows();
        }
    }
    void clear() { beginResetModel(); entries.clear(); bytes = 0; evicted = 0; endResetModel(); }
    std::deque<RawPacketEntry> entries;
    qsizetype bytes = 0;
    quint64 evicted = 0;
};
class RawPacketFilter : public QSortFilterProxyModel {
public:
    explicit RawPacketFilter(QObject* parent) : QSortFilterProxyModel(parent) {}
    void setNeedle(const QByteArray& needle) { m_needle = needle; invalidateFilter(); }
protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        return m_needle.isEmpty() || sourceModel()->index(row, 0, parent).data(Qt::UserRole).toByteArray().contains(m_needle);
    }
private:
    QByteArray m_needle;
};
RawViewerPlugin::RawViewerPlugin(QWidget* parent) : IVisualPlugin(parent)
{
    buildUi();
    connect(&m_flushTimer, &QTimer::timeout, this, &RawViewerPlugin::flushPending);
    m_flushTimer.start(16);
}
void RawViewerPlugin::onChannelData(const DataFrame& frame)
{
    if (frame.rawPayload.isEmpty()) return;
    if (frame.rawPayload.size() > MaxBytes) { ++m_dropped; return; }
    RawPacketEntry entry;
    entry.timestamp_us = frame.timestamp_us;
    entry.direction = frame.direction;
    entry.payload = frame.rawPayload;
    entry.attributes = frame.attributes;
    entry.line = formatLine(entry);
    m_pendingBytes += entry.payload.size();
    m_pending.push_back(std::move(entry));
    while (m_pending.size() > MaxEntries || m_pendingBytes > MaxBytes) {
        m_pendingBytes -= m_pending.front().payload.size();
        m_pending.pop_front();
        ++m_dropped;
    }
}
QList<ChannelId> RawViewerPlugin::subscribedChannels() { return {}; }
qint64 RawViewerPlugin::historyFrom() { return 0; }
QString RawViewerPlugin::name() const { return QStringLiteral("Raw Viewer"); }
void RawViewerPlugin::clearHistory()
{
    m_pending.clear(); m_pendingBytes = 0; m_dropped = 0;
    m_model->clear(); m_detail->clear(); updateStatistics();
}
void RawViewerPlugin::buildUi()
{
    auto* root = new QVBoxLayout(this);
    auto* bar = new QHBoxLayout;
    m_autoScroll = new QCheckBox(tr("AutoScroll"), this);
    m_autoScroll->setChecked(true);
    m_pauseButton = new QToolButton(this);
    m_pauseButton->setObjectName("pausePackets");
    m_pauseButton->setText(tr("Pause"));
    m_pauseButton->setCheckable(true);
    auto* clear = new QPushButton(tr("Clear"), this);
    auto* save = new QPushButton(tr("Export Raw Log"), this);
    m_filter = new QLineEdit(this);
    m_filter->setObjectName("packetFilter");
    m_filter->setPlaceholderText(tr("Filter hex pattern"));
    bar->addWidget(m_autoScroll); bar->addWidget(m_pauseButton); bar->addWidget(clear); bar->addWidget(save); bar->addWidget(m_filter, 1);
    root->addLayout(bar);
    auto* splitter = new QSplitter(Qt::Vertical, this);
    m_log = new QListView(splitter);
    m_log->setObjectName("packetList");
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setUniformItemSizes(true);
    m_log->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_log->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_model = new RawPacketModel(this);
    m_proxy = new RawPacketFilter(this);
    m_proxy->setSourceModel(m_model);
    m_log->setModel(m_proxy);
    m_detail = new QPlainTextEdit(splitter);
    m_detail->setObjectName("packetDetails");
    m_detail->setReadOnly(true);
    m_detail->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_detail->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_detail->setMaximumHeight(160);
    splitter->addWidget(m_log); splitter->addWidget(m_detail);
    splitter->setStretchFactor(0, 4); splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);
    m_statistics = new QLabel(this);
    root->addWidget(m_statistics);
    connect(clear, &QPushButton::clicked, this, &RawViewerPlugin::clearHistory);
    connect(save, &QPushButton::clicked, this, &RawViewerPlugin::exportLog);
    connect(m_filter, &QLineEdit::textChanged, this, &RawViewerPlugin::updateFilter);
    connect(m_log->selectionModel(), &QItemSelectionModel::currentChanged, this, [this]() { updateDetailPane(); });
    auto* copy = new QShortcut(QKeySequence::Copy, m_log);
    connect(copy, &QShortcut::activated, this, [this]() {
        auto selected = m_log->selectionModel()->selectedRows();
        std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) { return a.row() < b.row(); });
        QStringList lines;
        for (const auto& index : selected) lines.append(index.data().toString());
        QApplication::clipboard()->setText(lines.join('\n'));
    });
}
void RawViewerPlugin::flushPending()
{
    if (!m_pauseButton->isChecked() && !m_pending.empty()) {
        m_model->append(m_pending);
        m_pendingBytes = 0;
        if (m_autoScroll->isChecked()) m_log->scrollToBottom();
        updateDetailPane();
    }
    updateStatistics();
}
void RawViewerPlugin::updateStatistics()
{
    m_statistics->setText(tr("Retained: %1 | Paused/pending: %2 | Evicted: %3 | Dropped: %4 | TX = queued, not device ACK")
        .arg(m_model->rowCount()).arg(qulonglong(m_pending.size())).arg(m_model->evicted).arg(m_dropped));
}
void RawViewerPlugin::updateDetailPane()
{
    const auto index = m_log->currentIndex();
    m_detail->setPlainText(index.isValid() ? formatHexDump(index.data(Qt::UserRole).toByteArray()) : QString{});
}
void RawViewerPlugin::updateFilter()
{
    QString text = m_filter->text();
    text.remove(QRegularExpression(QStringLiteral("[\\s,;:_-]")));
    static const QRegularExpression valid(QStringLiteral("^[0-9a-fA-F]*$"));
    if (text.size() % 2 || !valid.match(text).hasMatch()) {
        m_filter->setStyleSheet(QStringLiteral("QLineEdit { border: 1px solid red; }"));
        m_filter->setToolTip(tr("Invalid HEX filter; previous filter remains active"));
        return;
    }
    m_filter->setStyleSheet({}); m_filter->setToolTip({});
    m_proxy->setNeedle(QByteArray::fromHex(text.toLatin1()));
    updateDetailPane();
}
QString RawViewerPlugin::formatLine(const RawPacketEntry& entry) const
{
    const auto time = QDateTime::fromMSecsSinceEpoch(entry.timestamp_us / 1000).toString("HH:mm:ss.zzz")
        + QStringLiteral("%1").arg(entry.timestamp_us % 1000, 3, 10, QLatin1Char('0'));
    const auto direction = entry.direction == FrameDirection::Transmit ? QStringLiteral("TX") : QStringLiteral("RX");
    return QStringLiteral("%1  %2  %3  |%4|").arg(time, direction, formatHex(entry.payload, 32).leftJustified(98, QLatin1Char(' ')), formatAscii(entry.payload, 32));
}
QString RawViewerPlugin::formatHex(const QByteArray& payload, int maxBytes) const
{
    const qsizetype count = maxBytes < 0 ? payload.size() : std::min<qsizetype>(maxBytes, payload.size());
    QString result = QString::fromLatin1(payload.left(count).toHex(' ')).toUpper();
    if (count < payload.size()) result += " ...";
    return result;
}
QString RawViewerPlugin::formatAscii(const QByteArray& payload, int maxBytes) const
{
    const qsizetype count = maxBytes < 0 ? payload.size() : std::min<qsizetype>(maxBytes, payload.size());
    QString result;
    for (qsizetype i = 0; i < count; ++i) {
        const auto c = static_cast<unsigned char>(payload.at(i));
        result.append(c >= 32 && c < 127 ? QChar(c) : QChar('.'));
    }
    if (count < payload.size()) result += "...";
    return result;
}
QString RawViewerPlugin::formatHexDump(const QByteArray& payload) const
{
    QString result;
    for (qsizetype offset = 0; offset < payload.size(); offset += 16) {
        const auto bytes = payload.mid(offset, 16);
        result += QStringLiteral("%1  %2  |%3|\n").arg(qulonglong(offset), 8, 16, QLatin1Char('0')).toUpper()
            .arg(formatHex(bytes).leftJustified(47, QLatin1Char(' ')), formatAscii(bytes));
    }
    return result;
}
void RawViewerPlugin::exportLog()
{
    const auto path = QFileDialog::getSaveFileName(this, tr("Export Retained Raw Log"), "raw_log.jsonl", tr("Raw packet log (*.jsonl)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { QMessageBox::warning(this, tr("Export failed"), file.errorString()); return; }
    const auto write = [&file](const RawPacketEntry& entry) {
        auto attributes = entry.attributes;
        if (attributes.contains("payload")) attributes.insert("payload", QString::fromLatin1(attributes.value("payload").toByteArray().toHex()));
        if (attributes.contains("bytes")) attributes.insert("bytes", QString::fromLatin1(attributes.value("bytes").toByteArray().toHex()));
        QJsonObject row{{"timestamp_us", QString::number(entry.timestamp_us)},
            {"direction", entry.direction == FrameDirection::Transmit ? "TX" : "RX"},
            {"raw_hex", QString::fromLatin1(entry.payload.toHex())}, {"attributes", QJsonObject::fromVariantMap(attributes)}};
        const auto bytes = QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
        return file.write(bytes) == bytes.size();
    };
    bool ok = true;
    for (const auto& entry : m_model->entries) { if (!write(entry)) { ok = false; break; } }
    if (ok) for (const auto& entry : m_pending) { if (!write(entry)) { ok = false; break; } }
    if (!ok || !file.commit()) QMessageBox::warning(this, tr("Export failed"), file.errorString());
}
