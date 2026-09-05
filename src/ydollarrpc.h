#ifndef YDOLLARRPC_H
#define YDOLLARRPC_H

#include <QtGlobal>

// The YDollar RPC contract, as the wallet depends on it.
//
// Every yd_* method name, every result field and every error identifier the wallet reads is
// declared here and nowhere else, so the wallet can be reconciled against
// ycash-dd/doc/ydollar-rpc.md in one place when that document is frozen (plan §4.4, §4.7
// cross-cutting rule 1). Nothing else crosses the node/wallet boundary.

namespace YDollarRpc {

// The rpcversion this build of the wallet understands. yd_getinfo.rpcversion must equal it.
constexpr int RPC_VERSION = 1;

// ── Methods (node context) ────────────────────────────────────────────────────────────────
constexpr const char* GETINFO             = "yd_getinfo";
constexpr const char* GETSTATS            = "yd_getstats";
constexpr const char* GETROSTER           = "yd_getroster";
constexpr const char* GETVAULT            = "yd_getvault";
constexpr const char* GETTXINFO           = "yd_gettxinfo";
constexpr const char* ESTIMATECOLLATERAL  = "yd_estimatecollateral";

// ── Methods (wallet context) ──────────────────────────────────────────────────────────────
constexpr const char* GETNEWADDRESS       = "yd_getnewaddress";
constexpr const char* VALIDATEADDRESS     = "yd_validateaddress";
constexpr const char* GETBALANCE          = "yd_getbalance";
constexpr const char* MINT                = "yd_mint";
constexpr const char* SEND                = "yd_send";
constexpr const char* REDEEM              = "yd_redeem";
constexpr const char* SUBMITREDEEM        = "yd_submitredeem";
constexpr const char* ABORTREDEEM         = "yd_abortredeem";
constexpr const char* LISTPOSITIONS       = "yd_listpositions";
constexpr const char* LISTTRANSACTIONS    = "yd_listtransactions";

// ── yd_getinfo result ─────────────────────────────────────────────────────────────────────
namespace Info {
    constexpr const char* ENABLED          = "enabled";
    constexpr const char* RPCVERSION       = "rpcversion";
    constexpr const char* NETWORK          = "network";        // "main" | "test" | "regtest"
    constexpr const char* HEIGHT           = "height";         // index height
    constexpr const char* SYNCED           = "synced";
    constexpr const char* HEALTHY          = "healthy";
    constexpr const char* UNHEALTHY_REASON = "unhealthyReason";
    constexpr const char* START_HEIGHT     = "startHeight";
    constexpr const char* ANCHOR           = "anchor";
    constexpr const char* ROSTER_INDEX     = "rosterIndex";
}

// ── yd_getstats result ────────────────────────────────────────────────────────────────────
namespace Stats {
    constexpr const char* SUPPLY_CENTS     = "supplyCents";
    constexpr const char* COLLATERAL_ZAT   = "collateralZat";
    constexpr const char* ACTIVE_VAULTS    = "activeVaults";
    constexpr const char* VOID_VAULTS      = "voidVaults";
    constexpr const char* PRICE_MICRO_USD  = "priceMicroUsd";  // null when no fresh price
    constexpr const char* PRICE_HEIGHT     = "priceHeight";
    constexpr const char* PRICE_AGE        = "priceAge";
    constexpr const char* HEALTH_PCT       = "healthPct";
    constexpr const char* DCA_BPS          = "dcaBps";
    constexpr const char* ERR_BPS          = "errBps";
    constexpr const char* MINT_FROZEN      = "mintFrozen";
    constexpr const char* MINT_FROZEN_UNTIL= "mintFrozenUntil";
    constexpr const char* SUPPLY_CAP_CENTS = "supplyCapCents"; // optional; absent or 0 = no cap
}

// ── yd_getbalance result ──────────────────────────────────────────────────────────────────
namespace Balance {
    constexpr const char* CONFIRMED_CENTS   = "confirmedCents";
    constexpr const char* UNCONFIRMED_CENTS = "unconfirmedCents";
}

// ── yd_listpositions element ──────────────────────────────────────────────────────────────
namespace Position {
    constexpr const char* VAULT_TXID        = "vaultTxid";
    constexpr const char* STATUS            = "status";        // "ACTIVE" | "VOID" | "CLOSED"
    constexpr const char* MINTED_CENTS      = "mintedCents";
    constexpr const char* COLLATERAL_ZAT    = "collateralZat";
    constexpr const char* LOCK_HEIGHT       = "lockHeight";
    constexpr const char* TIER              = "tier";
    constexpr const char* ROSTER_INDEX      = "rosterIndex";
    constexpr const char* CAN_REDEEM        = "canRedeem";
    constexpr const char* REQUIRED_BURN_CENTS = "requiredBurnCents";
    constexpr const char* UNLOCK_HEIGHT     = "unlockHeight";
    constexpr const char* OWNER_KEY_ID      = "ownerKeyId";

    constexpr const char* STATUS_ACTIVE     = "ACTIVE";
    constexpr const char* STATUS_VOID       = "VOID";
    constexpr const char* STATUS_CLOSED     = "CLOSED";
}

// ── yd_listtransactions element ───────────────────────────────────────────────────────────
namespace Transaction {
    constexpr const char* TXID              = "txid";
    constexpr const char* HEIGHT            = "height";
    constexpr const char* CONFIRMATIONS     = "confirmations";
    constexpr const char* TYPE              = "type";          // mint | send | receive | burn | redeem
    constexpr const char* VERDICT           = "verdict";
    constexpr const char* YD_IN             = "ydIn";
    constexpr const char* YD_OUT            = "ydOut";
    constexpr const char* BURNED            = "burned";
    constexpr const char* AMOUNT_CENTS      = "amountCents";
    constexpr const char* EXPIRED           = "expired";

    constexpr const char* TYPE_MINT         = "mint";
    constexpr const char* TYPE_SEND         = "send";
    constexpr const char* TYPE_RECEIVE      = "receive";
    constexpr const char* TYPE_BURN         = "burn";
    constexpr const char* TYPE_REDEEM       = "redeem";
}

// ── yd_estimatecollateral result ──────────────────────────────────────────────────────────
namespace Estimate {
    constexpr const char* REQUIRED_ZAT      = "requiredZat";
    constexpr const char* PRICE_MICRO_USD   = "priceMicroUsd";
    constexpr const char* DCA_BPS           = "dcaBps";
    constexpr const char* RATIO_PCT         = "ratioPct";
    constexpr const char* LOCK_BLOCKS       = "lockBlocks";
    constexpr const char* UNLOCK_HEIGHT     = "unlockHeight";
}

// ── yd_getroster result ───────────────────────────────────────────────────────────────────
namespace Roster {
    constexpr const char* K                 = "k";
    constexpr const char* N                 = "n";
    constexpr const char* PUBKEYS           = "pubkeys";
    constexpr const char* SCRIPT_HEX        = "scriptHex";
    constexpr const char* ADDRESS           = "address";
    constexpr const char* INDEX             = "index";
    constexpr const char* PREVIOUS          = "previous";
}

// ── yd_mint result ────────────────────────────────────────────────────────────────────────
namespace MintResult {
    constexpr const char* TXID              = "txid";
    constexpr const char* VAULT             = "vault";
    constexpr const char* LOCK_HEIGHT       = "lockHeight";
    constexpr const char* COLLATERAL_ZAT    = "collateralZat";
}

// ── yd_send / yd_submitredeem result ──────────────────────────────────────────────────────
namespace SendResult {
    constexpr const char* TXID              = "txid";
}

// ── yd_redeem result ──────────────────────────────────────────────────────────────────────
namespace RedeemResult {
    constexpr const char* HEX               = "hex";
    constexpr const char* VAULT             = "vault";
    constexpr const char* ROSTER            = "roster";
    constexpr const char* REQUIRED_BURN_CENTS = "requiredBurnCents";
    constexpr const char* EXPIRY_HEIGHT     = "expiryHeight";
}

// ── yd_abortredeem result ─────────────────────────────────────────────────────────────────
namespace AbortResult {
    constexpr const char* ABORTED           = "aborted";
}

// ── yd_validateaddress result ─────────────────────────────────────────────────────────────
namespace ValidateAddress {
    constexpr const char* ISVALID           = "isvalid";
    constexpr const char* ISMINE            = "ismine";
    constexpr const char* ADDRESS           = "address";
}

// ── Error identifiers the wallet recognises (matched as substrings of error.message) ──────
namespace Errors {
    // JSON-RPC: the node has no yd_* methods (experimentalfeatures / ydollar not set)
    constexpr const char* METHOD_NOT_FOUND  = "Method not found";
    constexpr int         METHOD_NOT_FOUND_CODE = -32601;
    // yd_send: the change output would be below MIN_OUTPUT (plan C20)
    constexpr const char* CHANGE_FLOOR      = "change";
    // Co-signer refusals that are transient (plan E2): retried after the next block
    constexpr const char* RED_0             = "RED-0";
    constexpr const char* RED_2             = "RED-2";
}

// ── Operator /cosign endpoint (HTTPS, plan §5 and D16) ────────────────────────────────────
// POST <endpoint>/cosign with a JSON body {"hex": "<owner-signed hex>"}; a 200 reply carries
// {"hex": "<hex with one more signature>"}; any other status carries {"error": "<RED-n ...>",
// "transient": bool}. A plain-text hex body is also accepted for a 200 reply.
namespace Cosign {
    constexpr const char* PATH              = "/cosign";
    constexpr const char* REQ_HEX           = "hex";
    constexpr const char* RESP_HEX          = "hex";
    constexpr const char* RESP_ERROR        = "error";
    constexpr const char* RESP_TRANSIENT    = "transient";
    constexpr const char* RESP_SIGNATURES   = "signatures";
}

// ── Protocol constants the wallet displays (plan §3.1) ────────────────────────────────────
constexpr int    SECONDS_PER_BLOCK   = 75;
constexpr int    MINT_WINDOW         = 40;   // nExpiryHeight = indexTip + 40
constexpr int    EXPIRING_SOON       = 3;    // mempool refuses within 3 blocks of expiry
constexpr int    REDEEM_DEADLINE     = MINT_WINDOW - EXPIRING_SOON;  // 36 blocks (plan B24)
constexpr int    MINT_EVAL_LAG       = 2;
constexpr qint64 MIN_MINT_CENTS      = 10000;     // $100
constexpr qint64 MAX_MINT_CENTS      = 1000000;   // $10,000
constexpr qint64 MIN_OUTPUT_CENTS    = 100;       // $1 change floor (plan C20)
constexpr int    TIER_COUNT          = 5;

}

#endif // YDOLLARRPC_H
