#include "core/RingBufferPool.h"
#include "core/SessionIO.h"
#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

class StorageTests : public QObject {
    Q_OBJECT
    QByteArray history(int mode) {
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        out << quint32(0x4d434452) << quint16(2) << quint32(mode == 2 ? 2 : 1);
        out << quint64(7) << quint32(mode == 0 ? 1000001 : 3) << quint32(mode == 1 ? 4 : 1);
        out << qint64(mode == 5 ? -1 : 10) << (mode == 3 ? std::numeric_limits<double>::quiet_NaN() : 1.0);
        if (mode == 2) out << quint64(7) << quint32(3) << quint32(0);
        if (mode == 4) bytes.chop(1);
        if (mode == 6) bytes.append('x');
        return bytes;
    }
    QJsonObject session() {
        return {{"version", 2}, {"physical", "Test Physical"}, {"protocol", "Raw Protocol"},
            {"physical_configs", QJsonObject{}}, {"channel_metadata", QJsonObject{}}};
    }
private slots:
    void legacyV1StillLoads() {
        QTemporaryDir dir;
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        out << quint32(0x4d434452) << quint16(1) << quint32(1)
            << quint16(65535) << quint32(3) << quint32(1) << qint64(10) << double(7.5);
        QFile file(dir.filePath("legacy.mcdr"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size()); file.close();
        RingBufferPool pool;
        QString error;
        QVERIFY2(pool.loadFromFile(file.fileName(), &error), qPrintable(error));
        QCOMPARE(pool.activeChannels().first(), ChannelId(65535));
        QCOMPARE(pool.replay(65535, 0).first().value, 7.5);
    }
    void malformedHistoryIsTransactional_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("capacity-over-limit") << 0;
        QTest::newRow("count-over-capacity") << 1;
        QTest::newRow("duplicate-channel") << 2;
        QTest::newRow("nan-sample") << 3;
        QTest::newRow("truncated-sample") << 4;
        QTest::newRow("negative-timestamp") << 5;
        QTest::newRow("trailing-junk") << 6;
    }
    void malformedHistoryIsTransactional() {
        QFETCH(int, mode);
        QTemporaryDir dir;
        QFile file(dir.filePath("bad.mcdr"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes = history(mode);
        QCOMPARE(file.write(bytes), bytes.size()); file.close();
        RingBufferPool pool;
        QVERIFY(pool.push(999, {1, 42}));
        QString error;
        QVERIFY(!pool.loadFromFile(file.fileName(), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(pool.activeChannels(), QList<ChannelId>{999});
        QCOMPARE(pool.replay(999, 0).first().value, 42.0);
    }
    void shortHeaderRejected() {
        QTemporaryDir dir;
        QFile file(dir.filePath("bad.mcdr"));
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("MCDR"); file.close();
        RingBufferPool pool;
        QString error;
        QVERIFY(!pool.loadFromFile(file.fileName(), &error));
        QVERIFY(!error.isEmpty());
    }
    void fullQuotaKeepsAcceptingLatestSamples() {
        RingBufferPool pool;
        for (int timestamp = 0; timestamp < 17000; ++timestamp) {
            for (ChannelId channel = 0; channel < 64; ++channel) {
                if (!pool.push(channel, {timestamp, double(timestamp)})) QFAIL("Global quota must roll, not freeze, existing channels");
            }
        }
        qsizetype total = 0;
        for (ChannelId channel : pool.activeChannels()) {
            const auto values = pool.replay(channel, 0);
            total += values.size();
            QCOMPARE(values.last().value, 16999.0);
        }
        QVERIFY(total <= RingBufferPool::MaxTotalSamples);
    }
    void channelCountAndInvalidSamplesAreBounded() {
        RingBufferPool pool(1);
        for (ChannelId i = 0; i < RingBufferPool::MaxChannels; ++i) QVERIFY(pool.push(i, {0, 1}));
        QVERIFY(!pool.push(2000, {1, 1}));
        QVERIFY(!pool.push(0, {1, std::numeric_limits<double>::infinity()}));
        QVERIFY(!pool.push(0, {-1, 1}));
        QVERIFY(pool.push(0, {2, 2}));
        QCOMPARE(pool.replay(0, 0).size(), 1);
        QCOMPARE(pool.newestTimestamp(0), qint64(2));
    }
    void sessionValidWideIdentifier() {
        auto root = session();
        root.insert("channel_metadata", QJsonObject{{"18446744073709551615", QJsonObject{{"name", "wide"}, {"unit", "V"}}}});
        QJsonObject result;
        QString error;
        QVERIFY2(SessionIO::parse(QJsonDocument(root).toJson(), &result, &error), qPrintable(error));
        QCOMPARE(result, root);
    }
    void invalidSessionDoesNotMutateResult_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("syntax") << QByteArray("{broken");
        QTest::newRow("array") << QByteArray("[]");
        QTest::newRow("missing-required") << QByteArray("{}");
        auto root = session(); root.insert("version", 3);
        QTest::newRow("version") << QJsonDocument(root).toJson();
        root = session(); root.insert("channel_metadata", QJsonObject{{"18446744073709551616", QJsonObject{{"name", "x"}, {"unit", "V"}}}});
        QTest::newRow("overflow-channel-id") << QJsonDocument(root).toJson();
        root = session(); root.insert("physical_configs", QJsonObject{{"bad", 42}});
        QTest::newRow("config-not-object") << QJsonDocument(root).toJson();
        QTest::newRow("oversized") << QByteArray(SessionIO::MaxBytes + 1, ' ');
    }
    void invalidSessionDoesNotMutateResult() {
        QFETCH(QByteArray, bytes);
        QJsonObject result{{"keep", "unchanged"}};
        const auto original = result;
        QString error;
        QVERIFY(!SessionIO::parse(bytes, &result, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(result, original);
    }
};
QTEST_MAIN(StorageTests)
#include "tst_storage.moc"
