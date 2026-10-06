// Copyright (c) 2026 The Ycash developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or https://www.opensource.org/licenses/mit-license.php .

#ifndef VAULTRPC_H
#define VAULTRPC_H

// The vault primitive's RPCs (set_*, vault_*) the wallet uses, as ycash-dd/doc/vault-rpc.md
// documents them (upgrade plan §15.8). Under the vault upgrade a Yellowback claim is a primitive
// intent: it is released with vault_release after CLAIM_DELAY, and before that a member of the
// YED attestor set may cancel it (vault_buildcancel + set_signcancel + vault_send). An attestor
// is a member of that set and stays live with set_heartbeat. Everything else the wallet does goes
// through the yed_* contract (yellowbackrpc.h).
//
// The primitive is application-agnostic and its RPCs carry no rpcversion: the wallet reads only
// the fields named here and treats any other shape as an error message, never as data.

namespace VaultRpc {

// ── Methods ───────────────────────────────────────────────────────────────────────────────
constexpr const char* VAULT_GETINFO       = "vault_getinfo";
constexpr const char* SET_GETINFO         = "set_getinfo";       // "setid" ( height ): members, current/dormant
constexpr const char* SET_HEARTBEAT       = "set_heartbeat";     // "setid" ( "memberkey" ): a SET_HEARTBEAT act
constexpr const char* VAULT_RELEASE       = "vault_release";     // "intentoutpoint" ( "address"|"script" )
constexpr const char* VAULT_BUILDCANCEL   = "vault_buildcancel"; // "intentoutpoint": the unsigned CANCEL spend
constexpr const char* SET_SIGNCANCEL      = "set_signcancel";    // "hex": this wallet's cancel-set signatures
constexpr const char* VAULT_SEND          = "vault_send";        // "hex": sign the fee inputs and broadcast

// ── vault_getinfo ─────────────────────────────────────────────────────────────────────────
namespace Info {
    constexpr const char* BRANCH_ID         = "branchid";          // "6d5b7a31"
    constexpr const char* ACTIVATION_HEIGHT = "activationheight";  // -1 when unscheduled
    constexpr const char* ACTIVE            = "active";            // at the next block
    constexpr const char* HEIGHT            = "height";
}

// ── set_getinfo ───────────────────────────────────────────────────────────────────────────
namespace Set {
    constexpr const char* SEATS             = "seats";
    constexpr const char* CANCEL_THRESHOLD  = "cancelthreshold";
    constexpr const char* LIVENESS_WINDOW   = "livenesswindow";
    constexpr const char* MATURITY          = "maturity";
    constexpr const char* MEMBERS           = "members";
    constexpr const char* CURRENT           = "current";
    constexpr const char* DORMANT           = "dormant";           // fewer than cancelthreshold live current members
    constexpr const char* RELEASED          = "released";
    constexpr const char* MEMBER_LIST       = "memberlist";
}

namespace Member {   // set_getinfo.memberlist[]
    constexpr const char* KEY               = "key";
    constexpr const char* STATUS            = "status";            // active | removed | ejected | withdrawn
    constexpr const char* CURRENT           = "current";           // ACTIVE and past maturity
    constexpr const char* LIVE              = "live";              // lastact within the liveness window
    constexpr const char* JOIN_HEIGHT       = "joinheight";
    constexpr const char* LAST_ACT          = "lastact";
    constexpr const char* BOND_OUTPOINT     = "bondoutpoint";
    constexpr const char* BOND_VALUE        = "bondvalue";
    constexpr const char* BOND_LOCKTIME     = "bondlocktime";
    constexpr const char* BOND_FROZEN       = "bondfrozen";
    constexpr const char* WALLET            = "wallet";            // this wallet holds the key

    constexpr const char* STATUS_ACTIVE     = "active";            // value
}

// ── set_heartbeat ─────────────────────────────────────────────────────────────────────────
namespace Heartbeat {
    constexpr const char* TXID              = "txid";
    constexpr const char* MEMBER_KEY        = "memberkey";
}

// ── vault_buildcancel / set_signcancel ────────────────────────────────────────────────────
namespace Cancel {
    constexpr const char* HEX               = "hex";
    constexpr const char* REQUIRED          = "required";          // the cancel set's cancelthreshold
    constexpr const char* CANCEL_SET_ID     = "cancelsetid";
    constexpr const char* DEADLINE          = "deadline";          // the last height a cancel can confirm at
    constexpr const char* COMPLETE          = "complete";          // set_signcancel: enough signatures
    constexpr const char* SIGNATURES        = "signatures";
}

// vault_release before coinHeight + delay: RPC_MISC_ERROR "the intent matures at height h"
constexpr const char* ERR_MATURES_AT      = "matures at height";
// vault_release without a recipient this wallet knows
constexpr const char* ERR_RECIPIENT_UNKNOWN = "recipient script is unknown";
// vault_buildcancel after the intent matured
constexpr const char* ERR_NO_LONGER_CANCELLABLE = "can no longer be cancelled";

}

#endif // VAULTRPC_H
