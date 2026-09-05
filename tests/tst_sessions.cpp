#include "core/DebugCore.h"
#include <QComboBox>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSignalSpy>
#include <QSpinBox>
#include <QToolButton>
#include <QtTest>
#include <functional>
#include <limits>

class CallbackVisual : public IVisualPlugin {
public:
    std::function<void()> callback;
    void onChannelData(const DataFrame&) override { if (callback) callback(); }
    QList<ChannelId> subscribedChannels() override { return {}; }
    qint64 historyFrom() override { return 0; }
    QString name() const override { return "test"; }
};
class SessionTests : public QObject {
    Q_OBJECT
    DebugCore* core = nullptr;
    IPhysicalPlugin* physical = nullptr;
    IVisualPlugin* visual(const QString& name) {
        for (auto* p : core->pluginManager()->visualPlugins()) if (p->name() == name) return p;
        return nullptr;
    }
    IControlPlugin* control(const QString& name) {
        for (auto* p : core->pluginManager()->controlPlugins()) if (p->name() == name) return p;
        return nullptr;
    }
    bool open(const QString& protocol = "Raw Protocol", const QVariantMap& config = {}) {
        auto* manager = core->pluginManager();
        return manager->activateProtocol(protocol) && manager->activatePhysical("Test Physical", config);
    }
    void packet(IVisualPlugin* target, const QByteArray& bytes) {
        DataFrame frame;
        frame.timestamp_us = 1000000;
        frame.rawPayload = bytes;
        target->onChannelData(frame);
    }
private slots:
    void initTestCase() {
        core = DebugCore::instance();
        core->initialize();
    }
    void init() {
        auto* manager = core->pluginManager();
        manager->scanPlugins(QString::fromUtf8(MCD_PLUGIN_ROOT));
        manager->scanPlugins(QString::fromUtf8(MCD_TEST_PLUGIN_ROOT));
        physical = nullptr;
        for (auto* p : manager->physicalPlugins()) if (p->name() == "Test Physical") physical = p;
        QVERIFY(physical);
        for (auto* p : manager->visualPlugins()) core->channelHub()->subscribe(p, p->subscribedChannels());
        for (auto* p : manager->controlPlugins())
            connect(p, &IControlPlugin::commandGenerated, core, &DebugCore::sendCommand, Qt::UniqueConnection);
    }
    void cleanup() {
        core->pluginManager()->deactivateAll();
        core->clearHistory();
        core->setChannelMetadata(QVariantMap{});
        QCoreApplication::processEvents();
    }
    void queuedOldSessionIsIgnored() {
        QVERIFY(open());
        QSignalSpy spy(core, &DebugCore::framePublished);
        physical->dataReceived("old");
        core->pluginManager()->deactivateAll();
        QVERIFY(open());
        physical->dataReceived("new");
        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(qvariant_cast<DataFrame>(spy.first().first()).rawPayload, QByteArray("new"));
        QVERIFY(physical->isOpen());
    }
    void receivesFirstPacketDuringOpen() {
        QSignalSpy spy(core, &DebugCore::framePublished);
        QVERIFY(open("Raw Protocol", {{"first", QByteArray("first")}}));
        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(qvariant_cast<DataFrame>(spy.first().first()).rawPayload, QByteArray("first"));
    }
    void failedOpenDoesNotPoisonNextConnection() {
        QVERIFY(!open("Raw Protocol", {{"fail", true}}));
        QVERIFY(!core->pluginManager()->activePhysical());
        QVERIFY(open());
        QCoreApplication::processEvents();
        QVERIFY(physical->isOpen());
        QCOMPARE(core->pluginManager()->activePhysical(), physical);
    }
    void periodicSendMustBeRearmed() {
        QVERIFY(open());
        auto* raw = control("Raw Control");
        QVERIFY(raw);
        auto* input = raw->findChild<QLineEdit*>();
        auto* interval = raw->findChild<QSpinBox*>();
        QToolButton* periodic = nullptr;
        for (auto* button : raw->findChildren<QToolButton*>()) if (button->text() == "Periodic") periodic = button;
        QVERIFY(input && interval && periodic);
        input->setText("A5"); interval->setValue(1);
        QSignalSpy spy(raw, &IControlPlugin::commandGenerated);
        periodic->setChecked(true);
        QTRY_VERIFY(!spy.isEmpty());
        core->pluginManager()->deactivateAll();
        QVERIFY(!periodic->isChecked());
        const auto count = spy.size();
        QVERIFY(open());
        QTest::qWait(30);
        QCOMPARE(spy.size(), count);
    }
    void sliderBoundedAndConfigurationNeverSends() {
        auto* slider = control("Slider Widget");
        QVERIFY(slider);
        auto* value = slider->findChild<QSpinBox*>("sliderValue");
        auto* width = slider->findChild<QComboBox*>("payloadWidth");
        auto* send = slider->findChild<QToolButton*>("sendValue");
        auto* live = slider->findChild<QToolButton*>("liveValue");
        QVERIFY(value && width && send && live);
        QSignalSpy spy(slider, &IControlPlugin::commandGenerated);
        QCOMPARE(value->maximum(), 255);
        value->setValue(300);
        send->click();
        QCOMPARE(spy.size(), 1);
        const auto bytes = spy.first().first().toMap().value("bytes").toByteArray();
        QCOMPARE(quint8(bytes.back()), quint8(255));
        live->setChecked(true);
        width->setCurrentIndex(1);
        QVERIFY(!live->isChecked());
        QCOMPARE(spy.size(), 1);
        core->pluginManager()->stopControls();
        QVERIFY(!live->isChecked());
    }
    void canRejectsUnsupportedOrOversizedCommands() {
        QVERIFY(open("CAN Frame"));
        auto* can = core->pluginManager()->activeProtocol();
        QVERIFY(can->encodeCommand({{"source", "slider_widget"}, {"bytes", QByteArray("SL")}}).isEmpty());
        QVERIFY(can->encodeCommand({{"payload", QByteArray(65, 'a')}}).isEmpty());
        QVERIFY(can->encodeCommand({{"data", "GG"}}).isEmpty());
        QVERIFY(can->encodeCommand({{"data", "A"}}).isEmpty());
    }
    void userMetadataWinsOverParserLabels() {
        const ChannelId id = ChannelId(0x1fffffff) << 6;
        core->setChannelMetadata(id, "Battery", "V");
        QSignalSpy spy(core, &DebugCore::framePublished);
        DataFrame frame;
        frame.timestamp_us = 1;
        frame.channels = {{id, 12.5, "CAN GENERATED", "byte"}};
        core->publish(frame);
        const auto enriched = qvariant_cast<DataFrame>(spy.first().first());
        QCOMPARE(enriched.channels.first().name, QString("Battery"));
        QCOMPARE(enriched.channels.first().unit, QString("V"));
    }
    void replayDoesNotTransmitDuplicateOrEraseRawEvidence() {
        auto* chart = visual("Time Chart");
        auto* raw = visual("Raw Viewer");
        auto* gauge = visual("Gauge");
        QVERIFY(chart && raw && gauge);
        auto* list = raw->findChild<QListView*>("packetList");
        auto* selector = chart->findChild<QComboBox*>("chartChannelSelector");
        QVERIFY(list && selector);
        packet(raw, "evidence");
        QTRY_COMPARE(list->model()->rowCount(), 1);
        const ChannelId id = (ChannelId(0x1fffffff) << 6) | 63;
        QVERIFY(core->ringBufferPool()->push(id, {100, 12}));
        QSignalSpy tx(core, &DebugCore::commandSent);
        core->replayHistory();
        QCOMPARE(selector->count(), 2);
        QCOMPARE(selector->itemData(1).toULongLong(), id);
        QCOMPARE(core->ringBufferPool()->replay(id, 0).size(), 1);
        QCOMPARE(list->model()->rowCount(), 1);
        QCOMPARE(tx.size(), 0);
        core->clearHistory();
        QCOMPARE(selector->count(), 1);
        QCOMPARE(list->model()->rowCount(), 0);
    }
    void packetPauseFilterAndSelectionStayAligned() {
        auto* raw = visual("Raw Viewer");
        QVERIFY(raw);
        auto* pause = raw->findChild<QToolButton*>("pausePackets");
        auto* list = raw->findChild<QListView*>("packetList");
        auto* details = raw->findChild<QPlainTextEdit*>("packetDetails");
        auto* filter = raw->findChild<QLineEdit*>("packetFilter");
        QVERIFY(pause && list && details && filter);
        packet(raw, "A");
        QTRY_COMPARE(list->model()->rowCount(), 1);
        list->setCurrentIndex(list->model()->index(0, 0));
        pause->setChecked(true);
        packet(raw, "B"); packet(raw, "C");
        QTest::qWait(30);
        QCOMPARE(list->model()->rowCount(), 1);
        QCOMPARE(list->currentIndex().data(Qt::UserRole).toByteArray(), QByteArray("A"));
        pause->setChecked(false);
        QTRY_COMPARE(list->model()->rowCount(), 3);
        filter->setText("42");
        QCOMPARE(list->model()->rowCount(), 1);
        list->setCurrentIndex(list->model()->index(0, 0));
        QCOMPARE(list->currentIndex().data(Qt::UserRole).toByteArray(), QByteArray("B"));
        QVERIFY(details->toPlainText().contains("42"));
        filter->setText("GG");
        QCOMPARE(list->model()->rowCount(), 1); // Invalid filter does not silently select everything.
        filter->clear();
    }
    void packetRetentionDoesNotRemapRowsToWrongPayloads() {
        auto* raw = visual("Raw Viewer");
        auto* list = raw->findChild<QListView*>("packetList");
        QVERIFY(list);
        for (int i = 0; i < 50000; ++i) packet(raw, QByteArray::number(i));
        QTRY_COMPARE(list->model()->rowCount(), 50000);
        packet(raw, "last");
        QTest::qWait(40);
        QCOMPARE(list->model()->rowCount(), 50000);
        QCOMPARE(list->model()->index(0, 0).data(Qt::UserRole).toByteArray(), QByteArray("1"));
        QCOMPARE(list->model()->index(49999, 0).data(Qt::UserRole).toByteArray(), QByteArray("last"));
    }
    void callbacksCanDestroyAnotherSubscriber() {
        ChannelHub hub;
        QPointer<CallbackVisual> a = new CallbackVisual;
        QPointer<CallbackVisual> b = new CallbackVisual;
        int calls = 0;
        a->callback = [&]() { ++calls; delete b.data(); };
        b->callback = [&]() { ++calls; delete a.data(); };
        hub.subscribe(a, {}); hub.subscribe(b, {});
        hub.dispatch(DataFrame{});
        QCOMPARE(calls, 1);
        delete a.data(); delete b.data();
        hub.dispatch(DataFrame{});
        QCOMPARE(calls, 1);
    }
    void repeatedUnloadAndRescan() {
        auto* manager = core->pluginManager();
        for (int i = 0; i < 5; ++i) {
            QPointer<IVisualPlugin> old = visual("Raw Viewer");
            QVERIFY(old);
            manager->clear();
            QVERIFY(old.isNull());
            DataFrame empty;
            core->publish(empty);
            manager->scanPlugins(QString::fromUtf8(MCD_PLUGIN_ROOT));
            for (auto* p : manager->visualPlugins()) core->channelHub()->subscribe(p, p->subscribedChannels());
        }
    }
    void cleanupTestCase() { core->pluginManager()->clear(); }
};
QTEST_MAIN(SessionTests)
#include "tst_sessions.moc"
