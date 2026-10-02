#include "nodedatacheck.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QTextStream>

namespace NodeDataCheck {

DataDir resolve(const QString& confPath) {
    DataDir d;
    QFileInfo confInfo(confPath);
    d.baseDir = confInfo.absoluteDir().absolutePath();
    QString netSubdir;
    QString walletName = "wallet.dat";

    QFile file(confPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith('#')) continue;
            auto s = line.indexOf('=');
            if (s < 0) continue;
            QString name  = line.left(s).trimmed().toLower();
            QString value = line.mid(s + 1).trimmed();
            if (name == "datadir" && !value.isEmpty())
                d.baseDir = QDir(value).absolutePath();
            else if (name == "testnet" && value == "1")
                netSubdir = "testnet3";
            else if (name == "regtest" && value == "1")
                netSubdir = "regtest";
            else if (name == "wallet" && !value.isEmpty())
                walletName = value;
        }
    }

    d.netDir = netSubdir.isEmpty() ? d.baseDir : QDir(d.baseDir).filePath(netSubdir);
    d.walletPath = QDir::isAbsolutePath(walletName) ? walletName : QDir(d.netDir).filePath(walletName);
    return d;
}

int versionFromText(const QString& text) {
    static const QRegularExpression re("(\\d+)\\.(\\d+)\\.(\\d+)");
    auto m = re.match(text);
    if (!m.hasMatch()) return 0;
    return m.captured(1).toInt() * 1000000 + m.captured(2).toInt() * 10000
         + m.captured(3).toInt() * 100 + 50;
}

int lastIndexLoaderVersion(const QString& debugLogText) {
    // ref/ycash/src/init.cpp:916 and ref/ycash6/src/init.cpp:1018 ("Ycash version %s"),
    // ref/ycash/src/init.cpp:1760 and ref/ycash6/src/init.cpp:2013 (" block index %15dms",
    // printed only once LoadBlockIndex has succeeded or a reindex has begun).
    static const QRegularExpression versionLine("Ycash version v(\\d+\\.\\d+\\.\\d+)");
    static const QRegularExpression indexLine("\\sblock index\\s+-?\\d+ms\\s*$");
    int run = 0;
    int loaded = 0;
    const auto lines = debugLogText.split('\n');
    for (const auto& line : lines) {
        auto m = versionLine.match(line);
        if (m.hasMatch()) {
            run = versionFromText(m.captured(1));
            continue;
        }
        if (run != 0 && indexLine.match(line).hasMatch())
            loaded = run;
    }
    return loaded;
}

static int markerVersion(const QString& netDir) {
    QFile f(QDir(netDir).filePath(MARKER_FILE));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    return versionFromText(QString::fromUtf8(f.readLine()));
}

static QString readDebugLogTail(const QString& netDir) {
    // debug.log shrinks to about 10 MB at startup by default (-shrinkdebugfile); read at most
    // the last 16 MiB of a larger one.
    constexpr qint64 maxBytes = 16 * 1024 * 1024;
    QFile f(QDir(netDir).filePath("debug.log"));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    if (f.size() > maxBytes) f.seek(f.size() - maxBytes);
    return QString::fromUtf8(f.readAll());
}

Result inspect(const QString& netDir) {
    Result r;
    QDir index(QDir(netDir).filePath("blocks/index"));
    if (!index.exists() || index.isEmpty()) {
        r.state = State::Fresh;
        r.reason = "no block index in " + netDir;
        return r;
    }

    int marker = markerVersion(netDir);
    int loaded = lastIndexLoaderVersion(readDebugLogTail(netDir));
    if (marker >= UPGRADING_MIN_VERSION) {
        r.marked = true;
        // The marker records the acceptance, not the upgrade: when the log still shows an older
        // node as the last to load the index, the reindex never ran (the user quit before it
        // started, or restored an older blocks/ afterwards) and the warning is due again.
        if (loaded != 0 && loaded < UPGRADING_MIN_VERSION) {
            r.state = State::Older;
            r.indexVersion = loaded;
            r.reason = QString("marker %1 records version %2 but debug.log shows the block index last loaded by ycashd version %3: the upgrade did not complete")
                           .arg(MARKER_FILE).arg(marker).arg(loaded);
            return r;
        }
        r.state = State::Marked;
        r.indexVersion = marker;
        r.reason = QString("marker %1 records version %2").arg(MARKER_FILE).arg(marker);
        return r;
    }

    if (loaded == 0) {
        r.state = State::Unknown;
        r.reason = "block index present, no marker, debug.log does not say which node wrote it";
        return r;
    }
    r.indexVersion = loaded;
    r.state = loaded >= UPGRADING_MIN_VERSION ? State::Current : State::Older;
    r.reason = QString("debug.log: block index last loaded by ycashd version %1").arg(loaded);
    return r;
}

bool needsWarning(State state) {
    return state == State::Older || state == State::Unknown;
}

bool writeMarker(const QString& netDir, const QString& nodeVersionText, QString* error) {
    QFile f(QDir(netDir).filePath(MARKER_FILE));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    QTextStream out(&f);
    out << nodeVersionText << "\n"
        << "# Written by YecWallet when the user accepted that ycashd " << nodeVersionText << "\n"
        << "# upgrades this data directory (older ycashd versions need a reindex to use it).\n"
        << "# YecWallet does not warn again while this file is here.\n";
    out.flush();
    if (f.error() != QFileDevice::NoError) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

QString backupWallet(const QString& walletPath, const QDateTime& now, QString* error) {
    if (!QFile::exists(walletPath)) {
        if (error) *error = QObject::tr("%1 does not exist").arg(walletPath);
        return QString();
    }
    QString base = walletPath + ".yecwallet-backup-" + now.toString("yyyyMMdd-HHmmss");
    QString target = base;
    for (int i = 1; QFile::exists(target); ++i)
        target = base + "-" + QString::number(i);
    QFile src(walletPath);
    if (!src.copy(target)) {
        if (error) *error = src.errorString();
        return QString();
    }
    return target;
}

qint64 directorySize(const QString& dir) {
    qint64 total = 0;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

}  // namespace NodeDataCheck
