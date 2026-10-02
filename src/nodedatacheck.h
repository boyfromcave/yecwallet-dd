#ifndef NODEDATACHECK_H
#define NODEDATACHECK_H

#include <QDateTime>
#include <QString>

// The one-way data-directory upgrade check (owner decision 2026-10-01).
//
// The bundled ycashd 6.20.0 cannot read a block index written by ycashd v4.5.0 or 4.4.x. It
// stops with "LoadBlockIndex() : failed to read value ... restart with -reindex"; after that
// reindex the older node refuses the directory ("block index inconsistency (post-Heartwood)")
// until it reindexes in turn. On mainnet that is a full reindex each way, so the wallet warns
// before it starts the embedded node on a data directory that may still be in the older format.
//
// What a failed 6.20.0 start leaves behind (verified on regtest, docs/yellowback.md "Packaging"):
// wallet.dat, peers.dat, banlist.dat, fee_estimates.dat and the Yellowback index are untouched;
// LevelDB folds blocks/index and chainstate's write-ahead logs into tables on open (a format both
// lines read) and a BerkeleyDB log appears under database/. v4.5.0 still starts on it unchanged.
// The point of no return is the reindex, not the first failed start.
//
// Nothing here starts a node or shows a window; ConnectionLoader drives the dialog.
namespace NodeDataCheck {

// The file the wallet writes into the network data directory (next to blocks/) once the user
// has accepted the upgrade. First line: the node version, e.g. "6.20.0".
constexpr const char* MARKER_FILE = "yecwallet-node-version";

// The first node version whose block index is incompatible with v4.5.0 (getinfo.version scale,
// as NodeCompat::YCASH6_MIN_VERSION).
constexpr int UPGRADING_MIN_VERSION = 6200000;

// The directories one ycash.conf implies.
struct DataDir {
    QString baseDir;     // datadir= or the directory holding the conf
    QString netDir;      // baseDir, or baseDir/testnet3, baseDir/regtest
    QString walletPath;  // netDir/wallet.dat (or wallet=<name>)
};

// Reads datadir=, testnet=1, regtest=1 and wallet= from a ycash.conf.
DataDir resolve(const QString& confPath);

enum class State {
    Fresh,    // no blocks/index: a new data directory, nothing to upgrade
    Marked,   // the wallet's marker says the user already accepted the upgrade
    Current,  // debug.log: the last node that loaded the block index was 6.20.0 or later
    Older,    // debug.log: the last node that loaded the block index was older than 6.20.0
    Unknown,  // blocks/index exists, no marker and no usable debug.log: possibly older
};

struct Result {
    State   state = State::Fresh;
    int     indexVersion = 0;  // the version that last loaded the index (Current/Older), else 0
    bool    marked = false;    // the marker is present (Marked; or Older when debug.log shows the
                               // upgrade never completed after the user accepted it, audit F-2)
    QString reason;            // one line for the wallet log
};

// "v6.20.0-b397ab6e4", "Ycash Daemon version v4.5.0-...", "6.20.0" -> 6200050 / 4050050 (the
// getinfo.version scale: major*1000000 + minor*10000 + patch*100 + 50); 0 when absent.
int versionFromText(const QString& text);

// The version of the node that last loaded the block index, from debug.log text: the latest
// "Ycash version vX" run that reached the " block index NNNms" line. A run that failed to load
// the index (6.20.0 on a v4.5.0 directory) does not count. 0 when the text does not say.
int lastIndexLoaderVersion(const QString& debugLogText);

// Inspects a network data directory without changing it.
Result inspect(const QString& netDir);

// The warning is needed for Older and Unknown.
bool needsWarning(State state);

// Writes the marker (version text such as "6.20.0" on the first line). False + error on failure.
bool writeMarker(const QString& netDir, const QString& nodeVersionText, QString* error);

// Copies walletPath to walletPath + ".yecwallet-backup-<yyyyMMdd-HHmmss>" (a suffix is added if
// that exists). Returns the new path, or an empty string + error.
QString backupWallet(const QString& walletPath, const QDateTime& now, QString* error);

// Total size in bytes of the files under dir (for the "full copy" note in the dialog).
qint64 directorySize(const QString& dir);

}  // namespace NodeDataCheck

#endif // NODEDATACHECK_H
