#ifndef YELLOWBACKRPC_H
#define YELLOWBACKRPC_H

#include <QtGlobal>

// The Yellowback RPC contract (rpcversion 3), as the wallet depends on it.
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
// (bumped to 2 in Phase 7b-a's first commit, N27; to 3 in v3 Phase A5's first commit).
constexpr int RPC_VERSION = 3;

// ── Methods (node context) ────────────────────────────────────────────────────────────────
constexpr const char* GETINFO             = "yed_getinfo";
constexpr const char* GETSTATS            = "yed_getstats";
constexpr const char* GETACTIVATION       = "yed_getactivation";
constexpr const char* GETVAULT            = "yed_getvault";
constexpr const char* LISTCLAIMABLE       = "yed_listclaimable";
constexpr const char* GETTXINFO           = "yed_gettxinfo";
constexpr const char* ESTIMATECOLLATERAL  = "yed_estimatecollateral";
constexpr const char* GETFEEPAYEE         = "yed_getfeepayee";
constexpr const char* GETPRICE            = "yed_getprice";          // v3: xMint/xClaim, armed, attestStatus
constexpr const char* LISTATTESTORS       = "yed_listattestors";     // v3: the Attestors view
constexpr const char* GETSELECTION        = "yed_getselection";      // v3: "n of m selected attestors reachable"

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
// v3 (plan §4.8, A5-b): the two-step notice, the carrier sweep and the attestor actions
constexpr const char* CLAIMNOTICE         = "yed_claimnotice";        // step 1 of the emergency claim (NOT-1)
constexpr const char* SWEEPCARRIERS       = "yed_sweepcarriers";      // reclaim lapsed carriers (W7)
constexpr const char* REGISTERATTESTOR    = "yed_registerattestor";
constexpr const char* WITHDRAWBOND        = "yed_withdrawbond";
constexpr const char* REVIVE              = "yed_revive";
constexpr const char* REPORTEQUIVOCATION  = "yed_reportequivocation";

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
    constexpr const char* LOCKED_OUTPUTS    = "lockedOutputs";      // H10: YED outpoints the index holds locked
    constexpr const char* PROTECTED_BY_INDEX = "protectedByIndex";  // H10: false means ordinary sends could burn YED
    constexpr const char* ACTIVATION        = "activation";
    constexpr const char* MINER             = "miner";
    constexpr const char* PARAMS            = "params";
    constexpr const char* ATTEST            = "attest";         // v3: the arming state at the index tip
}

// v3: yed_getinfo.attest, the arming state (ARM-1/2) the Attestors view's banner shows.
namespace Attest {   // contract: yed_getinfo.attest
    constexpr const char* STATUS            = "status";         // UNARMED | TRIGGERED | ARMED
    constexpr const char* TRIGGER_HEIGHT    = "triggerHeight";  // 0 while UNARMED
    constexpr const char* ARM_HEIGHT        = "armHeight";      // triggerHeight + ATTEST_ARM_DELAY once TRIGGERED
    constexpr const char* SEATED_COUNT      = "seatedCount";
    constexpr const char* POOL_SIZE         = "poolSize";       // attestations in this node's pool
    constexpr const char* POOL_FRESH        = "poolFresh";      // seated seqs with a fresh attestation in the pool
    constexpr const char* CARRIER_MODE      = "carrierMode";
    constexpr const char* REQUIRED          = "required";       // ATTEST_REQUIRED; false: the layer is disabled by parameter set
    constexpr const char* ARMED             = "armed";          // status == ARMED && required

    constexpr const char* STATUS_UNARMED    = "UNARMED";        // value
    constexpr const char* STATUS_TRIGGERED  = "TRIGGERED";      // value
    constexpr const char* STATUS_ARMED      = "ARMED";          // value
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
    constexpr const char* ATTEST              = "attest";       // v3: the attestation-layer parameters
}

// v3: yed_getinfo.params.attest, the values the Mint page and the Attestors view derive from.
namespace ParamsAttest {   // contract: yed_getinfo.params.attest
    constexpr const char* REQUIRED            = "required";
    constexpr const char* M_SELECT            = "mSelect";
    constexpr const char* K_SLACK             = "kSlack";
    constexpr const char* DIVERGE_BPS_ATTEST  = "divergeBpsAttest";  // mint10-diverged above this
    constexpr const char* ARM_DELAY           = "armDelay";
    constexpr const char* ARM_MIN             = "armMin";
    constexpr const char* EMERGENCY_PERSIST   = "emergencyPersist";
    constexpr const char* CARRIER_MODE        = "carrierMode";
    constexpr const char* BOND_MIN_ZAT        = "bondMinZat";       // A5-b: the register dialog states them
    constexpr const char* BOND_MIN_LOCK       = "bondMinLock";
    constexpr const char* BOND_MATURITY       = "bondMaturity";
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
    constexpr const char* NOTICED           = "noticed";       // v3: a Notices record stands (NOT-1)
    constexpr const char* NOTICE_HEIGHT     = "noticeHeight";  // v3: null unless noticed
    constexpr const char* EMERGENCY_OPEN_AT = "emergencyOpenAt"; // v3: first refHeight a clause-(b) claim may cite; null unless noticed
    constexpr const char* CAN_NOTICE        = "canNotice";     // v3, listpositions only

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
    constexpr const char* CLAIM_PATH        = "claimPath";     // v3: "a" | "b" | "" (no bundle could be built)
    constexpr const char* RESIDUAL_ZAT      = "residualZat";   // v3: RED-5, returned to the owner by the claim
    constexpr const char* ATTEST_FEE_ZAT    = "attestFeeZat";  // v3: AFEE-1
    constexpr const char* NOTICED           = "noticed";       // v3
    constexpr const char* NOTICE_HEIGHT     = "noticeHeight";  // v3: null unless noticed
    constexpr const char* EMERGENCY_OPEN_AT = "emergencyOpenAt"; // v3: null unless noticed
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
    constexpr const char* TYPE_NOTICE       = "notice";        // value, v3: a CLAIM_NOTICE this wallet posted
    constexpr const char* TYPE_NOTICED      = "noticed";       // value, v3: a notice against a vault of ours
    constexpr const char* TYPE_REGISTER     = "register";      // value, v3
    constexpr const char* TYPE_EQUIVOCATION = "equivocation";  // value, v3
    constexpr const char* TYPE_REVIVE       = "revive";        // value, v3

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
    constexpr const char* P_MINT            = "pMint";         // null when undefined; v3: min(xMint, aMint) while armed
    constexpr const char* REF_HEIGHT        = "refHeight";
    constexpr const char* X_MINT            = "xMint";         // v3: the pool cross-section bound
    constexpr const char* A_MINT            = "aMint";         // v3: the attestation quantile bound; null when not armed
    constexpr const char* SOURCE            = "source";        // v3: "x" | "a" | "" (not armed, or a price override)
    constexpr const char* ARMED             = "armed";         // v3
    constexpr const char* BUNDLE_SEQS       = "bundleSeqs";    // v3: the seqs the estimate used
    constexpr const char* ATTEST_FEE_ZAT    = "attestFeeZat";  // v3: AFEE-1, on top of feeZat
    constexpr const char* DIVERGENCE_BPS    = "divergenceBps"; // v3: |xMint - aMint| / min; null when either is undefined

    constexpr const char* SOURCE_X          = "x";             // value
    constexpr const char* SOURCE_A          = "a";             // value
}

// ── v3: yed_getprice result (the snapshot at a height; the Overview's two source prices) ──
namespace Price {   // contract: yed_getprice
    constexpr const char* HEIGHT            = "height";
    constexpr const char* P_MINT            = "pMint";         // = xMint (the v2 key keeps the snapshot value)
    constexpr const char* P_CLAIM           = "pClaim";        // = xClaim
    constexpr const char* X_MINT            = "xMint";         // null when undefined
    constexpr const char* X_CLAIM           = "xClaim";        // null when undefined
    constexpr const char* ARMED             = "armed";
    constexpr const char* ATTEST_STATUS     = "attestStatus";  // UNARMED | TRIGGERED | ARMED
    constexpr const char* SEATED            = "seated";        // seqs
    constexpr const char* PINNED_SEQS       = "pinnedSeqs";
    constexpr const char* PINNED_KEYS       = "pinnedKeys";
}

// ── v3: yed_listattestors element (the Attestors view) ────────────────────────────────────
namespace Attestor {   // contract: yed_listattestors[]
    constexpr const char* SEQ               = "seq";
    constexpr const char* STATUS            = "status";        // PENDING | ELIGIBLE | DORMANT | EJECTED | WITHDRAWN
    constexpr const char* STATUS_HEIGHT     = "statusHeight";
    constexpr const char* ATTESTOR_PUBKEY   = "attestorPubKey";
    constexpr const char* BOND_ADDRESS      = "bondAddress";
    constexpr const char* BOND_KEY_ADDRESS  = "bondKeyAddress";
    constexpr const char* BOND_ZAT          = "bondZat";
    constexpr const char* BOND_LOCKTIME     = "bondLocktime";
    constexpr const char* BOND_SPENT_HEIGHT = "bondSpentHeight";   // null until spent
    constexpr const char* REGISTER_HEIGHT   = "registerHeight";
    constexpr const char* WEIGHT            = "weight";        // decimal string
    constexpr const char* SEATED            = "seated";
    constexpr const char* SEATED_SINCE      = "seatedSince";   // null while unseated
    constexpr const char* PINNED            = "pinned";
    constexpr const char* FOUNDING          = "founding";
    constexpr const char* FLAGS             = "flags";
    constexpr const char* LAST_BUNDLE_HEIGHT= "lastBundleHeight";  // null when none
    constexpr const char* POOL_FRESH        = "poolFresh";

    constexpr const char* STATUS_PENDING    = "PENDING";       // value
    constexpr const char* STATUS_ELIGIBLE   = "ELIGIBLE";      // value
    constexpr const char* STATUS_DORMANT    = "DORMANT";       // value
    constexpr const char* STATUS_EJECTED    = "EJECTED";       // value
    constexpr const char* STATUS_WITHDRAWN  = "WITHDRAWN";     // value
}

namespace AttestorFlags {   // contract: yed_listattestors[].flags
    constexpr const char* TIER              = "tier";          // 0 exchange APIs | 1 mixed | 2 aggregator
    constexpr const char* POOL              = "pool";          // operates a mining pool
}

// ── v3: yed_getselection result ("n of m selected attestors reachable") ───────────────────
namespace Selection {   // contract: yed_getselection
    constexpr const char* REF_HEIGHT        = "refHeight";
    constexpr const char* ARMED             = "armed";
    constexpr const char* M_SELECT          = "mSelect";
    constexpr const char* K_SLACK           = "kSlack";
    constexpr const char* POOL              = "pool";          // seqs
    constexpr const char* SELECTED          = "selected";      // rows
    constexpr const char* REACHABLE         = "reachable";     // count of poolFresh among selected
    constexpr const char* FALLBACK          = "fallback";
    constexpr const char* SUM_WEIGHT        = "sumWeight";
}

namespace SelectionRow {   // contract: yed_getselection.selected[]
    constexpr const char* SEQ               = "seq";
    constexpr const char* STATUS            = "status";
    constexpr const char* WEIGHT            = "weight";
    constexpr const char* BOND_KEY_ADDRESS  = "bondKeyAddress";
    constexpr const char* POOL_FRESH        = "poolFresh";
}

// ── yed_getfeepayee result (the mint / redeem confirmation shows the fee and its payee) ────
namespace FeePayee {   // contract: yed_getfeepayee
    constexpr const char* ELIGIBLE          = "eligible";      // E(refHeight)
    constexpr const char* FEE_ZAT           = "feeZat";        // FEE-1 for the given collateral
    constexpr const char* DEFAULT           = "default";       // the FEE-W choice
    constexpr const char* PREFERRED         = "preferred";     // optional: the configured preference, when eligible
    constexpr const char* POLICY            = "policy";
}

namespace FeePayeeDefault {   // contract: yed_getfeepayee.default
    constexpr const char* PAYOUT_ADDRESS    = "payoutAddress";
    constexpr const char* WEIGHT            = "weight";
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
    // v3 (W7): with wait = false the call returns after the carrier broadcast with pending = true,
    // carrierTxid set and every other field at its zero value; the main transaction follows on
    // the next ChainTip and yed_listtransactions / yed_gettxinfo then carry it.
    constexpr const char* CARRIER_TXID      = "carrierTxid";
    constexpr const char* PENDING           = "pending";
    constexpr const char* REF_HEIGHT        = "refHeight";
    constexpr const char* X_MINT            = "xMint";
    constexpr const char* A_MINT            = "aMint";         // null when not armed
    constexpr const char* P_MINT            = "pMint";
    constexpr const char* SOURCE            = "source";        // "x" | "a" | ""
    constexpr const char* BUNDLE_SEQS       = "bundleSeqs";
    constexpr const char* ATTEST_FEE_ZAT    = "attestFeeZat";
    constexpr const char* ATTEST_PAYEE      = "attestPayee";   // the bondKeyAddress paid; null under AFEE-0
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
    constexpr const char* EXTRA_BURN_CENTS  = "extraBurnCents";  // H4: a sub-dollar remainder burned with the debt
}

// v3: the fields yed_claim adds to the yed_redeem shape (read alongside RedeemResult).
namespace ClaimResult {   // contract: yed_claim
    constexpr const char* CARRIER_TXID      = "carrierTxid";
    constexpr const char* PENDING           = "pending";
    constexpr const char* REF_HEIGHT        = "refHeight";
    constexpr const char* X_CLAIM           = "xClaim";
    constexpr const char* A_CLAIM           = "aClaim";
    constexpr const char* P_CLAIM           = "pClaim";
    constexpr const char* P_EMERG           = "pEmerg";        // null under clause (a)
    constexpr const char* CLAIM_PATH        = "claimPath";     // "a" underwater | "b" emergency clause
    constexpr const char* BUNDLE_SEQS       = "bundleSeqs";
    constexpr const char* ATTEST_FEE_ZAT    = "attestFeeZat";
    constexpr const char* ATTEST_PAYEE      = "attestPayee";
    constexpr const char* RESIDUAL_ZAT      = "residualZat";   // RED-5: returned to the vault owner

    constexpr const char* PATH_A            = "a";             // value
    constexpr const char* PATH_B            = "b";             // value
}

// v3: yed_claimnotice result (step 1 of the emergency claim, NOT-1).
namespace NoticeResult {   // contract: yed_claimnotice
    constexpr const char* TXID              = "txid";
    constexpr const char* VAULT             = "vault";
    constexpr const char* CARRIER_TXID      = "carrierTxid";
    constexpr const char* PENDING           = "pending";
    constexpr const char* REF_HEIGHT        = "refHeight";
    constexpr const char* EMERGENCY_OPEN_AT = "emergencyOpenAt";  // refHeight + EMERGENCY_PERSIST
    constexpr const char* X_CLAIM           = "xClaim";
    constexpr const char* A_CLAIM           = "aClaim";
    constexpr const char* P_EMERG           = "pEmerg";
    constexpr const char* BUNDLE_SEQS       = "bundleSeqs";
}

// v3: yed_gettxinfo, the fields the two-step actions read once the main transaction exists
// (the v2 fields the Transactions view reads are in the Transaction namespace above).
namespace TxInfo {   // contract: yed_gettxinfo
    constexpr const char* TXID              = "txid";
    constexpr const char* TYPE              = "type";
    constexpr const char* HEIGHT            = "height";
    constexpr const char* VERDICT           = "verdict";
    constexpr const char* FEE_ZAT           = "feeZat";
    constexpr const char* PAYEE             = "payee";
    constexpr const char* P_MINT            = "pMint";
    constexpr const char* X_MINT            = "xMint";
    constexpr const char* A_MINT            = "aMint";
    constexpr const char* P_CLAIM           = "pClaim";
    constexpr const char* X_CLAIM           = "xClaim";
    constexpr const char* A_CLAIM           = "aClaim";
    constexpr const char* BUNDLE_SEQS       = "bundleSeqs";
    constexpr const char* ATTEST_FEE_ZAT    = "attestFeeZat";
    constexpr const char* ATTEST_PAYEE      = "attestPayee";
    constexpr const char* CLAIM_PATH        = "claimPath";
    constexpr const char* RESIDUAL_ZAT      = "residualZat";
    constexpr const char* NOTICE            = "notice";        // true when this transaction wrote a notice
    constexpr const char* BURNED            = "burned";
}

// v3: yed_sweepcarriers result (W7).
namespace SweepCarriersResult {   // contract: yed_sweepcarriers
    constexpr const char* TXID              = "txid";          // "" when count is 0
    constexpr const char* COUNT             = "count";
    constexpr const char* RECLAIMED_ZAT     = "reclaimedZat";
    constexpr const char* OUTSTANDING       = "outstanding";   // carriers still inside their window
}

// v3: yed_registerattestor result. `seq` is null until the registration confirms (REG-A1).
namespace RegisterResult {   // contract: yed_registerattestor
    constexpr const char* TXID              = "txid";
    constexpr const char* SEQ               = "seq";
    constexpr const char* ATTESTOR_PUBKEY   = "attestorPubKey";
    constexpr const char* BOND_ADDRESS      = "bondAddress";
    constexpr const char* BOND_KEY_ADDRESS  = "bondKeyAddress";
    constexpr const char* BOND_ZAT          = "bondZat";
    constexpr const char* BOND_LOCKTIME     = "bondLocktime";
    constexpr const char* MATURES_AT        = "maturesAt";
}

// v3: yed_withdrawbond result.
namespace WithdrawResult {   // contract: yed_withdrawbond
    constexpr const char* TXID              = "txid";
    constexpr const char* SEQ               = "seq";
    constexpr const char* BOND_ZAT          = "bondZat";
    constexpr const char* BOND_OUT          = "bondOut";       // net of the network fee
    constexpr const char* TO                = "to";
}

// v3: yed_revive result.
namespace ReviveResult {   // contract: yed_revive
    constexpr const char* TXID              = "txid";
    constexpr const char* SEQ               = "seq";
    constexpr const char* CITED_HEIGHT      = "citedHeight";
    constexpr const char* PRICE_MICRO_USD   = "priceMicroUsd";
}

// v3: yed_reportequivocation result (two-step, as yed_mint).
namespace EquivocationResult {   // contract: yed_reportequivocation
    constexpr const char* TXID              = "txid";
    constexpr const char* CARRIER_TXID      = "carrierTxid";
    constexpr const char* PENDING           = "pending";
    constexpr const char* SEQ               = "seq";
    constexpr const char* CITED_HEIGHT      = "citedHeight";
    constexpr const char* PRICE_A           = "priceA";
    constexpr const char* PRICE_B           = "priceB";
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
    constexpr const char* VAULT_NOT_FOUND   = "vault-not-found";
    constexpr const char* VAULT_NOT_ACTIVE  = "vault-not-active";     // CLOSED or CLAIMED only (L14)
    constexpr const char* VAULT_NOT_OWNED   = "vault-not-owned";
    constexpr const char* SWEEP_NOT_ABANDONED = "sweep-not-abandoned";
    constexpr const char* SWEEP_ACKNOWLEDGEMENT_MISSING = "sweep-acknowledgement-missing";
    constexpr const char* INSUFFICIENT_YED  = "insufficient-yed";
    // yed_mint: MINTPOL-1, one per halt bit and the cap
    constexpr const char* MINTPOL_NOT_ACTIVE    = "mintpol-not-active";
    constexpr const char* MINTPOL_NO_PRICE      = "mintpol-no-price";
    constexpr const char* MINTPOL_PARTICIPATION = "mintpol-participation";
    constexpr const char* MINTPOL_GLOBAL_RATIO  = "mintpol-global-ratio";
    constexpr const char* MINTPOL_DIVERGENCE    = "mintpol-divergence";
    constexpr const char* MINTPOL_CAP           = "mintpol-cap";
    constexpr const char* MINT_UNSATISFIABLE    = "mint-unsatisfiable";
    constexpr const char* MINT_BAD_LOCK         = "mint-bad-lock";
    constexpr const char* CLAIM_NOT_YET         = "claim-not-yet";
    constexpr const char* CLAIM_NOT_UNDERWATER  = "claim-not-underwater";
    // The contract lists `mempool-check-failed:<verdict>`; the wallet matches the prefix
    constexpr const char* MEMPOOL_CHECK_FAILED  = "mempool-check-failed";   // value
    constexpr const char* FEE_NO_ELIGIBLE_PAYEE = "fee-no-eligible-payee";  // yed_getfeepayee under FEE-0: not an error for the wallet
    // v3: yed_estimatecollateral / yed_mint while armed. The Mint page shows the first as the
    // selection line and the second as "pools and attestors disagree by N %; minting paused".
    constexpr const char* BUNDLE_INSUFFICIENT   = "bundle-insufficient";     // "<count> of <selected> selected attestors have a fresh attestation; missing seq <a,b,…>"
    constexpr const char* MINT10_DIVERGED       = "mint10-diverged";
    constexpr const char* BUNDLE_MALFORMED      = "bundle-malformed";
    constexpr const char* INSUFFICIENT_YEC      = "insufficient-yec";        // yed_mint: the carrier and two fees are part of the need
    // v3: yed_claimnotice
    constexpr const char* NOTICE_STANDING       = "notice-standing";
    constexpr const char* NOTICE_NOT_UNDERWATER = "notice-not-underwater";
    // v3: the attestor actions
    constexpr const char* BOND_BELOW_MIN        = "bond-below-min";
    constexpr const char* LOCK_BELOW_MIN        = "lock-below-min";
    constexpr const char* BOND_LOCKED           = "bond-locked";
    constexpr const char* BOND_SPENT            = "bond-spent";
    constexpr const char* NOT_DORMANT           = "not-dormant";
    constexpr const char* NOT_EQUIVOCATION      = "not-equivocation";
    constexpr const char* ATTEST_KEY_NOT_HELD   = "attest-key-not-held";
    constexpr const char* ATTEST_UNKNOWN_SEQ    = "attest-unknown-seq";
    constexpr const char* ATTEST_MALFORMED      = "attest-malformed";
    constexpr const char* ATTEST_RANGE          = "attest-range";
    constexpr const char* EQUIVOCATION_GUARD    = "equivocation-guard";
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
// v3: an attestation travels through RPC as 74 bytes of hex (yed_reportequivocation's arguments).
constexpr int ATTESTATION_HEX_LENGTH = 148;

}

#endif // YELLOWBACKRPC_H
