#include "sdk/UsbIoWorker.h"
#include <QElapsedTimer>
#include <QtTest>
#include <atomic>

class UsbWorkerTests : public QObject {
    Q_OBJECT
private slots:
    void enqueueIsBoundedAndDoesNotWaitForNativeWrite() {
        std::atomic_bool entered{false};
        std::atomic<int> received{0};
        UsbIoWorker worker(this);
        worker.start([]() { QThread::msleep(1); return UsbIoWorker::ReadResult{}; },
            [&](const QByteArray&, const std::atomic_bool& running) {
                entered = true;
                while (running) QThread::msleep(1);
                return QString{};
            }, [&](const QByteArray&) { ++received; }, [](const QString&) {}, []() {});
        QElapsedTimer timer;
        timer.start();
        int accepted = 0;
        const QByteArray bytes(64 * 1024, 'x');
        for (int i = 0; i < 17; ++i) if (worker.enqueue(bytes) == bytes.size()) ++accepted;
        QCOMPARE(accepted, 16); // Includes the in-flight command, not just waiting commands.
        QVERIFY(timer.elapsed() < 1000);
        QTRY_VERIFY(entered.load());
        worker.stop();
        QVERIFY(!worker.running());
        QCOMPARE(received.load(), 0);
    }
    void oldQueuedCallbacksAreDiscardedOnStop() {
        std::atomic_bool issued{false};
        int callbacks = 0;
        UsbIoWorker worker(this);
        worker.start([&]() {
            if (!issued.exchange(true)) return UsbIoWorker::ReadResult{QByteArray("old"), {}};
            QThread::msleep(1); return UsbIoWorker::ReadResult{};
        }, [](const QByteArray&, const std::atomic_bool&) { return QString{}; },
        [&](const QByteArray&) { ++callbacks; }, [](const QString&) {}, []() {});
        QElapsedTimer deadline; deadline.start();
        while (!issued && deadline.elapsed() < 1000) QThread::msleep(1); // Do not pump queued callbacks yet.
        QVERIFY(issued.load());
        worker.stop();
        QCoreApplication::processEvents();
        QCOMPARE(callbacks, 0);
    }
    void zeroLengthReadIsNotADisconnect() {
        int errors = 0, stopped = 0;
        UsbIoWorker worker(this);
        worker.start([]() { return UsbIoWorker::ReadResult{}; },
            [](const QByteArray&, const std::atomic_bool&) { return QString{}; },
            [](const QByteArray&) {}, [&](const QString&) { ++errors; }, [&]() { ++stopped; });
        QTest::qWait(20);
        QVERIFY(worker.running());
        QCOMPARE(errors, 0); QCOMPARE(stopped, 0);
        worker.stop();
    }
    void writeFailureDoesNotRetryACommand() {
        std::atomic<int> writes{0};
        int errors = 0, stopped = 0;
        UsbIoWorker worker(this);
        worker.start([]() { return UsbIoWorker::ReadResult{}; },
            [&](const QByteArray&, const std::atomic_bool&) { ++writes; return QString("simulated partial write failure"); },
            [](const QByteArray&) {}, [&](const QString&) { ++errors; }, [&]() { ++stopped; });
        QCOMPARE(worker.enqueue("command"), qint64(7));
        QTRY_COMPARE(errors, 1);
        QTRY_COMPARE(stopped, 1);
        QCOMPARE(writes.load(), 1);
        QVERIFY(!worker.running());
        worker.stop();
    }
    void rxOverflowIsReportedRatherThanSilentlyDroppingStreamBytes() {
        int errors = 0;
        UsbIoWorker worker(this);
        worker.start([]() { return UsbIoWorker::ReadResult{QByteArray(4096, 'x'), {}}; },
            [](const QByteArray&, const std::atomic_bool&) { return QString{}; },
            [](const QByteArray&) {}, [&](const QString&) { ++errors; }, []() {});
        QElapsedTimer deadline; deadline.start();
        while (worker.running() && deadline.elapsed() < 2000) QThread::msleep(1);
        QVERIFY(!worker.running());
        QTRY_COMPARE(errors, 1);
        worker.stop();
    }
};
QTEST_MAIN(UsbWorkerTests)
#include "tst_usb_worker.moc"
