#ifndef YELLOWBACKRPC_H
#define YELLOWBACKRPC_H

#include <QtGlobal>

// The Yellowback RPC contract, as the wallet depends on it.
//
// Every yed_* method name, every result field and every error identifier the wallet reads is
// declared here and nowhere else, so the wallet can be reconciled against
// ycash-dd/doc/yellowback-rpc.md (rpcversion 1, frozen) in one place. Nothing else crosses the
// node/wallet boundary. The section order below follows that document.

namespace YellowbackRpc {

// The rpcversion this build of the wallet understands. yed_getinfo.rpcversion must equal it.
constexpr int RPC_VERSION = 1;

// ── Methods (node context) ────────────────────────────────────────────────────────────────
constexpr const char* GETINFO             = "yed_getinfo";
constexpr const char* GETSTATS            = "yed_getstats";
constexpr const char* GETPROTECTIONSTATUS = "yed_getprotectionstatus";
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
    constexpr const char* HEIGHT           = "height";         // index height; -1 while empty
    constexpr const char* CHAIN_HEIGHT     = "chainHeight";
    constexpr const char* SYNCED           = "synced";         // index tip == chain tip
    constexpr const char* HEALTHY          = "healthy";
    constexpr const char* UNHEALTHY_REASON = "unhealthyReason";
    constexpr const char* START_HEIGHT     = "startHeight";
    constexpr const char* ANCHOR           = "anchor";
    constexpr const char* ROSTER_INDEX     = "rosterIndex";
    constexpr const char* PARAMS           = "params";
}

// yed_getinfo.params: the node's protocol parameters. The wallet prefers these over the
// compiled-in defaults at the bottom of this file whenever they are present.
namespace Params {
    constexpr const char* MIN_MINT_CENTS   = "minMintCents";
    constexpr const char* MAX_MINT_CENTS   = "maxMintCents";
    constexpr const char* MIN_OUTPUT_CENTS = "minOutputCents";
    constexpr const char* MAX_OUTPUT_CENTS = "maxOutputCents";
    constexpr const char* SUPPLY_CAP_CENTS = "supplyCapCents";
    constexpr const char* TIERS            = "tiers";          // [{tier,blocks,ratioPct}]
    constexpr const char* TIER             = "tier";
    constexpr const char* TIER_BLOCKS      = "blocks";
    constexpr const char* TIER_RATIO_PCT   = "ratioPct";
    constexpr const char* VOL_COOLDOWN     = "volCooldown";
    constexpr const char* PRICE_MAX_AGE    = "priceMaxAge";
    constexpr const char* MINT_WINDOW      = "mintWindow";
    constexpr const char* MINT_EVAL_LAG    = "mintEvalLag";
    constexpr const char* FEE_ZAT          = "feeZat";
}

// ── yed_getstats result ────────────────────────────────────────────────────────────────────
namespace Stats {
    constexpr const char* HEIGHT           = "height";
    constexpr const char* SUPPLY_CENTS     = "supplyCents";
    constexpr const char* COLLATERAL_ZAT   = "collateralZat";
    constexpr const char* ACTIVE_VAULTS    = "activeVaults";
    constexpr const char* VOID_VAULTS      = "voidVaults";
    constexpr const char* SUPPLY_CAP_CENTS = "supplyCapCents"; // always present; 0 = no cap
    constexpr const char* PRICE_MICRO_USD  = "priceMicroUsd";  // null when no price is defined
    constexpr const char* PRICE_HEIGHT     = "priceHeight";    // -1 when no price
    constexpr const char* PRICE_AGE        = "priceAge";       // -1 when no price
    constexpr const char* HEALTH_PCT       = "healthPct";
    constexpr const char* DCA_BPS          = "dcaBps";
    constexpr const char* ERR_BPS          = "errBps";
    constexpr const char* MINT_FROZEN      = "mintFrozen";
    constexpr const char* LAST_BREACH_HEIGHT = "lastBreachHeight";   // -1 when never
    constexpr const char* MINT_FROZEN_UNTIL= "mintFrozenUntil"; // -1 when never breached
}

// ── yed_getprotectionstatus result ─────────────────────────────────────────────────────────
namespace Protection {
    constexpr const char* HEIGHT           = "height";
    constexpr const char* HEALTH_PCT       = "healthPct";
    constexpr const char* DCA              = "dca";            // {bps, band}
    constexpr const char* DCA_BPS          = "bps";
    constexpr const char* DCA_BAND         = "band";           // healthy | warning | critical | emergency
    constexpr const char* ERR              = "err";            // {bps, active, burnMultiplierBps}
    constexpr const char* ERR_BPS          = "bps";
    constexpr const char* ERR_ACTIVE       = "active";
    constexpr const char* ERR_BURN_MULTIPLIER_BPS = "burnMultiplierBps";
    constexpr const char* VOLATILITY       = "volatility";
    constexpr const char* VOL_MINT_FROZEN  = "mintFrozen";
    constexpr const char* VOL_LAST_BREACH_HEIGHT = "lastBreachHeight";
    constexpr const char* VOL_FROZEN_UNTIL = "frozenUntil";    // -1 when never breached
    constexpr const char* VOL_COOLDOWN_BLOCKS = "cooldownBlocks";
    constexpr const char* VOL_PRICE_NOW    = "priceNow";
    constexpr const char* MINTING_ALLOWED  = "mintingAllowed";
}

// ── yed_getbalance result ──────────────────────────────────────────────────────────────────
namespace Balance {
    constexpr const char* CONFIRMED_CENTS   = "confirmedCents";
    constexpr const char* UNCONFIRMED_CENTS = "unconfirmedCents";
    constexpr const char* HEIGHT            = "height";
}

// ── yed_listpositions element ──────────────────────────────────────────────────────────────
namespace Position {
    constexpr const char* VAULT_TXID        = "vaultTxid";
    constexpr const char* STATUS            = "status";        // "ACTIVE" | "VOID" | "CLOSED"
    constexpr const char* MINTED_CENTS      = "mintedCents";
    constexpr const char* COLLATERAL_ZAT    = "collateralZat";
    constexpr const char* LOCK_HEIGHT       = "lockHeight";
    constexpr const char* UNLOCK_HEIGHT     = "unlockHeight";  // same value as lockHeight
    constexpr const char* TIER              = "tier";
    constexpr const char* MINT_HEIGHT       = "mintHeight";
    constexpr const char* ROSTER_INDEX      = "rosterIndex";
    constexpr const char* OWNER_KEY_ID      = "ownerKeyId";
    constexpr const char* REQUIRED_BURN_CENTS = "requiredBurnCents";
    constexpr const char* CAN_REDEEM        = "canRedeem";
    constexpr const char* PENDING           = "pending";       // a yed_redeem is outstanding on the node
    constexpr const char* VOID_REASON       = "voidReason";    // VOID only
    constexpr const char* CLOSE_HEIGHT      = "closeHeight";   // CLOSED only
    constexpr const char* CLOSING_TXID      = "closingTxid";   // CLOSED only
    constexpr const char* BURNED_CENTS      = "burnedCents";   // CLOSED only

    constexpr const char* STATUS_ACTIVE     = "ACTIVE";
    constexpr const char* STATUS_VOID       = "VOID";
    constexpr const char* STATUS_CLOSED     = "CLOSED";
}

// ── yed_listtransactions element ───────────────────────────────────────────────────────────
namespace Transaction {
    constexpr const char* TXID              = "txid";
    constexpr const char* HEIGHT            = "height";        // -1 for an expired row
    constexpr const char* CONFIRMATIONS     = "confirmations";
    constexpr const char* TYPE              = "type";          // mint | send | receive | burn | redeem
    constexpr const char* VERDICT           = "verdict";       // "expired" for an expired row
    constexpr const char* YED_IN            = "yedIn";
    constexpr const char* YED_OUT           = "yedOut";
    constexpr const char* BURNED            = "burned";
    constexpr const char* AMOUNT_CENTS      = "amountCents";
    constexpr const char* EXPIRED           = "expired";

    constexpr const char* TYPE_MINT         = "mint";
    constexpr const char* TYPE_SEND         = "send";
    constexpr const char* TYPE_RECEIVE      = "receive";
    constexpr const char* TYPE_BURN         = "burn";
    constexpr const char* TYPE_REDEEM       = "redeem";
    // Not in the contract's list, but what the node's expired rows carry for a transfer
    // payload (the payload type name); rendered as "Sent" with the expired marker.
    constexpr const char* TYPE_TRANSFER     = "transfer";

    constexpr const char* VERDICT_EXPIRED   = "expired";
}

// ── yed_estimatecollateral result ──────────────────────────────────────────────────────────
namespace Estimate {
    constexpr const char* CENTS             = "cents";
    constexpr const char* TIER              = "tier";
    constexpr const char* RATIO_PCT         = "ratioPct";
    constexpr const char* LOCK_BLOCKS       = "lockBlocks";
    constexpr const char* EVAL_HEIGHT       = "evalHeight";
    constexpr const char* DCA_BPS           = "dcaBps";
    constexpr const char* PRICE_MICRO_USD   = "priceMicroUsd"; // null with error: bad-oracle-price
    constexpr const char* REQUIRED_ZAT      = "requiredZat";   // null when `error` is set
    constexpr const char* REQUIRED          = "required";
    constexpr const char* LOCK_HEIGHT       = "lockHeight";
    constexpr const char* UNLOCK_HEIGHT     = "unlockHeight";  // same value as lockHeight
    constexpr const char* EXPIRY_HEIGHT     = "expiryHeight";
    constexpr const char* ERROR             = "error";         // bad-oracle-price | collateral-out-of-range
}

// ── yed_getroster result ───────────────────────────────────────────────────────────────────
namespace Roster {
    constexpr const char* INDEX             = "index";
    constexpr const char* REVEAL_HEIGHT     = "revealHeight";
    constexpr const char* SCRIPT_HEX        = "scriptHex";
    constexpr const char* ADDRESS           = "address";
    constexpr const char* K                 = "k";
    constexpr const char* N                 = "n";
    constexpr const char* PUBKEYS           = "pubkeys";
    constexpr const char* MINTABLE_INDICES  = "mintableIndices";
    constexpr const char* PREVIOUS          = "previous";
}

// ── yed_mint result ────────────────────────────────────────────────────────────────────────
namespace MintResult {
    constexpr const char* TXID              = "txid";
    constexpr const char* VAULT             = "vault";         // "txid:0"
    constexpr const char* LOCK_HEIGHT       = "lockHeight";
    constexpr const char* EVAL_HEIGHT       = "evalHeight";
    constexpr const char* EXPIRY_HEIGHT     = "expiryHeight";
    constexpr const char* COLLATERAL_ZAT    = "collateralZat";
    constexpr const char* OWNER_KEY_ID      = "ownerKeyId";
    constexpr const char* WARNING           = "warning";
    constexpr const char* FUNDED_FROM       = "fundedFrom";    // "transparent" | "sapling" (plan I2)
    constexpr const char* FROM              = "from";          // the funding address as given ("" = any transparent)
}

// ── yed_send result ────────────────────────────────────────────────────────────────────────
namespace SendResult {
    constexpr const char* TXID              = "txid";
    constexpr const char* CHANGE_CENTS      = "changeCents";
    constexpr const char* EXPIRY_HEIGHT     = "expiryHeight";
}

// ── yed_redeem result ──────────────────────────────────────────────────────────────────────
namespace RedeemResult {
    constexpr const char* HEX               = "hex";           // owner-signed
    constexpr const char* VAULT             = "vault";
    constexpr const char* ROSTER            = "roster";        // {index,k,n,pubkeys[]} (Roster::*)
    constexpr const char* REQUIRED_BURN_CENTS = "requiredBurnCents";
    constexpr const char* BURN_CENTS        = "burnCents";
    constexpr const char* CHANGE_CENTS      = "changeCents";
    constexpr const char* EXPIRY_HEIGHT     = "expiryHeight";
    constexpr const char* DEADLINE_HEIGHT   = "deadlineHeight"; // submit at or before this height
    constexpr const char* COLLATERAL_TO     = "collateralTo";  // where the collateral goes (plan I2)
    constexpr const char* SHIELDED          = "shielded";      // true when it is a Sapling output
}

// ── yed_submitredeem result ────────────────────────────────────────────────────────────────
namespace SubmitResult {
    constexpr const char* TXID              = "txid";
    constexpr const char* QUORUM_SIGNATURES = "quorumSignatures";
}

// ── yed_abortredeem result ─────────────────────────────────────────────────────────────────
namespace AbortResult {
    constexpr const char* ABORTED           = "aborted";
}

// ── yed_validateaddress result ─────────────────────────────────────────────────────────────
namespace ValidateAddress {
    constexpr const char* ISVALID           = "isvalid";
    constexpr const char* ADDRESS           = "address";
    constexpr const char* KEYID             = "keyid";
    constexpr const char* ISMINE            = "ismine";
    constexpr const char* TRANSPARENT_ADDRESS = "transparentAddress";
}

// ── Error identifiers the wallet recognises (matched as substrings of error.message) ──────
// Node error strings are stable identifiers and are always shown verbatim; these are only
// the ones the wallet branches on.
namespace Errors {
    // JSON-RPC: the node has no yed_* methods (experimentalfeatures / yellowback not set)
    constexpr const char* METHOD_NOT_FOUND  = "Method not found";
    constexpr int         METHOD_NOT_FOUND_CODE = -32601;
    // Every command but yed_getinfo while the index is unhealthy (code -1)
    constexpr const char* INDEX_UNHEALTHY   = "yellowback index unhealthy";
    // yed_send: the change output would be below the minimum output (rule C20)
    constexpr const char* CHANGE_FLOOR      = "C20";
    // yed_mint / yed_send / yed_redeem on an encrypted, locked wallet
    constexpr const char* WALLET_LOCKED     = "walletpassphrase";
    // yed_submitredeem: the co-signed transaction differs or a signature fails to verify
    constexpr const char* SUB_1             = "SUB-1";
    // Co-signer refusals (yed_cosignredeem, and the operator /cosign relaying it) begin with
    // the rule id RED-0 … RED-8 and end with this suffix when a retry after the next block
    // may succeed.
    constexpr const char* TRANSIENT_SUFFIX  = "(transient)";
    constexpr const char* RULE_PREFIX       = "RED-";
}

// ── Operator co-sign endpoint (coordinator, not the node) ────────────────────────────────
// POST <endpoint>/cosign with a JSON body {"hex": "<owner-signed or partially co-signed hex>"};
// success 2xx {"hex", "quorumSignatures", "k", "complete"}; refusal 4xx {"error", "transient"}
// (409 for a co-signer refusal, 429 rate-limited with transient: true).
namespace Cosign {
    constexpr const char* PATH              = "/cosign";
    constexpr const char* REQ_HEX           = "hex";
    constexpr const char* RESP_HEX          = "hex";
    constexpr const char* RESP_QUORUM_SIGNATURES = "quorumSignatures";
    constexpr const char* RESP_K            = "k";
    constexpr const char* RESP_COMPLETE     = "complete";
    constexpr const char* RESP_ERROR        = "error";
    constexpr const char* RESP_TRANSIENT    = "transient";
}

// ── Protocol constants (plan §3.1) ────────────────────────────────────────────────────────
// Defaults only: the amounts and windows are overridden by yed_getinfo.params once the node
// has answered (YellowbackController::param*). SECONDS_PER_BLOCK is display-only.
constexpr int    SECONDS_PER_BLOCK   = 75;
constexpr int    MINT_WINDOW         = 40;   // nExpiryHeight = indexTip + 40
constexpr int    EXPIRING_SOON       = 3;    // the mempool refuses within 3 blocks of expiry
// yed_redeem.deadlineHeight = expiryHeight - EXPIRING_SOON - 1: the last height at which
// yed_submitredeem is accepted. 36 blocks from the height the redemption was built at.
constexpr int    REDEEM_DEADLINE     = MINT_WINDOW - EXPIRING_SOON - 1;
constexpr int    MINT_EVAL_LAG       = 2;
constexpr qint64 MIN_MINT_CENTS      = 10000;     // $100
constexpr qint64 MAX_MINT_CENTS      = 1000000;   // $10,000
constexpr qint64 MIN_OUTPUT_CENTS    = 100;       // $1 change floor (rule C20)
constexpr int    TIER_COUNT          = 5;

}

#endif // YELLOWBACKRPC_H
