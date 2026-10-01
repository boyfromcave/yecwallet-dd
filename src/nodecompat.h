#ifndef NODECOMPAT_H
#define NODECOMPAT_H

#include <QString>
#include "3rdparty/json/json.hpp"

// One wallet for both ycashd lines (ycash6 plan Phase 7, owner decision: version-aware calls).
//
// The node version is read once at connect (getinfo.version, see
// ConnectionLoader::refreshZcashdState) and kept on the Connection. Everything below branches
// only where the stock RPCs of the two lines differ; for any version below YCASH6_MIN_VERSION,
// including "unknown" (0), the wallet sends exactly what it sent before this file existed.
//
//   v4.5.0  getinfo.version 4050050  (ref/ycash/configure.ac:3-6)
//   6.20.0  getinfo.version 6200050  (ref/ycash6/src/clientversion.h:18-21)
namespace NodeCompat {

using json = nlohmann::json;

constexpr int YCASH6_MIN_VERSION = 6200000;

inline bool isYcash6(int nodeVersion) { return nodeVersion >= YCASH6_MIN_VERSION; }

// getinfo.version, or 0 when the reply does not carry a number.
inline int versionFromGetinfo(const json& reply) {
    if (!reply.is_object()) return 0;
    auto it = reply.find("version");
    if (it == reply.end() || !it->is_number_integer()) return 0;
    return it->get<int>();
}

// getrescaninfo / rescanblockchain: Ycash-only RPCs that 6.20.0 does not have (the reply is
// "Method not found"). On 6.20.0 a rescan runs only inside an import RPC, synchronously, or at
// startup under -rescan.
inline bool hasRescanRpcs(int nodeVersion) { return !isYcash6(nodeVersion); }

// z_importivk.
//   v4.5.0: ivk, rescan ("yes"/"no"), startHeight, zaddr  (ref/ycash/src/wallet/rpcdump.cpp:1211)
//   6.20.0: ivk, zaddr, rescan ("yes"/"no"/"whenkeyisnew"), startHeight
//           (ref/ycash6/src/wallet/rpcdump.cpp:1180; arity {{s,s},{s,o}}, rpc/common.h:138)
inline json importIvkParams(int nodeVersion, const QString& ivk, bool rescan, int height, const QString& zaddr) {
    if (isYcash6(nodeVersion))
        return { ivk.toStdString(), zaddr.toStdString(), (rescan ? "yes" : "no"), height };
    return { ivk.toStdString(), (rescan ? "yes" : "no"), height, zaddr.toStdString() };
}

// importprivkey.
//   v4.5.0: key, label, rescan, startHeight  (Ycash-only 4th argument, ref/ycash/src/wallet/rpcdump.cpp:121)
//   6.20.0: key, label, rescan               (1-3 enforced server-side, ref/ycash6/src/rpc/common.h:126)
// 6.20.0 has no start height: a rescan always starts at genesis
// (ref/ycash6/src/wallet/rpcdump.cpp ScanForWalletTransactions(chainActive.Genesis(), ...)).
inline json importPrivKeyParams(int nodeVersion, const QString& key, bool rescan, int height) {
    if (isYcash6(nodeVersion))
        return { key.toStdString(), "", rescan };
    return { key.toStdString(), "", rescan, height };
}

// The ycash.conf key that turns on the fast initial sync. v4.5.0 honours both `fastsync` and
// `ibdskiptxverification` identically (ref/ycash/src/init.cpp:1078-1079); 6.20.0 knows only
// `ibdskiptxverification` (ref/ycash6/src/init.cpp:1199) and silently ignores `fastsync`. The
// conf is written before any node is running, so the wallet writes the key both lines read.
constexpr const char* FASTSYNC_CONF_KEY        = "ibdskiptxverification";
constexpr const char* FASTSYNC_LEGACY_CONF_KEY = "fastsync";

inline bool isFastSyncConfKey(const QString& lowerName) {
    return lowerName == FASTSYNC_CONF_KEY || lowerName == FASTSYNC_LEGACY_CONF_KEY;
}

// z_getnewaddress "sprout" is refused after Canopy on 6.20.0. The wallet's UI only ever asks for
// Sapling addresses (MainWindow::addNewZaddr is always called with sapling=true), so there is no
// option to hide; this is the guard should one ever be added.
inline bool canCreateSprout(int nodeVersion) { return !isYcash6(nodeVersion); }

} // namespace NodeCompat

#endif // NODECOMPAT_H
