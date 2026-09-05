#ifndef YELLOWBACKRPC_H
#define YELLOWBACKRPC_H

#include <QtGlobal>

// The Yellowback RPC contract, as the wallet depends on it.
//
// Every yed_* method name, every result field and every error identifier the wallet reads is
// declared here and nowhere else, so the wallet can be reconciled against
// ycash-dd/doc/yellowback-rpc.md in one place when that document is frozen (plan §4.4, §4.7
// cross-cutting rule 1). Nothing else crosses the node/wallet boundary.

namespace YellowbackRpc {

// The rpcversion this build of the wallet understands. yed_getinfo.rpcversion must equal it.
constexpr int RPC_VERSION = 1;

// ── Methods (node context) ────────────────────────────────────────────────────────────────
constexpr const char* GETINFO             = "yed_getinfo";
constexpr const char* GETSTATS            = "yed_getstats";
constexpr const char* GETROSTER           = "yed_getroster";
constexpr const char* GETVAULT            = "yed_getvault";
constexpr const char* GETTXINFO           = "yed_gettxinfo";
constexpr const char* ESTIMATECOLLATERAL  = "yed_estimatecollateral";

// ── Methods (wallet context) ──────────────────────────────────────────────────────────────
constexpr const char* GETNEWADDRESS       = "yed_getnewaddress";
constexpr const char* VALIDATEADDRESS     = "yed_validateaddress";
constexpr const char* GETBALANCE          = "yed_getbalance";
constexpr const char* MINT                = "yed_mint";
constexpr const char* SEND                = "yed_send";
constexpr const char* REDEEM              = "yed_redeem";
constexpr const char* SUBMITREDEEM        = "yed_submitredeem";
constexpr const char* ABORTREDEEM         = "yed_abortredeem";
constexpr const char* LISTPOSITIONS       = "yed_listpositions";
constexpr const char* LISTTRANSACTIONS    = "yed_listtransactions";

// ── yed_getinfo result ─────────────────────────────────────────────────────────────────────
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

// ── yed_getstats result ────────────────────────────────────────────────────────────────────
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

// ── yed_getbalance result ──────────────────────────────────────────────────────────────────
namespace Balance {
    constexpr const char* CONFIRMED_CENTS   = "confirmedCents";
    constexpr const char* UNCONFIRMED_CENTS = "unconfirmedCents";
}

// ── yed_listpositions element ──────────────────────────────────────────────────────────────
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

// ── yed_listtransactions element ───────────────────────────────────────────────────────────
namespace Transaction {
    constexpr const char* TXID              = "txid";
    constexpr const char* HEIGHT            = "height";
    constexpr const char* CONFIRMATIONS     = "confirmations";
    constexpr const char* TYPE              = "type";          // mint | send | receive | burn | redeem
    constexpr const char* VERDICT           = "verdict";
    constexpr const char* YD_IN             = "yedIn";
    constexpr const char* YD_OUT            = "yedOut";
    constexpr const char* BURNED            = "burned";
    constexpr const char* AMOUNT_CENTS      = "amountCents";
    constexpr const char* EXPIRED           = "expired";

    constexpr const char* TYPE_MINT         = "mint";
    constexpr const char* TYPE_SEND         = "send";
    constexpr const char* TYPE_RECEIVE      = "receive";
    constexpr const char* TYPE_BURN         = "burn";
    constexpr const char* TYPE_REDEEM       = "redeem";
}

// ── yed_estimatecollateral result ──────────────────────────────────────────────────────────
namespace Estimate {
    constexpr const char* REQUIRED_ZAT      = "requiredZat";
    constexpr const char* PRICE_MICRO_USD   = "priceMicroUsd";
    constexpr const char* DCA_BPS           = "dcaBps";
    constexpr const char* RATIO_PCT         = "ratioPct";
    constexpr const char* LOCK_BLOCKS       = "lockBlocks";
    constexpr const char* UNLOCK_HEIGHT     = "unlockHeight";
}

// ── yed_getroster result ───────────────────────────────────────────────────────────────────
namespace Roster {
    constexpr const char* K                 = "k";
    constexpr const char* N                 = "n";
    constexpr const char* PUBKEYS           = "pubkeys";
    constexpr const char* SCRIPT_HEX        = "scriptHex";
    constexpr const char* ADDRESS           = "address";
    constexpr const char* INDEX             = "index";
    constexpr const char* PREVIOUS          = "previous";
}

// ── yed_mint result ────────────────────────────────────────────────────────────────────────
namespace MintResult {
    constexpr const char* TXID              = "txid";
    constexpr const char* VAULT             = "vault";
    constexpr const char* LOCK_HEIGHT       = "lockHeight";
    constexpr const char* COLLATERAL_ZAT    = "collateralZat";
}

// ── yed_send / yed_submitredeem result ──────────────────────────────────────────────────────
namespace SendResult {
    constexpr const char* TXID              = "txid";
}

// ── yed_redeem result ──────────────────────────────────────────────────────────────────────
namespace RedeemResult {
    constexpr const char* HEX               = "hex";
    constexpr const char* VAULT             = "vault";
    constexpr const char* ROSTER            = "roster";
    constexpr const char* REQUIRED_BURN_CENTS = "requiredBurnCents";
    constexpr const char* EXPIRY_HEIGHT     = "expiryHeight";
}

// ── yed_abortredeem result ─────────────────────────────────────────────────────────────────
namespace AbortResult {
    constexpr const char* ABORTED           = "aborted";
}

// ── yed_validateaddress result ─────────────────────────────────────────────────────────────
namespace ValidateAddress {
    constexpr const char* ISVALID           = "isvalid";
    constexpr const char* ISMINE            = "ismine";
    constexpr const char* ADDRESS           = "address";
}

// ── Error identifiers the wallet recognises (matched as substrings of error.message) ──────
namespace Errors {
    // JSON-RPC: the node has no yed_* methods (experimentalfeatures / yellowback not set)
    constexpr const char* METHOD_NOT_FOUND  = "Method not found";
    constexpr int         METHOD_NOT_FOUND_CODE = -32601;
    // yed_send: the change output would be below MIN_OUTPUT (plan C20)
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

#endif // YELLOWBACKRPC_H
