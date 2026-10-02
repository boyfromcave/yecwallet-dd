// Offline QTest for the one-way data directory upgrade check (NodeDataCheck, owner decision
// 2026-10-01). Runs under QT_QPA_PLATFORM=offscreen with no node and no window.
//
// The fixture directories are built in a QTemporaryDir with the debug.log lines the two node
// lines really print (copied from regtest runs of ycash-dd v4.5.0 and ycash6 6.20.0, including a
// 6.20.0 start that failed on the v4.5.0 block index). With NODEDATACHECK_V45_NETDIR and
// NODEDATACHECK_V620_NETDIR set to real regtest network directories (<datadir>/regtest) the
// realDatadirs case also checks directories the nodes themselves created; without them it skips.

#include <QtTest>
#include <QTemporaryDir>

#include "nodedatacheck.h"

using namespace NodeDataCheck;

// ref/ycash/src/init.cpp:916, :1760 (v4.5.0 log format)
static const char* V45_RUN =
    "Oct 01 21:01:25.158  INFO main: Ycash version v4.5.0-f86bed6bd-dirty (2026-09-29 22:26:54 -0700)\n"
    "Oct 01 21:01:25.793  INFO Init: main: init message: Loading block index...\n"
    "Oct 01 21:01:25.794  INFO Init: main: Opening LevelDB in /x/regtest/blocks/index\n"
    "Oct 01 21:01:25.857  INFO Init: main:  block index              64ms\n"
    "Oct 01 21:01:29.243  INFO Shutdown: main: done\n";

// ref/ycash6/src/init.cpp:1018, :2013 (6.20.0 log format, a successful start)
static const char* V620_RUN =
    "2026-10-02T04:04:27.919614Z  INFO main: Ycash version v6.20.0-b397ab6e4\n"
    "2026-10-02T04:04:30.737397Z  INFO Init: main: * Using 2.0MiB for block index database\n"
    "2026-10-02T04:04:30.791915Z  INFO Init: main:  block index              54ms\n";

// 6.20.0 on a v4.5.0 block index: it never reaches the " block index NNms" line.
static const char* V620_FAILED_RUN =
    "2026-10-02T04:01:37.880189Z  INFO main: Ycash version v6.20.0-b397ab6e4\n"
    "2026-10-02T04:01:40.921121Z  INFO Init: main: init message: Loading block index...\n"
    "2026-10-02T04:01:40.921299Z  INFO Init: main: Opening LevelDB in /x/regtest/blocks/index\n"
    "2026-10-02T04:01:40.959873Z ERROR Init: main: LoadBlockIndex() : failed to read value\n"
    "2026-10-02T04:01:40.959880Z  INFO Init: main: : Error loading block database.\n"
    "2026-10-02T04:01:40.968449Z  INFO Shutdown: main: done\n";

static const char* V444_RUN =
    "Mar 03 10:00:00.000  INFO main: Ycash version v4.4.4-abcdef (2025-01-01 00:00:00 +0000)\n"
    "Mar 03 10:00:01.000  INFO Init: main:  block index            1234ms\n";

class NodeDataCheckTest : public QObject {
    Q_OBJECT

    // A network data directory under the temporary dir: blocks/index (with a LevelDB-like file
    // unless empty), an optional debug.log and an optional marker.
    QString makeNetDir(const QTemporaryDir& tmp, const QString& name, bool withIndex,
                       const QString& debugLog = QString(), const QString& marker = QString()) {
        QDir root(tmp.path());
        root.mkpath(name);
        QString dir = root.filePath(name);
        if (withIndex) {
            QDir(dir).mkpath("blocks/index");
            writeFile(QDir(dir).filePath("blocks/index/CURRENT"), "MANIFEST-000002\n");
            writeFile(QDir(dir).filePath("wallet.dat"), "not really a wallet");
        }
        if (!debugLog.isNull()) writeFile(QDir(dir).filePath("debug.log"), debugLog.toUtf8());
        if (!marker.isNull()) writeFile(QDir(dir).filePath(MARKER_FILE), marker.toUtf8());
        return dir;
    }
    static void writeFile(const QString& path, const QByteArray& bytes) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(bytes);
    }

private slots:
    void versionText() {
        QCOMPARE(versionFromText("Ycash Daemon version v6.20.0-b397ab6e4"), 6200050);
        QCOMPARE(versionFromText("Ycash Daemon version v4.5.0-f86bed6bd-dirty"), 4050050);
        QCOMPARE(versionFromText("6.20.0"), 6200050);
        QCOMPARE(versionFromText("v4.4.4"), 4040450);
        QCOMPARE(versionFromText("garbage"), 0);
        QVERIFY(versionFromText("6.20.0") >= UPGRADING_MIN_VERSION);
        QVERIFY(versionFromText("4.5.0") < UPGRADING_MIN_VERSION);
    }

    void logParsing() {
        QCOMPARE(lastIndexLoaderVersion(V45_RUN), 4050050);
        QCOMPARE(lastIndexLoaderVersion(V620_RUN), 6200050);
        QCOMPARE(lastIndexLoaderVersion(QString(V45_RUN) + V620_FAILED_RUN), 4050050);
        QCOMPARE(lastIndexLoaderVersion(QString(V45_RUN) + V620_RUN), 6200050);
        QCOMPARE(lastIndexLoaderVersion(QString(V620_RUN) + V45_RUN), 4050050);
        QCOMPARE(lastIndexLoaderVersion(V444_RUN), 4040450);
        QCOMPARE(lastIndexLoaderVersion(V620_FAILED_RUN), 0);
        QCOMPARE(lastIndexLoaderVersion(""), 0);
        // A tail that starts mid-run: the orphan index line has no version and does not count.
        QCOMPARE(lastIndexLoaderVersion("x INFO Init: main:  block index   9ms\n"), 0);
    }

    // The four cases of the owner's brief, plus the in-between ones.
    void v45CreatedWarns() {
        QTemporaryDir tmp;
        auto r = inspect(makeNetDir(tmp, "v45", true, V45_RUN));
        QCOMPARE(r.state, State::Older);
        QCOMPARE(r.indexVersion, 4050050);
        QVERIFY(needsWarning(r.state));
    }

    void v45AfterFailed620StartStillWarns() {
        QTemporaryDir tmp;
        auto r = inspect(makeNetDir(tmp, "v45f", true, QString(V45_RUN) + V620_FAILED_RUN));
        QCOMPARE(r.state, State::Older);
        QVERIFY(needsWarning(r.state));
    }

    void v444CreatedWarns() {
        QTemporaryDir tmp;
        auto r = inspect(makeNetDir(tmp, "v444", true, V444_RUN));
        QCOMPARE(r.state, State::Older);
        QVERIFY(needsWarning(r.state));
    }

    void v620CreatedDoesNotWarn() {
        QTemporaryDir tmp;
        auto r = inspect(makeNetDir(tmp, "v620", true, V620_RUN));
        QCOMPARE(r.state, State::Current);
        QVERIFY(!needsWarning(r.state));
    }

    void reindexedBy620DoesNotWarn() {
        QTemporaryDir tmp;
        auto r = inspect(makeNetDir(tmp, "re", true, QString(V45_RUN) + V620_FAILED_RUN + V620_RUN));
        QCOMPARE(r.state, State::Current);
        QVERIFY(!needsWarning(r.state));
    }

    void emptyOrNewDirDoesNotWarn() {
        QTemporaryDir tmp;
        QCOMPARE(inspect(makeNetDir(tmp, "empty", false)).state, State::Fresh);
        QCOMPARE(inspect(QDir(tmp.path()).filePath("does-not-exist")).state, State::Fresh);
        // blocks/index created but still empty
        QString dir = makeNetDir(tmp, "emptyindex", false);
        QDir(dir).mkpath("blocks/index");
        QCOMPARE(inspect(dir).state, State::Fresh);
        QVERIFY(!needsWarning(State::Fresh));
    }

    void markerPresentDoesNotWarn() {
        QTemporaryDir tmp;
        // The marker plus a log in which 6.20.0 loaded the index after v4.5.0: upgraded
        auto r = inspect(makeNetDir(tmp, "marked", true, QString(V45_RUN) + V620_RUN, "6.20.0\n# comment\n"));
        QCOMPARE(r.state, State::Marked);
        QVERIFY(r.marked);
        QVERIFY(!needsWarning(r.state));
        // The marker with no log at all (rotated away): the acceptance stands
        r = inspect(makeNetDir(tmp, "markednolog", true, QString(), "6.20.0\n"));
        QCOMPARE(r.state, State::Marked);
        QVERIFY(!needsWarning(r.state));
        // A marker naming an older version is not an acceptance.
        QCOMPARE(inspect(makeNetDir(tmp, "oldmarker", true, V45_RUN, "4.5.0\n")).state, State::Older);
    }

    // audit F-2: the marker records the acceptance, not the upgrade. When debug.log still shows
    // an older node as the last to load the index (the user quit before the reindex started, or
    // put an older blocks/ back), the warning is due again and says the upgrade did not complete.
    void markerWithoutUpgradeWarnsAgain() {
        QTemporaryDir tmp;
        auto r = inspect(makeNetDir(tmp, "markedold", true, V45_RUN, "6.20.0\n"));
        QCOMPARE(r.state, State::Older);
        QVERIFY(r.marked);
        QCOMPARE(r.indexVersion, 4050050);
        QVERIFY(needsWarning(r.state));
        QVERIFY2(r.reason.contains("did not complete"), qPrintable(r.reason));
        // A 6.20.0 start that failed to load the index does not count as the upgrade
        r = inspect(makeNetDir(tmp, "markedfailed", true, QString(V45_RUN) + V620_FAILED_RUN, "6.20.0\n"));
        QCOMPARE(r.state, State::Older);
        QVERIFY(r.marked);
        // An older node after the upgrade (a restored v4.5.0 blocks/ that v4.5.0 then loaded)
        r = inspect(makeNetDir(tmp, "markedrestored", true, QString(V620_RUN) + V45_RUN, "6.20.0\n"));
        QCOMPARE(r.state, State::Older);
        QVERIFY(r.marked);
        // Without the marker, the same log is plain Older
        r = inspect(makeNetDir(tmp, "plainold", true, V45_RUN));
        QCOMPARE(r.state, State::Older);
        QVERIFY(!r.marked);
    }

    void noEvidenceWarnsOnce() {
        QTemporaryDir tmp;
        QString dir = makeNetDir(tmp, "nolog", true);
        auto r = inspect(dir);
        QCOMPARE(r.state, State::Unknown);
        QVERIFY(needsWarning(r.state));
        QString error;
        QVERIFY2(writeMarker(dir, "6.20.0", &error), qPrintable(error));
        QCOMPARE(inspect(dir).state, State::Marked);
    }

    void resolveConf() {
        QTemporaryDir tmp;
        QDir root(tmp.path());
        writeFile(root.filePath("plain.conf"), "rpcuser=u\nrpcport=1\n");
        auto d = resolve(root.filePath("plain.conf"));
        QCOMPARE(d.baseDir, QDir(tmp.path()).absolutePath());
        QCOMPARE(d.netDir, d.baseDir);
        QCOMPARE(d.walletPath, QDir(d.netDir).filePath("wallet.dat"));

        writeFile(root.filePath("reg.conf"), "regtest=1\n datadir = /data/ycash \nwallet=w2.dat\n");
        d = resolve(root.filePath("reg.conf"));
        QCOMPARE(d.baseDir, QString("/data/ycash"));
        QCOMPARE(d.netDir, QString("/data/ycash/regtest"));
        QCOMPARE(d.walletPath, QString("/data/ycash/regtest/w2.dat"));

        writeFile(root.filePath("test.conf"), "#datadir=/nope\ntestnet=1\n");
        d = resolve(root.filePath("test.conf"));
        QCOMPARE(d.netDir, QDir(tmp.path()).absoluteFilePath("testnet3"));
    }

    void backup() {
        QTemporaryDir tmp;
        QString wallet = QDir(tmp.path()).filePath("wallet.dat");
        writeFile(wallet, QByteArray("keys\0keys", 9));
        QDateTime now(QDate(2026, 10, 1), QTime(21, 5, 7));
        QString error;
        QString b1 = backupWallet(wallet, now, &error);
        QCOMPARE(b1, wallet + ".yecwallet-backup-20261001-210507");
        QString b2 = backupWallet(wallet, now, &error);
        QCOMPARE(b2, b1 + "-1");
        QFile f(b1);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("keys\0keys", 9));
        QVERIFY(backupWallet(QDir(tmp.path()).filePath("missing.dat"), now, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QCOMPARE(directorySize(tmp.path()), qint64(27));
    }

    void inspectChangesNothing() {
        QTemporaryDir tmp;
        QString dir = makeNetDir(tmp, "ro", true, V45_RUN);
        QStringList before = QDir(dir).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        inspect(dir);
        QCOMPARE(QDir(dir).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), before);
    }

    void realDatadirs() {
        QString v45 = qEnvironmentVariable("NODEDATACHECK_V45_NETDIR");
        QString v620 = qEnvironmentVariable("NODEDATACHECK_V620_NETDIR");
        if (v45.isEmpty() || v620.isEmpty())
            QSKIP("NODEDATACHECK_V45_NETDIR / NODEDATACHECK_V620_NETDIR not set");
        auto r45 = inspect(v45);
        QCOMPARE(r45.state, State::Older);
        QCOMPARE(r45.indexVersion / 10000, 405);
        auto r620 = inspect(v620);
        QCOMPARE(r620.state, State::Current);
        QCOMPARE(r620.indexVersion / 10000, 620);
    }
};

QTEST_MAIN(NodeDataCheckTest)
#include "nodedatacheck_test.moc"
