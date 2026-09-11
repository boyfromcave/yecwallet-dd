#ifndef YELLOWBACKRPC_H
#define YELLOWBACKRPC_H

#include <QtGlobal>

// The Yellowback RPC contract (rpcversion 2), as the wallet depends on it.
//
// Every yed_* method name, every result field and every error identifier the wallet reads is
// declared here and nowhere else, so the wallet can be reconciled against
// ycash-dd/doc/yellowback-rpc.md in one place. Nothing else crosses the node/wallet boundary.
//
// tests/check-rpc-contract.py (the `wallet` CI job, plan P7) parses this file: every namespace
// carries a `// contract: <command>[.<path>][[]]` marker naming the JSON object its constants
// are keys of in docs/yellowback-rpc-contract.json (`[]` = the example row of a list result);
// a constant marked `// value` is a field *value* (a status or type name), not a key; the
// method constants must be top-level commands of that file; the Errors namespace's identifiers
// must be listed under `errors`; and RPC_VERSION must equal the file's `rpcversion`.

namespace YellowbackRpc {

// The rpcversion this build of the wallet understands. yed_getinfo.rpcversion must equal it
// (bumped to 2 in Phase 7b-a's first commit, N27).
constexpr int RPC_VERSION = 2;

// ── Methods (node context) ────────────────────────────────────────────────────────────────
constexpr const char* GETINFO             = "yed_getinfo";
constexpr const char* GETSTATS            = "yed_getstats";
constexpr const char* GETACTIVATION       = "yed_getactivation";
constexpr const char* GETVAULT            = "yed_getvault";
constexpr const char* LISTCLAIMABLE       = "yed_listclaimable";
constexpr const char* GETTXINFO           = "yed_gettxinfo";
constexpr const char* ESTIMATECOLLATERAL  = "yed_estimatecollateral";

// ── Methods (wallet context) ──────────────────────────────────────────────────────────────
constexpr const char* GETNEWADDRESS       = "yed_getnewaddress";
constexpr const char* VALIDATEADDRESS     = "yed_validateaddress";
constexpr const char* GETBALANCE          = "yed_getbalance";
constexpr const char* MINT                = "yed_mint";
constexpr const char* SEND                = "yed_send";
constexpr const char* REDEEM              = "yed_redeem";
constexpr const char* CLAIM               = "yed_claim";
constexpr const char* SWEEP               = "yed_sweep";
constexpr const char* LISTPOSITIONS       = "yed_listpositions";
constexpr const char* LISTTRANSACTIONS    = "yed_listtransactions";

// ── yed_getinfo result ─────────────────────────────────────────────────────────────────────
namespace Info {   // contract: yed_getinfo
    constexpr const char* RPCVERSION        = "rpcversion";
    constexpr const char* ENABLED           = "enabled";
    constexpr const char* NETWORK           = "network";        // "main" | "test" | "regtest"
    constexpr const char* HEIGHT            = "height";         // index tip; -1 while empty
    constexpr const char* CHAIN_HEIGHT      = "chainHeight";    // v2 has no `synced`: synced == (height == chainHeight)
    constexpr const char* START_HEIGHT      = "startHeight";
    constexpr const char* HEALTHY           = "healthy";
    constexpr const char* UNHEALTHY_REASON  = "unhealthyReason";
    constexpr const char* ENFORCING         = "enforcing";      // false under kill switch, valve, sunset, unhealthy
    constexpr const char* VALVE_TRIPPED     = "valveTripped";   // ACT-7, L7: restart the node to re-arm
    constexpr const char* SUNSET            = "sunset";         // L8: this release's enforcement has ended
    constexpr const char* REJECTED_BLOCKS   = "rejectedBlocks";
    constexpr const char* SUPPRESSED_BLOCKS = "suppressedBlocks";   // L11: information, not a warning
    constexpr const char* TEMPLATE_POLICY   = "templatePolicy";
    constexpr const char* ABANDONED         = "abandoned";      // L10 predicate
    constexpr const char* ACTIVATION        = "activation";
    constexpr const char* MINER             = "miner";
    constexpr const char* PARAMS            = "params";
}

namespace Activation {   // contract: yed_getinfo.activation
    constexpr const char* STATUS            = "status";         // signaling | locked_in | active
    constexpr const char* LOCK_IN_HEIGHT    = "lockInHeight";
    constexpr const char* ACTIVATE_HEIGHT   = "activateHeight";
    constexpr const char* SIGNAL_COUNT      = "signalCount";
    constexpr const char* WINDOW            = "window";

    constexpr const char* STATUS_SIGNALING  = "signaling";      // value
    constexpr const char* STATUS_LOCKED_IN  = "locked_in";      // value
    constexpr const char* STATUS_ACTIVE     = "active";         // value
}

// The connected node's own mining state (only meaningful when it is a pool node).
namespace Miner {   // contract: yed_getinfo.miner
    constexpr const char* PAYOUT_ADDRESS    = "payoutAddress";  // null when the node has no payout key
    constexpr const char* SIGNAL            = "signal";
    constexpr const char* QUOTE_KIND        = "quoteKind";      // quote | signal | none
    constexpr const char* QUOTE_AGE_SECONDS = "quoteAgeSeconds"; // null when no quote is held
    constexpr const char* REGISTERED        = "registered";
    constexpr const char* ELIGIBLE          = "eligible";

    constexpr const char* KIND_QUOTE        = "quote";          // value
    constexpr const char* KIND_SIGNAL       = "signal";         // value
    constexpr const char* KIND_NONE         = "none";           // value
}

// yed_getinfo.params: the network's protocol parameters (§3.1). Displayed, never configurable.
namespace Params {   // contract: yed_getinfo.params
    constexpr const char* START_HEIGHT        = "startHeight";
    constexpr const char* ENFORCE_UNTIL_HEIGHT= "enforceUntilHeight";   // 0 = none
    constexpr const char* SIGMA_REF_BPS       = "sigmaRefBps";
    constexpr const char* SUPPLY_CAP_BPS      = "supplyCapBps";
    constexpr const char* REF_LAG             = "refLag";
    constexpr const char* REF_WINDOW          = "refWindow";
    constexpr const char* GRACE               = "grace";
    constexpr const char* PAYEE_WINDOW        = "payeeWindow";
    constexpr const char* FEE_MIN_ZAT         = "feeMinZat";
    constexpr const char* FEE_BPS             = "feeBps";
    constexpr const char* TOKEN_VALUE_ZAT     = "tokenValueZat";
    constexpr const char* FEE_ZAT             = "feeZat";       // the network fee, not the enforcement fee
    constexpr const char* VALVE_BLOCKS        = "valveBlocks";
    constexpr const char* ABANDON_BLOCKS      = "abandonBlocks";
    constexpr const char* WINDOWS             = "windows";
    constexpr const char* MIN_FILL            = "minFill";
    constexpr const char* CLASSES             = "classes";
    constexpr const char* POLICY              = "policy";
}

namespace ParamClass {   // contract: yed_getinfo.params.classes[]
    constexpr const char* CLASS               = "class";        // "A" | "B" | "C"
    constexpr const char* MIN_BLOCKS          = "minBlocks";
    constexpr const char* MAX_BLOCKS          = "maxBlocks";
    constexpr const char* BASE_RATIO_BPS      = "baseRatioBps";
}

// ── yed_getstats result ────────────────────────────────────────────────────────────────────
namespace Stats {   // contract: yed_getstats
    constexpr const char* HEIGHT            = "height";
    constexpr const char* SUPPLY_CENTS      = "supplyCents";
    constexpr const char* COLLATERAL_ZAT    = "collateralZat";
    constexpr const char* ACTIVE_VAULTS     = "activeVaults";
    constexpr const char* VOID_VAULTS       = "voidVaults";
    constexpr const char* CLOSED_VAULTS     = "closedVaults";
    constexpr const char* CLAIMED_VAULTS    = "claimedVaults";
    constexpr const char* UNBACKED_CENTS    = "unbackedCents";
    constexpr const char* ISSUED_ZAT        = "issuedZat";
    constexpr const char* P_FAST            = "pFast";          // null when undefined
    constexpr const char* P_MID             = "pMid";           // null when undefined
    constexpr const char* P_SLOW            = "pSlow";          // null when undefined
    constexpr const char* P_MINT            = "pMint";          // null when undefined
    constexpr const char* P_CLAIM           = "pClaim";         // null when undefined
    constexpr const char* SIGMA_MULT_BPS    = "sigmaMultBps";
    constexpr const char* GLOBAL_RATIO_BPS  = "globalRatioBps"; // null when no supply
    constexpr const char* SUPPLY_CAP_CENTS  = "supplyCapCents"; // null when no cap
    constexpr const char* HALT_MASK         = "haltMask";       // [names]; empty when minting is open
    constexpr const char* MINTING_ALLOWED   = "mintingAllowed";

    // haltMask names (§3.6)
    constexpr const char* HALT_NOT_ACTIVE   = "NOT_ACTIVE";     // value
    constexpr const char* HALT_NO_PRICE     = "NO_PRICE";       // value
    constexpr const char* HALT_PARTICIPATION= "PARTICIPATION";  // value
    constexpr const char* HALT_GLOBAL_RATIO = "GLOBAL_RATIO";   // value
    constexpr const char* HALT_DIVERGENCE   = "DIVERGENCE";     // value
    constexpr const char* HALT_ENFORCEMENT  = "ENFORCEMENT";    // value
}

// ── yed_getactivation result ───────────────────────────────────────────────────────────────
namespace ActivationInfo {   // contract: yed_getactivation
    constexpr const char* STATUS               = "status";
    constexpr const char* LOCK_IN_HEIGHT       = "lockInHeight";
    constexpr const char* ACTIVATE_HEIGHT      = "activateHeight";
    constexpr const char* SIGNAL_COUNT         = "signalCount";
    constexpr const char* WINDOW               = "window";
    constexpr const char* THRESHOLD            = "threshold";
    constexpr const char* PARTICIPATION_FLOOR  = "participationFloor";
    constexpr const char* ENFORCEMENT_FLOOR    = "enforcementFloor";
    constexpr const char* ENFORCEMENT_RESUME   = "enforcementResume";
    constexpr const char* MINT_HALTED          = "mintHalted";
    constexpr const char* ENFORCEMENT_SUSPENDED= "enforcementSuspended";
    constexpr const char* ENFORCING            = "enforcing";
    constexpr const char* VALVE_TRIPPED        = "valveTripped";
    constexpr const char* SUNSET               = "sunset";
    constexpr const char* ENFORCE_UNTIL_HEIGHT = "enforceUntilHeight";
}

// ── yed_getbalance result ──────────────────────────────────────────────────────────────────
namespace Balance {   // contract: yed_getbalance
    constexpr const char* CONFIRMED_CENTS   = "confirmedCents";
    constexpr const char* UNCONFIRMED_CENTS = "unconfirmedCents";
    constexpr const char* HEIGHT            = "height";
}

// ── yed_getvault result / yed_listpositions element ────────────────────────────────────────
// yed_listpositions rows are yed_getvault rows plus the three can* flags; both are read with
// the same constants (the checker verifies them against yed_listpositions[], a superset).
namespace Position {   // contract: yed_listpositions[]
    constexpr const char* TXID              = "txid";
    constexpr const char* VOUT              = "vout";
    constexpr const char* STATUS            = "status";        // ACTIVE | VOID | CLOSED | CLAIMED
    constexpr const char* OWNER_PUBKEY      = "ownerPubKey";
    constexpr const char* OWNER_KEY_ID      = "ownerKeyId";
    constexpr const char* OWNER_ADDRESS     = "ownerAddress";
    constexpr const char* TERM_CLASS        = "termClass";     // "A" | "B" | "C"
    constexpr const char* LOCK_HEIGHT       = "lockHeight";
    constexpr const char* CLAIM_HEIGHT      = "claimHeight";
    constexpr const char* COLLATERAL_ZAT    = "collateralZat";
    constexpr const char* COLLATERAL        = "collateral";    // decimal YEC twin
    constexpr const char* MINTED_CENTS      = "mintedCents";
    constexpr const char* MINT_HEIGHT       = "mintHeight";
    constexpr const char* REF_HEIGHT        = "refHeight";
    constexpr const char* FEE_PAID_ZAT      = "feePaidZat";
    constexpr const char* CLOSE_HEIGHT      = "closeHeight";   // null until CLOSED/CLAIMED
    constexpr const char* CLOSING_TXID      = "closingTxid";   // "" until CLOSED/CLAIMED
    constexpr const char* BURNED_CENTS      = "burnedCents";
    constexpr const char* UNBACKED          = "unbacked";      // closed without its burn (a sweep)
    constexpr const char* CLAIMABLE         = "claimable";
    constexpr const char* UNDERWATER_AT     = "underwaterAt";  // µUSD; null for VOID
    constexpr const char* VOID_REASON       = "voidReason";    // "" unless VOID
    constexpr const char* SWEEP_BEFORE      = "sweepBefore";   // optional: VOID, or ACTIVE under abandonment
    constexpr const char* CAN_REDEEM        = "canRedeem";     // listpositions only
    constexpr const char* CAN_CLAIM         = "canClaim";      // listpositions only
    constexpr const char* CAN_SWEEP         = "canSweep";      // listpositions only

    constexpr const char* STATUS_ACTIVE     = "ACTIVE";        // value
    constexpr const char* STATUS_VOID       = "VOID";          // value
    constexpr const char* STATUS_CLOSED     = "CLOSED";        // value
    constexpr const char* STATUS_CLAIMED    = "CLAIMED";       // value
}

// ── yed_listclaimable element ──────────────────────────────────────────────────────────────
namespace Claimable {   // contract: yed_listclaimable[]
    constexpr const char* VAULT             = "vault";         // "txid:0"
    constexpr const char* OWNER_ADDRESS     = "ownerAddress";
    constexpr const char* COLLATERAL_ZAT    = "collateralZat";
    constexpr const char* MINTED_CENTS      = "mintedCents";   // the burn a claim must carry
    constexpr const char* FEE_ZAT           = "feeZat";
    constexpr const char* CLAIM_HEIGHT      = "claimHeight";
    constexpr const char* UNDERWATER_AT     = "underwaterAt";
    constexpr const char* P_CLAIM           = "pClaim";
}

// ── yed_listtransactions element ───────────────────────────────────────────────────────────
namespace Transaction {   // contract: yed_listtransactions[]
    constexpr const char* TXID              = "txid";
    constexpr const char* HEIGHT            = "height";        // -1 for an expired row
    constexpr const char* CONFIRMATIONS     = "confirmations";
    constexpr const char* TYPE              = "type";
    constexpr const char* VERDICT           = "verdict";       // "expired" for an expired row
    constexpr const char* PATH              = "path";          // owner | claim | ""
    constexpr const char* YED_IN            = "yedIn";
    constexpr const char* YED_OUT           = "yedOut";
    constexpr const char* BURNED            = "burned";
    constexpr const char* AMOUNT_CENTS      = "amountCents";
    constexpr const char* FEE_ZAT           = "feeZat";
    constexpr const char* PAYEE             = "payee";         // null where there is no fee output
    constexpr const char* UNBACKED          = "unbacked";
    constexpr const char* EXPIRED           = "expired";

    constexpr const char* TYPE_MINT         = "mint";          // value
    constexpr const char* TYPE_SEND         = "send";          // value
    constexpr const char* TYPE_RECEIVE      = "receive";       // value
    constexpr const char* TYPE_BURN         = "burn";          // value
    constexpr const char* TYPE_REDEEM       = "redeem";        // value
    constexpr const char* TYPE_CLAIM        = "claim";         // value
    constexpr const char* TYPE_CLAIMED      = "claimed";       // value
    constexpr const char* TYPE_SWEEP        = "sweep";         // value

    constexpr const char* VERDICT_EXPIRED   = "expired";       // value
}

// ── yed_estimatecollateral result ──────────────────────────────────────────────────────────
namespace Estimate {   // contract: yed_estimatecollateral
    constexpr const char* REQUIRED_ZAT      = "requiredZat";   // null when pMint is undefined
    constexpr const char* TERM_CLASS        = "termClass";
    constexpr const char* LOCK_HEIGHT       = "lockHeight";
    constexpr const char* CLAIM_HEIGHT      = "claimHeight";
    constexpr const char* MIN_RATIO_BPS     = "minRatioBps";
    constexpr const char* BASE_RATIO_BPS    = "baseRatioBps";
    constexpr const char* SIGMA_MULT_BPS    = "sigmaMultBps";
    constexpr const char* P_MINT            = "pMint";         // null when undefined
    constexpr const char* REF_HEIGHT        = "refHeight";
}

// ── yed_mint result ────────────────────────────────────────────────────────────────────────
namespace MintResult {   // contract: yed_mint
    constexpr const char* TXID              = "txid";
    constexpr const char* VAULT             = "vault";         // "txid:0"
    constexpr const char* TERM_CLASS        = "termClass";
    constexpr const char* LOCK_HEIGHT       = "lockHeight";
    constexpr const char* CLAIM_HEIGHT      = "claimHeight";
    constexpr const char* COLLATERAL_ZAT    = "collateralZat";
    constexpr const char* FEE_ZAT           = "feeZat";
    constexpr const char* PAYEE             = "payee";         // null under FEE-0
    constexpr const char* FUNDED_FROM       = "fundedFrom";    // "transparent" | "sapling"
    constexpr const char* WARNING           = "warning";       // keypool-low nag, "" when none
}

// ── yed_send result ────────────────────────────────────────────────────────────────────────
namespace SendResult {   // contract: yed_send
    constexpr const char* TXID              = "txid";
    constexpr const char* CHANGE_CENTS      = "changeCents";
    constexpr const char* EXPIRY_HEIGHT     = "expiryHeight";
}

// ── yed_redeem / yed_claim result (same shape) ────────────────────────────────────────────
namespace RedeemResult {   // contract: yed_redeem
    constexpr const char* TXID              = "txid";
    constexpr const char* BURNED_CENTS      = "burnedCents";   // 0 for a VOID release (L14)
    constexpr const char* FEE_ZAT           = "feeZat";        // 0 for a VOID release
    constexpr const char* PAYEE             = "payee";         // null for a VOID release
    constexpr const char* COLLATERAL_OUT    = "collateralOut";
    constexpr const char* TO                = "to";
}

// ── yed_sweep result ───────────────────────────────────────────────────────────────────────
namespace SweepResult {   // contract: yed_sweep
    constexpr const char* TXID              = "txid";
    constexpr const char* HEX               = "hex";
    constexpr const char* COLLATERAL_OUT    = "collateralOut";
    constexpr const char* TO                = "to";
    constexpr const char* UNBACKED_CENTS    = "unbackedCents";
}

// ── yed_validateaddress result ─────────────────────────────────────────────────────────────
namespace ValidateAddress {   // contract: yed_validateaddress
    constexpr const char* ISVALID           = "isvalid";
    constexpr const char* ADDRESS           = "address";
    constexpr const char* KEYID             = "keyid";
    constexpr const char* ISMINE            = "ismine";
    constexpr const char* TRANSPARENT_ADDRESS = "transparentAddress";
    constexpr const char* REASON            = "reason";        // not-a-yellowback-address when !isvalid
}

// ── Error identifiers the wallet recognises (the first token of error.message) ────────────
// Node error strings are stable identifiers and are always shown verbatim; these are only
// the ones the wallet branches on.
namespace Errors {   // contract: errors
    // Every gated command while the index is unhealthy (unhealthyReason follows)
    constexpr const char* INDEX_UNHEALTHY   = "yellowback-unhealthy";
    // yed_send / yed_redeem / yed_claim: YED change would fall below the minimum output
    constexpr const char* CHANGE_FLOOR      = "change-floor";
    constexpr const char* NOT_YELLOWBACK_ADDRESS = "not-a-yellowback-address";
    constexpr const char* VAULT_LOCKED      = "vault-locked";
    constexpr const char* SWEEP_NOT_ABANDONED = "sweep-not-abandoned";
    constexpr const char* INSUFFICIENT_YED  = "insufficient-yed";
}

// Wallet-side error matching that is not part of the yed_* contract (JSON-RPC and ycashd).
namespace RpcErrors {
    // JSON-RPC: the node has no yed_* methods (experimentalfeatures / yellowback not set)
    constexpr const char* METHOD_NOT_FOUND  = "Method not found";
    constexpr int         METHOD_NOT_FOUND_CODE = -32601;
    // yed_mint / yed_send / yed_redeem on an encrypted, locked wallet
    constexpr const char* WALLET_LOCKED     = "walletpassphrase";
}

// ── Display constants ─────────────────────────────────────────────────────────────────────
// Defaults only: everything the protocol fixes comes from yed_getinfo.params once the node
// has answered. SECONDS_PER_BLOCK is display-only (estimated dates).
constexpr int    SECONDS_PER_BLOCK   = 75;
constexpr qint64 MIN_MINT_CENTS      = 10000;     // $100 (§3.1 MIN_MINT)
constexpr qint64 MAX_MINT_CENTS      = 1000000;   // $10,000 (§3.1 MAX_MINT)
constexpr qint64 MIN_OUTPUT_CENTS    = 100;       // $1 change floor (§3.1 MIN_OUTPUT)
// The exact acknowledgement yed_sweep requires as its second argument (L10).
constexpr const char* SWEEP_ACKNOWLEDGEMENT = "I understand this leaves YED unbacked";

}

#endif // YELLOWBACKRPC_H
