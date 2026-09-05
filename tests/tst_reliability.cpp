#include "core/PluginManager.h"
#include "core/RingBufferPool.h"
#include <QApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class ReliabilityTest : public QObject {
    Q_OBJECT
private:
    PluginManager manager;
    IProtocolPlugin* can = nullptr;
    QByteArray frame(quint32 id, const QByteArray& payload) {
        QVariantMap command;
        command.insert("can_id", id);
        command.insert("payload", payload);
        return can->encodeCommand(command);
    }
private slots:
    void initTestCase() {
        registerMcuDebugMetaTypes();
        manager.scanPlugins(QString::fromUtf8(MCD_PLUGIN_ROOT));
        for (auto* p : manager.protocolPlugins())
            if (p->name() == "CAN Frame") can = p;
        QVERIFY2(can, "CAN plugin must load from the app plugin directory");
    }
    void init() { can->reset(); }
    void splitAtEveryBoundary() {
        const QByteArray bytes = frame(0x123, QByteArray::fromHex("aabbcc"));
        for (int split = 0; split <= bytes.size(); ++split) {
            can->reset();
            QSignalSpy spy(can, &IProtocolPlugin::frameParsed);
            can->feedBytes(bytes.left(split));
            can->feedBytes(bytes.mid(split));
            QCOMPARE(spy.size(), 1);
            QCOMPARE(qvariant_cast<DataFrame>(spy.at(0).at(0)).channels.size(), 3);
        }
    }
    void noiseThenSplitHeader() {
        QSignalSpy spy(can, &IProtocolPlugin::frameParsed);
        can->feedBytes(QByteArray::fromHex("000000000000ca"));
        can->feedBytes(frame(0x123, QByteArray::fromHex("ab")).mid(1));
        QCOMPARE(spy.size(), 1);
    }
    void resetDropsOldPartialFrame() {
        QSignalSpy spy(can, &IProtocolPlugin::frameParsed);
        can->feedBytes(QByteArray::fromHex("cafd0000011102"));
        can->reset();
        can->feedBytes(frame(0x123, QByteArray::fromHex("ab")));
        QCOMPARE(spy.size(), 1);
        QCOMPARE(qvariant_cast<DataFrame>(spy.at(0).at(0)).attributes.value("can_id").toUInt(), quint32(0x123));
    }
    void identifiersDoNotAlias() {
        QSignalSpy spy(can, &IProtocolPlugin::frameParsed);
        can->feedBytes(frame(0x123, QByteArray(1, 'a')));
        can->feedBytes(frame(0x523, QByteArray(1, 'b')));
        can->feedBytes(frame(0x1fffffff, QByteArray(64, 'c')));
        QCOMPARE(spy.size(), 3);
        const auto a = qvariant_cast<DataFrame>(spy.at(0).at(0));
        const auto b = qvariant_cast<DataFrame>(spy.at(1).at(0));
        const auto c = qvariant_cast<DataFrame>(spy.at(2).at(0));
        QVERIFY(a.channels.first().index != b.channels.first().index);
        QCOMPARE(c.channels.last().index, (ChannelId(0x1fffffff) << 6) | ChannelId(63));
    }
    void lazyRingAndWrap() {
        RingBuffer ring;
        ring.capacity = 3;
        ring.push({1, 1});
        QCOMPARE(ring.data.size(), 1);
        for (int i = 2; i <= 5; ++i) ring.push({i, double(i)});
        const auto values = ring.range(0, 0);
        QCOMPARE(values.size(), 3);
        QCOMPARE(values.first().value, 3.0);
        QCOMPARE(values.last().value, 5.0);
    }
    void historyPreservesWideIds() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        RingBufferPool a(3), b;
        const ChannelId id = (ChannelId(0x1fffffff) << 6) | 63;
        a.push(id, {100, 4.0});
        QString error;
        const QString path = dir.filePath("test.mcdr");
        QVERIFY2(a.saveToFile(path, &error), qPrintable(error));
        QVERIFY2(b.loadFromFile(path, &error), qPrintable(error));
        QCOMPARE(b.activeChannels(), QList<ChannelId>{id});
        QCOMPARE(b.replay(id, 0).first().value, 4.0);
    }
    void cleanupTestCase() { manager.clear(); }
};
QTEST_MAIN(ReliabilityTest)
#include "tst_reliability.moc"
