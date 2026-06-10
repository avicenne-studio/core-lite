using namespace QPI;

// ---------------------------------------------------------------------
// Constants / configuration
// ---------------------------------------------------------------------

static constexpr uint32 QSB_MAX_ORACLES = 64;
static constexpr uint32 QSB_MAX_PAUSERS = 32;
static constexpr uint32 QSB_MAX_FILLED_ORDERS = 256; // QPI::Array requires power-of-2; 256 = 5x the ~50 max concurrent orders
static constexpr uint32 QSB_MAX_LOCKED_ORDERS = 1024;
static constexpr uint32 QSB_MAX_BPS_FEE = 1000;      // max 10% fee (1000 / 10000)
static constexpr uint32 QSB_MAX_PROTOCOL_FEE = 100;  // max 100% of bps fee
static constexpr uint8 QSB_OVERRIDE_LOCK_MAX_ATTEMPTS = 3;
static constexpr uint32 QSB_MAX_UNLOCK_SIGNATURES = 8; // max sigs per Unlock call; keeps Unlock_input ≤ 1024 bytes

// Domain-prefixed order message for K12 hashing and signature verification.
// Layout: 245 bytes total. protocolName is padded to 16 (next power of 2 above 11).
struct QSBOrderMessage
{
	uint32 protocolNameLen;              // 0:  always 11
	Array<uint8, 16> protocolName;       // 4:  QubicBridge (11 used, 5 zero-padded)
	uint32 protocolVersionLen;           // 20: always 1
	Array<uint8, 1> protocolVersion;     // 24: version byte (49 = ASCII '1')
	Array<uint8, 32> contractAddress;    // 25: destination contract address (LE-padded index)
	uint32 networkIn;                    // 57
	uint32 networkOut;                   // 61
	Array<uint8, 32> tokenIn;            // 65
	Array<uint8, 32> tokenOut;           // 97
	Array<uint8, 32> fromAddress;        // 129
	Array<uint8, 32> toAddress;          // 161
	uint64 amount;                       // 193
	uint64 relayerFee;                   // 201
	Array<uint8, 32> nonce;              // 209
	uint32 orderEra;                     // 241
};
static constexpr uint32 QSB_QUERY_MAX_PAGE_SIZE = 64; // max entries per paginated query

// Log types for QSB contract (no enums allowed in contracts)
static constexpr uint32 QSBLogLock = 1;
static constexpr uint32 QSBLogOverrideLock = 2;
static constexpr uint32 QSBLogUnlock = 3;
static constexpr uint32 QSBLogPaused = 4;
static constexpr uint32 QSBLogUnpaused = 5;
static constexpr uint32 QSBLogAdminTransferred = 6;
static constexpr uint32 QSBLogThresholdUpdated = 7;
static constexpr uint32 QSBLogRoleGranted = 8;
static constexpr uint32 QSBLogRoleRevoked = 9;
static constexpr uint32 QSBLogFeeParametersUpdated = 10;
static constexpr uint32 QSBLogProposalCreated   = 11;
static constexpr uint32 QSBLogProposalApproved  = 12;
static constexpr uint32 QSBLogProposalExecuted  = 13;
static constexpr uint32 QSBLogProposalCancelled = 14;

// Multisig admin constants
static constexpr uint32 QSB_MAX_ADMINS               = 8;   // approvedMask is uint8; must stay ≤ 8
static constexpr uint32 QSB_MAX_PROPOSALS            = 16;
static constexpr uint32 QSB_MAX_PROPOSALS_PER_ADMIN  = 3;
static constexpr uint32 QSB_PROPOSAL_EXPIRY_EPOCHS   = 4;   // ~4 weeks

// Proposal types
static constexpr uint8 QSBPropAddAdmin            = 1;
static constexpr uint8 QSBPropRemoveAdmin         = 2;
static constexpr uint8 QSBPropSetAdminThreshold   = 3;
static constexpr uint8 QSBPropAddRole             = 4;
static constexpr uint8 QSBPropRemoveRole          = 5;
static constexpr uint8 QSBPropEditOracleThreshold = 6;
static constexpr uint8 QSBPropEditFeeParameters   = 7;
static constexpr uint8 QSBPropUnpause             = 8;

// Generic reason codes for logging
static constexpr uint8 QSBReasonNone = 0;
static constexpr uint8 QSBReasonPaused = 1;
static constexpr uint8 QSBReasonInvalidAmount = 2;
static constexpr uint8 QSBReasonInsufficientReward = 3;
static constexpr uint8 QSBReasonNonceUsed = 4;
static constexpr uint8 QSBReasonNoSpace = 5;
static constexpr uint8 QSBReasonNotSender = 6;
static constexpr uint8 QSBReasonBadRelayerFee = 7;
static constexpr uint8 QSBReasonNoOracles = 8;
static constexpr uint8 QSBReasonThresholdFailed = 9;
static constexpr uint8 QSBReasonAlreadyFilled = 10;
static constexpr uint8 QSBReasonInvalidSignature = 11;
static constexpr uint8 QSBReasonDuplicateSigner = 12;
static constexpr uint8 QSBReasonNotAdmin = 13;
static constexpr uint8 QSBReasonNotAdminOrPauser = 14;
static constexpr uint8 QSBReasonInvalidThreshold = 15;
static constexpr uint8 QSBReasonRoleExists = 16;
static constexpr uint8 QSBReasonRoleMissing = 17;
static constexpr uint8 QSBReasonInvalidFeeParams = 18;
static constexpr uint8 QSBReasonTransferFailed = 19;
static constexpr uint8 QSBReasonEraMismatch = 20;
static constexpr uint8 QSBReasonInvalidAdmin = 21;
static constexpr uint8 QSBReasonInvalidRole = 22;
static constexpr uint8 QSBReasonOrderNotFound = 23;
static constexpr uint8 QSBReasonOverrideLimitReached = 24;
// Multisig admin reason codes
static constexpr uint8 QSBReasonProposalNotFound   = 25;
static constexpr uint8 QSBReasonProposalExpired    = 26;
static constexpr uint8 QSBReasonAlreadyApproved    = 27;
static constexpr uint8 QSBReasonProposalFull       = 28;
static constexpr uint8 QSBReasonWouldLockContract  = 29;
static constexpr uint8 QSBReasonNotProposer        = 30;
static constexpr uint8 QSBReasonAlreadyAdmin       = 31;
static constexpr uint8 QSBReasonAdminFull          = 32;
static constexpr uint8 QSBReasonTooManyProposals   = 33;
static constexpr uint8 QSBReasonInvalidProposalType = 34;

struct QSB2
{
};

struct QSB : public ContractBase
{
public:
	// Role identifiers for addRole / removeRole
	enum class Role : uint8
	{
		Oracle = 1,
		Pauser = 2
	};

	// ---------------------------------------------------------------------
	// Core data structures
	// ---------------------------------------------------------------------

	struct Order
	{
		id fromAddress;
		id toAddress;
		Array<uint8, 32> tokenIn;
		Array<uint8, 32> tokenOut;
		uint64 amount;
		uint64 relayerFee;
		uint32 networkIn;
		uint32 networkOut;
		Array<uint8, 32> nonce;
		uint32 orderEra;
	};

	// Compact order-hash representation (K12 digest)
	typedef Array<uint8, 32> OrderHash;

	// Signature wrapper compatible with QPI::signatureValidity
	struct SignatureData
	{
		id signer;     // oracle id (public key)
		Array<sint8, 64> signature;  // raw 64-byte signature
	};

	// Storage entry for filledOrders mapping
	struct FilledOrderEntry
	{
		OrderHash hash;
		bit used;
	};

	// Storage entry for role mappings (oracles / pausers)
	struct RoleEntry
	{
		id account;
		bit active;
	};

	// Storage entry for lock() orders (for overrideLock / off-chain reference)
	struct LockedOrderEntry
	{
		id sender;
		uint64 amount;
		uint64 relayerFee;
		uint32 networkOut;
		uint32 nonce;
		Array<uint8, 64> toAddress;
		OrderHash orderHash;
		uint32 lockEpoch;
		uint32 orderEra;
		bit active;
		uint8 overrideLockCount;  // at +161; 6 bytes padding follow to keep struct at 168 bytes
	};

	// Logging messages
	struct QSBLogLockMessage
	{
		uint32 _contractIndex;
		uint32 _type;
		id from;
		Array<uint8, 64> to;
		uint64 amount;
		uint64 relayerFee;
		uint32 networkOut;
		uint32 nonce;
		OrderHash orderHash;
		uint8 success;
		uint8 reasonCode;
		uint32 orderEra;
		sint8 _terminator;
	};

	struct QSBLogOverrideLockMessage
	{
		uint32 _contractIndex;
		uint32 _type;
		id from;
		Array<uint8, 64> to;
		uint64 amount;
		uint64 relayerFee;
		uint32 networkOut;
		uint32 nonce;
		OrderHash orderHash;
		uint8 success;
		uint8 reasonCode;
		uint32 orderEra;
		sint8 _terminator;
	};

	struct QSBLogUnlockMessage
	{
		uint32 _contractIndex;
		uint32 _type;
		OrderHash orderHash;
		id toAddress;
		uint64 amount;
		uint64 relayerFee;
		id relayer;
		uint8 success;
		uint8 reasonCode;
		uint32 orderEra;
		sint8 _terminator;
	};

	struct QSBLogAdminTransferredMessage
	{
		uint32 _contractIndex;
		uint32 _type;
		id previousAdmin;
		id newAdmin;
		uint8 success;
		uint8 reasonCode;
		sint8 _terminator;
	};

	struct QSBLogThresholdUpdatedMessage
	{
		uint32 _contractIndex;
		uint32 _type;
		uint8 oldThreshold;
		uint8 newThreshold;
		uint8 success;
		uint8 reasonCode;
		sint8 _terminator;
	};

	struct QSBLogRoleMessage
	{
		uint32 _contractIndex;
		uint32 _type;
		uint8 role;
		id account;
		id caller;
		uint8 success;
		uint8 reasonCode;
		sint8 _terminator;
	};

	struct QSBLogPausedMessage
	{
		uint32 _contractIndex;
		uint32 _type;
		id caller;
		uint8 success;
		uint8 reasonCode;
		sint8 _terminator;
	};

	struct QSBLogFeeParametersUpdatedMessage
	{
		uint32 _contractIndex;
		uint32 _type;
		uint32 bpsFee;
		uint32 protocolFee;
		id protocolFeeRecipient;
		id oracleFeeRecipient;
		uint8 success;
		uint8 reasonCode;
		sint8 _terminator;
	};

	struct QSBLogProposalMessage
	{
		uint32 _contractIndex;
		uint32 _type;
		uint8  proposalId;
		uint8  proposalType;
		id     proposer;
		id     actor;
		uint8  approvalCount;
		uint8  success;
		uint8  reasonCode;
		sint8  _terminator;
	};

	// Union-style: fields used depend on proposalType. Unused fields are zero.
	struct AdminProposal
	{
		uint8  proposalType;        // QSBProp* constant
		uint8  active;              // 1 = slot in use
		uint8  executed;            // 1 = executed successfully

		id     proposer;            // admin who created this proposal
		uint32 createdEpoch;        // for expiry: createdEpoch + QSB_PROPOSAL_EXPIRY_EPOCHS

		uint8  approvalCount;       // cached popcount of approvedMask
		uint8  approvedMask;        // bit i = admins[i] approved (max 8 admins)

		// Payload — fields used depend on proposalType
		id     targetId;            // AddAdmin, RemoveAdmin, AddRole/RemoveRole account
		uint8  role;                // AddRole, RemoveRole: (uint8)Role::Oracle or Role::Pauser
		uint8  newAdminThreshold;   // SetAdminThreshold
		uint8  newOracleThreshold;  // EditOracleThreshold
		id     protocolFeeRecipient;
		id     oracleFeeRecipient;
		uint32 bpsFee;
		uint32 protocolFee;
	};

	// ---------------------------------------------------------------------
	// User-facing I/O structures
	// ---------------------------------------------------------------------

	// 1) lock()
	struct Lock_input
	{
		// Recipient on Solana (fixed-size buffer, zero-padded)
		uint64 amount;
		uint64 relayerFee;
		Array<uint8, 64> toAddress;
		uint32 networkOut;
		uint32 nonce;
	};

	struct Lock_output
	{
		OrderHash orderHash;
		bit success;
	};

	// 2) overrideLock()
	struct OverrideLock_input
	{
		Array<uint8, 64> toAddress;
		uint64 relayerFee;
		uint32 nonce;
	};

	struct OverrideLock_output
	{
		OrderHash orderHash;
		bit success;
	};

	// 3) unlock()
	struct Unlock_input
	{
		Order order;
		uint32 numSignatures;
		Array<SignatureData, QSB_MAX_UNLOCK_SIGNATURES> signatures;
	};

	struct Unlock_output
	{
		OrderHash orderHash;
		bit success;
	};

	// 4) transferAdmin()
	struct TransferAdmin_input
	{
		id newAdmin;
	};

	struct TransferAdmin_output
	{
		bit success;
	};

	// 5) editOracleThreshold()
	struct EditOracleThreshold_input
	{
		uint8 newThreshold;
	};

	struct EditOracleThreshold_output
	{
		uint8 oldThreshold;
		bit success;
	};

	// 6) addRole()
	struct AddRole_input
	{
		id account;
		uint8 role;    // see Role enum
	};

	struct AddRole_output
	{
		bit success;
	};

	// 7) removeRole()
	struct RemoveRole_input
	{
		id account;
		uint8 role;
	};

	struct RemoveRole_output
	{
		bit success;
	};

	// 8) pause() / unpause()
	struct Pause_input
	{
	};

	struct Pause_output
	{
		bit success;
	};

	typedef Pause_input  Unpause_input;
	typedef Pause_output Unpause_output;

	// 9) editFeeParameters()
	struct EditFeeParameters_input
	{
		id protocolFeeRecipient; // updated when not zero-id
		id oracleFeeRecipient;   // updated when not zero-id
		uint32 bpsFee;           // basis points fee (0..10000)
		uint32 protocolFee;      // share of BPS fee for protocol (0..100)
	};

	struct EditFeeParameters_output
	{
		bit success;
	};

	// Propose: create a typed admin proposal (proposer auto-approves)
	struct Propose_input
	{
		uint8  proposalType;
		id     targetId;
		uint8  role;
		uint8  newAdminThreshold;
		uint8  newOracleThreshold;
		id     protocolFeeRecipient;
		id     oracleFeeRecipient;
		uint32 bpsFee;
		uint32 protocolFee;
	};
	struct Propose_output
	{
		uint8 proposalId;   // slot index; valid only when success == true
		bit   success;
		uint8 reasonCode;
	};

	struct ApproveProposal_input  { uint8 proposalId; };
	struct ApproveProposal_output { bit success; bit executed; uint8 reasonCode; };

	struct CancelProposal_input   { uint8 proposalId; };
	struct CancelProposal_output  { bit success; uint8 reasonCode; };

	struct GetProposal_input  { uint8 proposalId; };
	struct GetProposal_output { bit exists; AdminProposal proposal; };

	struct GetProposals_input  {};
	struct GetProposals_output
	{
		uint8 count;
		Array<AdminProposal, QSB_MAX_PROPOSALS> proposals;
	};

	// ---------------------------------------------------------------------
	// View / frontend helper functions
	// ---------------------------------------------------------------------

	struct GetConfig_input
	{
	};

	struct GetConfig_output
	{
		uint8  adminCount;
		uint8  adminThreshold;
		Array<id, QSB_MAX_ADMINS> admins;
		id     protocolFeeRecipient;
		id     oracleFeeRecipient;
		uint32 bpsFee;
		uint32 protocolFee;
		uint32 oracleCount;
		uint32 pauserCount;
		uint8  oracleThreshold;
		bit    paused;
		uint32 orderEra;
	};

	struct IsOracle_input
	{
		id account;
	};

	struct IsOracle_output
	{
		bit isOracle;
	};

	struct IsPauser_input
	{
		id account;
	};

	struct IsPauser_output
	{
		bit isPauser;
	};

	struct GetLockedOrder_input
	{
		uint32 nonce;
	};

	struct GetLockedOrder_output
	{
		bit exists;
		LockedOrderEntry order;
	};

	struct IsOrderFilled_input
	{
		OrderHash hash;
	};

	struct IsOrderFilled_output
	{
		bit filled;
	};

	// ComputeOrderHash: canonical hash for Unlock verification
	struct ComputeOrderHash_input
	{
		Order order;
	};

	struct ComputeOrderHash_output
	{
		OrderHash hash;
	};

	// GetOracles: bulk enumeration of all oracle accounts
	struct GetOracles_input
	{
	};

	struct GetOracles_output
	{
		uint32 count;
		Array<id, QSB_MAX_ORACLES> accounts;
	};

	// GetPausers: bulk enumeration of all pauser accounts
	struct GetPausers_input
	{
	};

	struct GetPausers_output
	{
		uint32 count;
		Array<id, QSB_MAX_PAUSERS> accounts;
	};

	// GetLockedOrders: paginated enumeration of active locked orders
	struct GetLockedOrders_input
	{
		uint32 offset; // skip this many active entries
		uint32 limit;  // return up to this many (capped at QSB_QUERY_MAX_PAGE_SIZE)
	};

	struct GetLockedOrders_output
	{
		uint32 totalActive;
		uint32 returned;
		Array<LockedOrderEntry, QSB_QUERY_MAX_PAGE_SIZE> entries;
	};

	// GetFilledOrders: paginated enumeration of filled order hashes
	struct GetFilledOrders_input
	{
		uint32 offset; // skip this many filled entries
		uint32 limit;  // return up to this many (capped at QSB_QUERY_MAX_PAGE_SIZE)
	};

	struct GetFilledOrders_output
	{
		uint32 totalActive;
		uint32 returned;
		Array<OrderHash, QSB_QUERY_MAX_PAGE_SIZE> hashes;
	};

	// ---------------------------------------------------------------------
	// State data (accessible via state.get() / state.mut() in procedures)
	// ---------------------------------------------------------------------
	struct StateData
	{
		// Multisig admin (replaces single `id admin`)
		Array<id, QSB_MAX_ADMINS> admins;  // zero entry = empty slot
		uint8 adminCount;                   // number of active admins
		uint8 adminThreshold;               // M in M-of-N (always ≥ 1, always ≤ adminCount)
		Array<AdminProposal, QSB_MAX_PROPOSALS> proposals;

		id protocolFeeRecipient;
		id oracleFeeRecipient;
		Array<RoleEntry, QSB_MAX_ORACLES> oracles;
		Array<RoleEntry, QSB_MAX_PAUSERS> pausers;
		Array<FilledOrderEntry, QSB_MAX_FILLED_ORDERS> filledOrders;
		Array<FilledOrderEntry, QSB_MAX_FILLED_ORDERS> filledOrdersPrev;
		Array<LockedOrderEntry, QSB_MAX_LOCKED_ORDERS> lockedOrders;
		uint32 lastFilledOrdersNextOverwriteIdx;
		uint32 lastLockedOrdersNextOverwriteIdx;
		uint32 oracleCount;
		uint32 pauserCount;
		uint32 bpsFee;
		uint32 protocolFee;
		uint8  oracleThreshold; // percent [1..100]
		bit    paused;
		uint32 orderEra;
	};

protected:

	// ---------------------------------------------------------------------
	// Internal helpers
	// ---------------------------------------------------------------------

	// Truncate digest to OrderHash (full 32 bytes)
	inline static void digestToOrderHash(const id& digest, OrderHash& outHash)
	{
		// Copy digest directly to OrderHash (both are 32 bytes)
		// Use setMem which handles 32-byte types specially
		outHash.setMem(digest);
	}

	inline static void initDomainPrefix(QSBOrderMessage& msg)
	{
		setMemory(msg, 0);
		msg.protocolNameLen = 11;
		msg.protocolName.set(0, 81);   // Q
		msg.protocolName.set(1, 117);  // u
		msg.protocolName.set(2, 98);   // b
		msg.protocolName.set(3, 105);  // i
		msg.protocolName.set(4, 99);   // c
		msg.protocolName.set(5, 66);   // B
		msg.protocolName.set(6, 114);  // r
		msg.protocolName.set(7, 105);  // i
		msg.protocolName.set(8, 100);  // d
		msg.protocolName.set(9, 103);  // g
		msg.protocolName.set(10, 101); // e
		msg.protocolVersionLen = 1;
		msg.protocolVersion.set(0, 49); // 1
		msg.contractAddress.set(0, (uint8)(CONTRACT_INDEX & 0xFF));
		msg.contractAddress.set(1, (uint8)((CONTRACT_INDEX >> 8) & 0xFF));
	}

	inline static void buildOrderMessage(
		QSBOrderMessage& msg,
		const Order& order,
		OrderHash& tmpIdBytes,
		uint32 i)
	{
		initDomainPrefix(msg);
		msg.networkIn = order.networkIn;
		msg.networkOut = order.networkOut;
		for (i = 0; i < 32; ++i) msg.tokenIn.set(i, order.tokenIn.get(i));
		for (i = 0; i < 32; ++i) msg.tokenOut.set(i, order.tokenOut.get(i));
		tmpIdBytes.setMem(order.fromAddress);
		for (i = 0; i < 32; ++i) msg.fromAddress.set(i, tmpIdBytes.get(i));
		tmpIdBytes.setMem(order.toAddress);
		for (i = 0; i < 32; ++i) msg.toAddress.set(i, tmpIdBytes.get(i));
		msg.amount = order.amount;
		msg.relayerFee = order.relayerFee;
		for (i = 0; i < 32; ++i) msg.nonce.set(i, order.nonce.get(i));
		msg.orderEra = order.orderEra;
	}

	// Popcount for uint8 approvedMask (used by multisig approval tracking)
	inline static uint8 countBitsUint8(uint8 mask, uint8 i)
	{
		uint8 count = 0;
		for (i = 0; i < 8; ++i)
		{
			if (mask & (uint8)(1u << i)) ++count;
		}
		return count;
	}

	// Check if caller is in the admin array
	inline static bool isAdmin(const QPI::ContractState<StateData, CONTRACT_INDEX>& state, const id& who, uint32 i)
	{
		for (i = 0; i < QSB_MAX_ADMINS; ++i)
		{
			if (!isZero(state.get().admins.get(i)) && state.get().admins.get(i) == who)
				return true;
		}
		return false;
	}

	// Find admin slot index; returns NULL_INDEX if not found
	inline static sint64 findAdminIndex(const QPI::ContractState<StateData, CONTRACT_INDEX>& state, const id& who, uint32 i)
	{
		for (i = 0; i < QSB_MAX_ADMINS; ++i)
		{
			if (!isZero(state.get().admins.get(i)) && state.get().admins.get(i) == who)
				return (sint64)i;
		}
		return NULL_INDEX;
	}

	// Check if caller is admin or has pauser role
	inline static bool isAdminOrPauser(const QPI::ContractState<StateData, CONTRACT_INDEX>& state, const id& who, uint32 i)
	{
		if (isAdmin(state, who, 0))
			return true;

		for (i = 0; i < state.get().pausers.capacity(); ++i)
		{
			if (state.get().pausers.get(i).active && state.get().pausers.get(i).account == who)
				return true;
		}
		return false;
	}

	// Cancel all pending (active) proposals — called when admin set changes
	inline static void cancelAllPendingProposals(QPI::ContractState<StateData, CONTRACT_INDEX>& state, uint32 i)
	{
		AdminProposal prop;
		for (i = 0; i < QSB_MAX_PROPOSALS; ++i)
		{
			prop = state.get().proposals.get(i);
			if (prop.active)
			{
				prop.active = 0;
				state.mut().proposals.set(i, prop);
			}
		}
	}

	// Execute the payload of an approved proposal. Returns true on success.
	// Pure state mutation — no qpi access.
	inline static bool executeProposalPayload(QPI::ContractState<StateData, CONTRACT_INDEX>& state, const AdminProposal& prop, uint32 i)
	{
		RoleEntry entry;
		sint64 idx;

		if (prop.proposalType == QSBPropAddAdmin)
		{
			for (i = 0; i < QSB_MAX_ADMINS; ++i)
			{
				if (isZero(state.get().admins.get(i)))
				{
					state.mut().admins.set(i, prop.targetId);
					state.mut().adminCount = state.get().adminCount + 1;
					return true;
				}
			}
			return false;
		}
		else if (prop.proposalType == QSBPropRemoveAdmin)
		{
			idx = findAdminIndex(state, prop.targetId, 0);
			state.mut().admins.set((uint32)idx, NULL_ID);
			state.mut().adminCount = state.get().adminCount - 1;
			return true;
		}
		else if (prop.proposalType == QSBPropSetAdminThreshold)
		{
			state.mut().adminThreshold = prop.newAdminThreshold;
			return true;
		}
		else if (prop.proposalType == QSBPropAddRole)
		{
			if (prop.role == (uint8)Role::Oracle)
			{
				if (findOracleIndex(state, prop.targetId, 0) != NULL_INDEX)
					return true;
				for (i = 0; i < state.get().oracles.capacity(); ++i)
				{
					entry = state.get().oracles.get(i);
					if (!entry.active)
					{
						entry.account = prop.targetId;
						entry.active  = true;
						state.mut().oracles.set(i, entry);
						++state.mut().oracleCount;
						return true;
					}
				}
				return false;
			}
			else if (prop.role == (uint8)Role::Pauser)
			{
				if (findPauserIndex(state, prop.targetId, 0) != NULL_INDEX)
					return true;
				for (i = 0; i < state.get().pausers.capacity(); ++i)
				{
					entry = state.get().pausers.get(i);
					if (!entry.active)
					{
						entry.account = prop.targetId;
						entry.active  = true;
						state.mut().pausers.set(i, entry);
						++state.mut().pauserCount;
						return true;
					}
				}
				return false;
			}
			return false;
		}
		else if (prop.proposalType == QSBPropRemoveRole)
		{
			if (prop.role == (uint8)Role::Oracle)
			{
				idx = findOracleIndex(state, prop.targetId, 0);
				if (idx == NULL_INDEX)
					return true;
				entry = state.get().oracles.get((uint32)idx);
				entry.active = false;
				state.mut().oracles.set((uint32)idx, entry);
				if (state.get().oracleCount > 0) --state.mut().oracleCount;
				return true;
			}
			else if (prop.role == (uint8)Role::Pauser)
			{
				idx = findPauserIndex(state, prop.targetId, 0);
				if (idx == NULL_INDEX)
					return true;
				entry = state.get().pausers.get((uint32)idx);
				entry.active = false;
				state.mut().pausers.set((uint32)idx, entry);
				if (state.get().pauserCount > 0) --state.mut().pauserCount;
				return true;
			}
			return false;
		}
		else if (prop.proposalType == QSBPropEditOracleThreshold)
		{
			state.mut().oracleThreshold = prop.newOracleThreshold;
			return true;
		}
		else if (prop.proposalType == QSBPropEditFeeParameters)
		{
			if (prop.bpsFee != 0 && prop.bpsFee <= QSB_MAX_BPS_FEE)
				state.mut().bpsFee = prop.bpsFee;
			if (prop.protocolFee != 0 && prop.protocolFee <= QSB_MAX_PROTOCOL_FEE)
				state.mut().protocolFee = prop.protocolFee;
			if (!isZero(prop.protocolFeeRecipient))
				state.mut().protocolFeeRecipient = prop.protocolFeeRecipient;
			if (!isZero(prop.oracleFeeRecipient))
				state.mut().oracleFeeRecipient = prop.oracleFeeRecipient;
			return true;
		}
		else if (prop.proposalType == QSBPropUnpause)
		{
			state.mut().paused = false;
			return true;
		}
		return false;
	}

	// Find oracle index; returns NULL_INDEX if not found
	inline static sint64 findOracleIndex(const QPI::ContractState<StateData, CONTRACT_INDEX>& state, const id& account, uint32 i)
	{
		for (i = 0; i < state.get().oracles.capacity(); ++i)
		{
			if (state.get().oracles.get(i).active && state.get().oracles.get(i).account == account)
				return (sint32)i;
		}
		return NULL_INDEX;
	}

	// Find pauser index; returns NULL_INDEX if not found
	inline static sint64 findPauserIndex(const QPI::ContractState<StateData, CONTRACT_INDEX>& state, const id& account, uint32 i)
	{
		for (i = 0; i < state.get().pausers.capacity(); ++i)
		{
			if (state.get().pausers.get(i).active && state.get().pausers.get(i).account == account)
				return (sint32)i;
		}
		return NULL_INDEX;
	}

	// Mark an orderHash as filled (idempotent, ring-buffer storage)
	inline static void markOrderFilled(QPI::ContractState<StateData, CONTRACT_INDEX>& state, const OrderHash& hash, uint32 i, uint32 j, bool same, FilledOrderEntry& entry)
	{
		// First, see if it already exists
		for (i = 0; i < state.get().filledOrders.capacity(); ++i)
		{
			entry = state.get().filledOrders.get(i);
			if (entry.used)
			{
				same = true;
				for (j = 0; j < hash.capacity(); ++j)
				{
					if (entry.hash.get(j) != hash.get(j))
					{
						same = false;
						break;
					}
				}
				if (same)
					return;
			}
		}

		// Otherwise, insert into the next ring-buffer slot and advance the index.
		i = state.get().lastFilledOrdersNextOverwriteIdx;
		entry = state.get().filledOrders.get(i);
		entry.hash = hash;
		entry.used = true;
		state.mut().filledOrders.set(i, entry);
		j = (state.get().lastFilledOrdersNextOverwriteIdx + 1) & (QSB_MAX_FILLED_ORDERS - 1);
		state.mut().lastFilledOrdersNextOverwriteIdx = j;
		if (j == 0)
		{
			// On ring buffer wrap: preserve current buffer as prev, clear current, advance era.
			state.mut().filledOrdersPrev = state.get().filledOrders;
			setMemory(state.mut().filledOrders, 0);
			state.mut().orderEra = state.get().orderEra + 1;
		}
	}

	// Check whether an orderHash has already been filled (checks current and previous era buffers)
	inline static bit isOrderFilled(const QPI::ContractState<StateData, CONTRACT_INDEX>& state, const OrderHash& hash, uint32 i, uint32 j, bool same, FilledOrderEntry& entry)
	{
		for (i = 0; i < state.get().filledOrders.capacity(); ++i)
		{
			entry = state.get().filledOrders.get(i);
			if (!entry.used)
				continue;

			same = true;
			for (j = 0; j < hash.capacity(); ++j)
			{
				if (entry.hash.get(j) != hash.get(j))
				{
					same = false;
					break;
				}
			}
			if (same)
				return true;
		}
		for (i = 0; i < state.get().filledOrdersPrev.capacity(); ++i)
		{
			entry = state.get().filledOrdersPrev.get(i);
			if (!entry.used)
				continue;

			same = true;
			for (j = 0; j < hash.capacity(); ++j)
			{
				if (entry.hash.get(j) != hash.get(j))
				{
					same = false;
					break;
				}
			}
			if (same)
				return true;
		}
		return false;
	}

	// Find index of locked order by nonce; returns NULL_INDEX if not found
	inline static sint64 findLockedOrderIndexByNonce(const QPI::ContractState<StateData, CONTRACT_INDEX>& state, uint32 nonce, uint32 i)
	{
		for (i = 0; i < QSB_MAX_LOCKED_ORDERS; ++i)
		{
			if (state.get().lockedOrders.get(i).active && state.get().lockedOrders.get(i).nonce == nonce)
				return (sint32)i;
		}
		return NULL_INDEX;
	}


public:
	// ---------------------------------------------------------------------
	// Core user procedures
	// ---------------------------------------------------------------------

	struct Lock_locals
	{
		id digest;
		Order tmpOrder;
		LockedOrderEntry entry;
		QSBOrderMessage msgBuffer;
		OrderHash tmpIdBytes;
		uint32 i;
		QSBLogLockMessage logMsg;
	};

	PUBLIC_PROCEDURE_WITH_LOCALS(Lock)
	{
		locals.logMsg._contractIndex = SELF_INDEX;
		locals.logMsg._type = QSBLogLock;
		locals.logMsg.from = qpi.invocator();
		copyFromBuffer(locals.logMsg.to, input.toAddress);
		locals.logMsg.amount = input.amount;
		locals.logMsg.relayerFee = input.relayerFee;
		locals.logMsg.networkOut = input.networkOut;
		locals.logMsg.nonce = input.nonce;
		setMemory(locals.logMsg.orderHash, 0);
		locals.logMsg.success = 0;
		locals.logMsg.reasonCode = QSBReasonNone;
		locals.logMsg._terminator = 0;

		output.success = false;
		setMemory(output.orderHash, 0);

		// Must not be paused
		if (state.get().paused)
		{
			// Refund attached funds if any
			if (qpi.invocationReward() > 0)
			{
				qpi.transfer(qpi.invocator(), qpi.invocationReward());
			}
			locals.logMsg.reasonCode = QSBReasonPaused;
			LOG_INFO(locals.logMsg);
			return;
		}

		// Basic validation
		if (input.amount == 0 || input.relayerFee >= input.amount)
		{
			if (qpi.invocationReward() > 0)
			{
				qpi.transfer(qpi.invocator(), qpi.invocationReward());
			}
			locals.logMsg.reasonCode = QSBReasonInvalidAmount;
			LOG_INFO(locals.logMsg);
			return;
		}

		// Ensure funds sent with call match the amount to be locked
		if (qpi.invocationReward() < (sint64)input.amount)
		{
			if (qpi.invocationReward() > 0)
			{
				qpi.transfer(qpi.invocator(), qpi.invocationReward());
			}
			locals.logMsg.reasonCode = QSBReasonInsufficientReward;
			LOG_INFO(locals.logMsg);
			return;
		}

		// Any excess over `amount` is refunded
		if (qpi.invocationReward() > (sint64)input.amount)
		{
			qpi.transfer(qpi.invocator(), qpi.invocationReward() - input.amount);
		}

		// Funds equal to `amount` now remain locked in the contract balance

		// Ensure nonce unused
		if (findLockedOrderIndexByNonce(state, input.nonce, 0) != NULL_INDEX)
		{
			// Nonce already used; reject
			qpi.transfer(qpi.invocator(), input.amount);
			locals.logMsg.reasonCode = QSBReasonNonceUsed;
			LOG_INFO(locals.logMsg);
			return;
		}

		locals.tmpOrder.networkIn = 1;
		locals.tmpOrder.networkOut = input.networkOut;
		setMemory(locals.tmpOrder.tokenIn, 0);
		setMemory(locals.tmpOrder.tokenOut, 0);
		locals.tmpOrder.fromAddress = qpi.invocator();
		locals.tmpOrder.toAddress = NULL_ID;
		locals.tmpOrder.amount = input.amount;
		locals.tmpOrder.relayerFee = input.relayerFee;
		setMemory(locals.tmpOrder.nonce, 0);
		locals.tmpOrder.nonce.set(0, (uint8)(input.nonce & 0xFF));
		locals.tmpOrder.nonce.set(1, (uint8)((input.nonce >> 8) & 0xFF));
		locals.tmpOrder.nonce.set(2, (uint8)((input.nonce >> 16) & 0xFF));
		locals.tmpOrder.nonce.set(3, (uint8)((input.nonce >> 24) & 0xFF));
		locals.tmpOrder.orderEra = state.get().orderEra;

		buildOrderMessage(locals.msgBuffer, locals.tmpOrder, locals.tmpIdBytes, locals.i);
		locals.digest = qpi.K12(locals.msgBuffer);
		digestToOrderHash(locals.digest, output.orderHash);
		locals.logMsg.orderHash = output.orderHash;
		locals.logMsg.orderEra = state.get().orderEra;

		// Persist locked order in ring buffer. Oldest slot is overwritten when the buffer is full;
		// by the time the ring wraps (1024 orders), off-chain tooling has indexed earlier entries.
		locals.entry.active = true;
		locals.entry.sender = qpi.invocator();
		locals.entry.networkOut = input.networkOut;
		locals.entry.amount = input.amount;
		locals.entry.relayerFee = input.relayerFee;
		locals.entry.nonce = input.nonce;
		copyMemory(locals.entry.toAddress, input.toAddress);
		locals.entry.orderHash = output.orderHash;
		locals.entry.lockEpoch = qpi.epoch();
		locals.entry.orderEra = state.get().orderEra;
		locals.entry.overrideLockCount = 0;
		state.mut().lockedOrders.set(state.get().lastLockedOrdersNextOverwriteIdx, locals.entry);
		state.mut().lastLockedOrdersNextOverwriteIdx = (state.get().lastLockedOrdersNextOverwriteIdx + 1) & (QSB_MAX_LOCKED_ORDERS - 1);

		output.success = true;
		locals.logMsg.success = 1;
		locals.logMsg.reasonCode = QSBReasonNone;
		LOG_INFO(locals.logMsg);
	}

	struct OverrideLock_locals
	{
		LockedOrderEntry entry;
		Order tmpOrder;
		id digest;
		QSBOrderMessage msgBuffer;
		OrderHash tmpIdBytes;
		sint64 idx;
		uint32 i;
		QSBLogOverrideLockMessage logMsg;
	};

	PUBLIC_PROCEDURE_WITH_LOCALS(OverrideLock)
	{
		locals.logMsg._contractIndex = SELF_INDEX;
		locals.logMsg._type = QSBLogOverrideLock;
		locals.logMsg.from = qpi.invocator();
		setMemory(locals.logMsg.to, 0);
		locals.logMsg.amount = 0;
		locals.logMsg.relayerFee = 0;
		locals.logMsg.networkOut = 0;
		locals.logMsg.nonce = input.nonce;
		setMemory(locals.logMsg.orderHash, 0);
		locals.logMsg.success = 0;
		locals.logMsg.reasonCode = QSBReasonNone;
		locals.logMsg._terminator = 0;
		output.success = false;
		setMemory(output.orderHash, 0);

		// Always refund invocationReward (locking was done in original lock() call)
		if (qpi.invocationReward() > 0)
		{
			qpi.transfer(qpi.invocator(), qpi.invocationReward());
		}

		// Contract must not be paused
		if (state.get().paused)
		{
			locals.logMsg.reasonCode = QSBReasonPaused;
			LOG_INFO(locals.logMsg);
			return;
		}

		// Find existing order by nonce
		locals.idx = findLockedOrderIndexByNonce(state, input.nonce, 0);
		if (locals.idx == NULL_INDEX)
		{
			locals.logMsg.reasonCode = QSBReasonOrderNotFound;
			LOG_INFO(locals.logMsg);
			return;
		}

		locals.entry = state.get().lockedOrders.get((uint32)locals.idx);

		// Only original sender can override
		if (locals.entry.sender != qpi.invocator())
		{
			locals.logMsg.reasonCode = QSBReasonNotSender;
			LOG_INFO(locals.logMsg);
			return;
		}

		// Enforce per-order override attempt cap
		if (locals.entry.overrideLockCount >= QSB_OVERRIDE_LOCK_MAX_ATTEMPTS)
		{
			locals.logMsg.reasonCode = QSBReasonOverrideLimitReached;
			LOG_INFO(locals.logMsg);
			return;
		}

		// Validate new relayer fee
		if (input.relayerFee >= locals.entry.amount)
		{
			locals.logMsg.reasonCode = QSBReasonBadRelayerFee;
			LOG_INFO(locals.logMsg);
			return;
		}

		// Update mutable fields
		copyMemory(locals.entry.toAddress, input.toAddress);
		locals.entry.relayerFee = input.relayerFee;

		locals.tmpOrder.networkIn = 1;
		locals.tmpOrder.networkOut = locals.entry.networkOut;
		setMemory(locals.tmpOrder.tokenIn, 0);
		setMemory(locals.tmpOrder.tokenOut, 0);
		locals.tmpOrder.fromAddress = locals.entry.sender;
		locals.tmpOrder.toAddress = NULL_ID;
		locals.tmpOrder.amount = locals.entry.amount;
		locals.tmpOrder.relayerFee = locals.entry.relayerFee;
		setMemory(locals.tmpOrder.nonce, 0);
		locals.tmpOrder.nonce.set(0, (uint8)(locals.entry.nonce & 0xFF));
		locals.tmpOrder.nonce.set(1, (uint8)((locals.entry.nonce >> 8) & 0xFF));
		locals.tmpOrder.nonce.set(2, (uint8)((locals.entry.nonce >> 16) & 0xFF));
		locals.tmpOrder.nonce.set(3, (uint8)((locals.entry.nonce >> 24) & 0xFF));
		locals.tmpOrder.orderEra = locals.entry.orderEra;  // preserve original era

		buildOrderMessage(locals.msgBuffer, locals.tmpOrder, locals.tmpIdBytes, locals.i);
		locals.digest = qpi.K12(locals.msgBuffer);
		digestToOrderHash(locals.digest, locals.entry.orderHash);
		output.orderHash = locals.entry.orderHash;
		locals.logMsg.orderHash = locals.entry.orderHash;
		locals.logMsg.orderEra = locals.entry.orderEra;

		locals.entry.overrideLockCount++;
		state.mut().lockedOrders.set((uint32)locals.idx, locals.entry);
		output.success = true;
		copyFromBuffer(locals.logMsg.to, input.toAddress);
		locals.logMsg.amount = locals.entry.amount;
		locals.logMsg.relayerFee = locals.entry.relayerFee;
		locals.logMsg.networkOut = locals.entry.networkOut;
		locals.logMsg.success = 1;
		locals.logMsg.reasonCode = QSBReasonNone;
		LOG_INFO(locals.logMsg);
	}

	// View helpers
	PUBLIC_FUNCTION(GetConfig)
	{
		output.adminCount     = state.get().adminCount;
		output.adminThreshold = state.get().adminThreshold;
		output.admins         = state.get().admins;
		output.protocolFeeRecipient = state.get().protocolFeeRecipient;
		output.oracleFeeRecipient   = state.get().oracleFeeRecipient;
		output.bpsFee         = state.get().bpsFee;
		output.protocolFee    = state.get().protocolFee;
		output.oracleCount    = state.get().oracleCount;
		output.pauserCount    = state.get().pauserCount;
		output.oracleThreshold = state.get().oracleThreshold;
		output.paused         = state.get().paused;
		output.orderEra       = state.get().orderEra;
	}

	PUBLIC_FUNCTION(GetProposal)
	{
		output.exists = false;
		if (input.proposalId < QSB_MAX_PROPOSALS)
		{
			output.proposal = state.get().proposals.get(input.proposalId);
			output.exists   = output.proposal.active;
		}
	}

	struct GetProposals_locals { uint32 i; AdminProposal prop; };
	PUBLIC_FUNCTION_WITH_LOCALS(GetProposals)
	{
		output.count = 0;
		setMemory(output.proposals, 0);
		for (locals.i = 0; locals.i < QSB_MAX_PROPOSALS; ++locals.i)
		{
			locals.prop = state.get().proposals.get(locals.i);
			if (locals.prop.active)
			{
				output.proposals.set(output.count, locals.prop);
				++output.count;
			}
		}
	}

	PUBLIC_FUNCTION(IsOracle)
	{
		output.isOracle = (findOracleIndex(state, input.account, 0) != NULL_INDEX);
	}

	PUBLIC_FUNCTION(IsPauser)
	{
		output.isPauser = (findPauserIndex(state, input.account, 0) != NULL_INDEX);
	}

	struct GetLockedOrder_locals
	{
		sint64 idx;
	};

	PUBLIC_FUNCTION_WITH_LOCALS(GetLockedOrder)
	{
		locals.idx = findLockedOrderIndexByNonce(state, input.nonce, 0);
		output.exists = (locals.idx != NULL_INDEX);
		if (output.exists)
		{
			output.order = state.get().lockedOrders.get((uint32)locals.idx);
		}
	}

	struct IsOrderFilled_locals
	{
		FilledOrderEntry entry;
		bool same;
	};

	PUBLIC_FUNCTION_WITH_LOCALS(IsOrderFilled)
	{
		output.filled = isOrderFilled(state, input.hash, 0, 0, locals.same, locals.entry);
	}

	struct ComputeOrderHash_locals
	{
		id digest;
		QSBOrderMessage msgBuffer;
		OrderHash tmpIdBytes;
		uint32 i;
	};

	PUBLIC_FUNCTION_WITH_LOCALS(ComputeOrderHash)
	{
		buildOrderMessage(locals.msgBuffer, input.order, locals.tmpIdBytes, locals.i);
		locals.digest = qpi.K12(locals.msgBuffer);
		output.hash.setMem(locals.digest);
	}

	struct GetOracles_locals
	{
		uint32 i;
		RoleEntry entry;
	};

	PUBLIC_FUNCTION_WITH_LOCALS(GetOracles)
	{
		output.count = 0;
		setMemory(output.accounts, 0);
		for (locals.i = 0; locals.i < state.get().oracles.capacity() && output.count < output.accounts.capacity(); ++locals.i)
		{
			locals.entry = state.get().oracles.get(locals.i);
			if (locals.entry.active)
			{
				output.accounts.set(output.count, locals.entry.account);
				++output.count;
			}
		}
	}

	struct GetPausers_locals
	{
		uint32 i;
		RoleEntry entry;
	};

	PUBLIC_FUNCTION_WITH_LOCALS(GetPausers)
	{
		output.count = 0;
		setMemory(output.accounts, 0);
		for (locals.i = 0; locals.i < state.get().pausers.capacity() && output.count < output.accounts.capacity(); ++locals.i)
		{
			locals.entry = state.get().pausers.get(locals.i);
			if (locals.entry.active)
			{
				output.accounts.set(output.count, locals.entry.account);
				++output.count;
			}
		}
	}

	struct GetLockedOrders_locals
	{
		uint32 i;
		uint32 slot;
		uint32 totalActive;
		uint32 collected;
		uint32 effectiveLimit;
		LockedOrderEntry entry;
	};

	PUBLIC_FUNCTION_WITH_LOCALS(GetLockedOrders)
	{
		output.totalActive = 0;
		output.returned = 0;
		setMemory(output.entries, 0);
		locals.effectiveLimit = input.limit;
		if (locals.effectiveLimit > QSB_QUERY_MAX_PAGE_SIZE)
			locals.effectiveLimit = QSB_QUERY_MAX_PAGE_SIZE;
		locals.collected = 0;
		// Iterate most-recent-first: start one slot before the next write position
		for (locals.i = 0; locals.i < QSB_MAX_LOCKED_ORDERS; ++locals.i)
		{
			locals.slot = (state.get().lastLockedOrdersNextOverwriteIdx + QSB_MAX_LOCKED_ORDERS - 1 - locals.i) & (QSB_MAX_LOCKED_ORDERS - 1);
			locals.entry = state.get().lockedOrders.get(locals.slot);
			if (!locals.entry.active)
				continue;
			++locals.totalActive;
			if (locals.totalActive <= input.offset)
				continue;
			if (locals.collected >= locals.effectiveLimit)
				continue;
			output.entries.set(locals.collected, locals.entry);
			++locals.collected;
		}
		output.totalActive = locals.totalActive;
		output.returned = locals.collected;
	}

	struct GetFilledOrders_locals
	{
		uint32 i;
		uint32 slot;
		uint32 totalActive;
		uint32 collected;
		uint32 effectiveLimit;
		FilledOrderEntry entry;
	};

	PUBLIC_FUNCTION_WITH_LOCALS(GetFilledOrders)
	{
		output.totalActive = 0;
		output.returned = 0;
		setMemory(output.hashes, 0);
		locals.effectiveLimit = input.limit;
		if (locals.effectiveLimit > QSB_QUERY_MAX_PAGE_SIZE)
			locals.effectiveLimit = QSB_QUERY_MAX_PAGE_SIZE;
		locals.collected = 0;
		// Iterate most-recent-first: start one slot before the next write position
		for (locals.i = 0; locals.i < QSB_MAX_FILLED_ORDERS; ++locals.i)
		{
			locals.slot = (state.get().lastFilledOrdersNextOverwriteIdx + QSB_MAX_FILLED_ORDERS - 1 - locals.i) & (QSB_MAX_FILLED_ORDERS - 1);
			locals.entry = state.get().filledOrders.get(locals.slot);
			if (!locals.entry.used)
				continue;
			++locals.totalActive;
			if (locals.totalActive <= input.offset)
				continue;
			if (locals.collected >= locals.effectiveLimit)
				continue;
			output.hashes.set(locals.collected, locals.entry.hash);
			++locals.collected;
		}
		output.totalActive = locals.totalActive;
		output.returned = locals.collected;
	}

	struct Unlock_locals
	{
		id digest;
		OrderHash hash;
		QSBOrderMessage msgBuffer;
		OrderHash tmpIdBytes;
		uint32 validSignatureCount;
		uint32 requiredSignatures;
		FilledOrderEntry entry;
		Array<id, QSB_MAX_ORACLES> seenSigners;
		SignatureData sig;
		uint32 seenCount;
		uint32 i;
		uint32 j;
		uint64 netAmount;
		uint128 tmpMul;
		uint128 tmpMul2;
		uint64 bpsFeeAmount;
		uint64 protocolFeeAmount;
		uint64 oracleFeeAmount;
		uint64 recipientAmount;
		bool same;
		bool allTransfersOk;
		Entity entity;
		uint64 contractBalance;
		QSBLogUnlockMessage logMsg;
	};

	PUBLIC_PROCEDURE_WITH_LOCALS(Unlock)
	{	
		locals.logMsg._contractIndex = SELF_INDEX;
		locals.logMsg._type = QSBLogUnlock;
		setMemory(locals.logMsg.orderHash, 0);
		locals.logMsg.toAddress = input.order.toAddress;
		locals.logMsg.amount = input.order.amount;
		locals.logMsg.relayerFee = input.order.relayerFee;
		locals.logMsg.relayer = qpi.invocator();
		locals.logMsg.orderEra = input.order.orderEra;
		locals.logMsg.success = 0;
		locals.logMsg.reasonCode = QSBReasonNone;
		locals.logMsg._terminator = 0;
		output.success = false;
		setMemory(output.orderHash, 0);

		// Must not be paused
		if (state.get().paused)
		{
			if (qpi.invocationReward() > 0)
			{
				qpi.transfer(qpi.invocator(), qpi.invocationReward());
			}
			locals.logMsg.reasonCode = QSBReasonPaused;
			LOG_INFO(locals.logMsg);
			return;
		}

		// Refund any invocation reward (relayer is paid from order.amount, not from reward)
		if (qpi.invocationReward() > 0)
		{
			qpi.transfer(qpi.invocator(), qpi.invocationReward());
		}

		// Basic order validation
		if (input.order.amount == 0 || input.order.relayerFee >= input.order.amount)
		{
			locals.logMsg.reasonCode = QSBReasonInvalidAmount;
			LOG_INFO(locals.logMsg);
			return;
		}

		// Check that the contract has enough balance to cover the full order amount.
		// This should never fail under normal circumstances (Lock keeps funds inside the contract), but we guard against any unexpected balance discrepancies.
		qpi.getEntity(SELF, locals.entity);
		if (locals.entity.incomingAmount < locals.entity.outgoingAmount)
		{
			locals.contractBalance = 0;
		}
		else
		{
			locals.contractBalance = locals.entity.incomingAmount - locals.entity.outgoingAmount;
		}

		if (locals.contractBalance < input.order.amount)
		{
			locals.logMsg.reasonCode = QSBReasonInsufficientReward;
			LOG_INFO(locals.logMsg);
			return;
		}

		// Era validation: accept current era or the immediately previous era.
		// Accepting era N-1 provides a grace window for in-flight orders signed just before
		// a ring-buffer wrap, while isOrderFilled checks both buffers to prevent replays.
		if (input.order.orderEra != state.get().orderEra &&
			!(state.get().orderEra > 0 && input.order.orderEra == state.get().orderEra - 1))
		{
			locals.logMsg.reasonCode = QSBReasonEraMismatch;
			LOG_INFO(locals.logMsg);
			return;
		}

		// NOTE: We intentionally do not require a matching lock() entry here.
		// Unlock is driven solely by:
		//  - oracle signatures over the burn/unlock order (on the other chain),
		//  - replay protection via filledOrders,
		//  - and balance checks on this contract.
		// This matches a fungible lock/mint ↔ burn/unlock bridge model where
		// minted tokens can be freely transferred and aggregated, and where
		// individual locks are not tied 1:1 to specific unlocks.

		// Serialize order with domain prefix and compute K12 digest
		buildOrderMessage(locals.msgBuffer, input.order, locals.tmpIdBytes, locals.i);
		locals.digest = qpi.K12(locals.msgBuffer);
		digestToOrderHash(locals.digest, locals.hash);
		output.orderHash = locals.hash;
		locals.logMsg.orderHash = locals.hash;

		// Ensure orderHash not yet filled
		if (isOrderFilled(state, locals.hash, 0, 0, 0, locals.entry))
		{
			locals.logMsg.reasonCode = QSBReasonAlreadyFilled;
			LOG_INFO(locals.logMsg);
			return;
		}

		// Verify oracle signatures against threshold
		if (state.get().oracleCount == 0 || input.numSignatures == 0)
		{
			locals.logMsg.reasonCode = QSBReasonNoOracles;
			LOG_INFO(locals.logMsg);
			return;
		}

		// requiredSignatures = ceil(oracleCount * oracleThreshold / 100)
		locals.tmpMul  = uint128(state.get().oracleCount) * uint128(state.get().oracleThreshold);
		locals.tmpMul2 = div(locals.tmpMul, uint128(100));
		locals.requiredSignatures = (uint32)locals.tmpMul2.low;
		if (locals.requiredSignatures * 100 < state.get().oracleCount * state.get().oracleThreshold)
		{
			++locals.requiredSignatures;
		}
		if (locals.requiredSignatures == 0)
		{
			locals.requiredSignatures = 1;
		}

		locals.validSignatureCount = 0;
		locals.seenCount = 0;

		for (locals.i = 0; locals.i < input.numSignatures && locals.i < input.signatures.capacity(); ++locals.i)
		{
			locals.sig = input.signatures.get(locals.i);

			// Check signer is authorized oracle
			if (findOracleIndex(state, locals.sig.signer, 0) == NULL_INDEX)
			{
				locals.logMsg.reasonCode = QSBReasonInvalidSignature;
				LOG_INFO(locals.logMsg); // unknown signer -> fail fast
				return;
			}

			// Check duplicates
			for (locals.j = 0; locals.j < locals.seenCount; ++locals.j)
			{
				if (locals.seenSigners.get(locals.j) == locals.sig.signer)
				{
					locals.logMsg.reasonCode = QSBReasonDuplicateSigner;
					LOG_INFO(locals.logMsg); // duplicate signer -> fail
					return;
				}
			}

			// Verify signature
			if (!qpi.signatureValidity(locals.sig.signer, locals.digest, locals.sig.signature))
			{
				locals.logMsg.reasonCode = QSBReasonInvalidSignature;
				LOG_INFO(locals.logMsg);
				return;
			}

			// Record signer and increment count
			if (locals.seenCount < locals.seenSigners.capacity())
			{
				locals.seenSigners.set(locals.seenCount, locals.sig.signer);
				++locals.seenCount;
			}
			++locals.validSignatureCount;
		}

		if (locals.validSignatureCount < locals.requiredSignatures)
		{
			locals.logMsg.reasonCode = QSBReasonThresholdFailed;
			LOG_INFO(locals.logMsg);
			return;
		}

		// -----------------------------------------------------------------
		// Fee calculations
		// -----------------------------------------------------------------
		locals.netAmount = input.order.amount - input.order.relayerFee;

		// bpsFeeAmount = netAmount * bpsFee / 10000
		locals.tmpMul  = uint128(locals.netAmount) * uint128(state.get().bpsFee);
		locals.tmpMul2 = div(locals.tmpMul, uint128(10000));
		locals.bpsFeeAmount = (uint64)locals.tmpMul2.low;

		// protocolFeeAmount = bpsFeeAmount * protocolFee / 100
		locals.tmpMul  = uint128(locals.bpsFeeAmount) * uint128(state.get().protocolFee);
		locals.tmpMul2 = div(locals.tmpMul, uint128(100));
		locals.protocolFeeAmount = (uint64)locals.tmpMul2.low;

		// oracleFeeAmount = bpsFeeAmount - protocolFeeAmount
		if (locals.bpsFeeAmount >= locals.protocolFeeAmount)
			locals.oracleFeeAmount = locals.bpsFeeAmount - locals.protocolFeeAmount;
		else
			locals.oracleFeeAmount = 0;

		// recipientAmount = netAmount - bpsFeeAmount
		if (locals.netAmount >= locals.bpsFeeAmount)
			locals.recipientAmount = locals.netAmount - locals.bpsFeeAmount;
		else
			locals.recipientAmount = 0;

		// -----------------------------------------------------------------
		// Mark order as filled BEFORE transfers to prevent replay.
		// If a transfer fails below, the order stays filled (no double-pay).
		// The balance check above guarantees the contract has enough funds.
		// -----------------------------------------------------------------
		markOrderFilled(state, locals.hash, 0, 0, 0, locals.entry);

		// -----------------------------------------------------------------
		// Token transfers
		// -----------------------------------------------------------------

		locals.allTransfersOk = true;

		// Recipient payout first (most important transfer)
		if (locals.recipientAmount > 0 && !isZero(input.order.toAddress))
		{
			if (qpi.transfer(input.order.toAddress, (sint64)locals.recipientAmount) < 0)
			{
				locals.allTransfersOk = false;
			}
		}

		// Relayer fee to caller
		if (input.order.relayerFee > 0)
		{
			if (qpi.transfer(qpi.invocator(), (sint64)input.order.relayerFee) < 0)
			{
				locals.allTransfersOk = false;
			}
		}

		// Protocol fee
		if (locals.protocolFeeAmount > 0 && !isZero(state.get().protocolFeeRecipient))
		{
			if (qpi.transfer(state.get().protocolFeeRecipient, (sint64)locals.protocolFeeAmount) < 0)
			{
				locals.allTransfersOk = false;
			}
		}

		// Oracle fee
		if (locals.oracleFeeAmount > 0 && !isZero(state.get().oracleFeeRecipient))
		{
			if (qpi.transfer(state.get().oracleFeeRecipient, (sint64)locals.oracleFeeAmount) < 0)
			{
				locals.allTransfersOk = false;
			}
		}

		if (!locals.allTransfersOk)
		{
			locals.logMsg.reasonCode = QSBReasonTransferFailed;
			LOG_INFO(locals.logMsg);
			return;
		}

		output.success = true;
		locals.logMsg.success = 1;
		locals.logMsg.reasonCode = QSBReasonNone;
		LOG_INFO(locals.logMsg);
	}

	// ---------------------------------------------------------------------
	// Admin procedures (multisig)
	// ---------------------------------------------------------------------

	struct Propose_locals
	{
		sint64 adminIdx;
		uint32 i;
		uint8  slotIdx;
		uint8  adminProposalCount;
		AdminProposal prop;
		bool   execOk;
		QSBLogProposalMessage logMsg;
	};

	PUBLIC_PROCEDURE_WITH_LOCALS(Propose)
	{
		output.success    = false;
		output.proposalId = 0;
		output.reasonCode = QSBReasonNone;

		if (qpi.invocationReward() > 0)
			qpi.transfer(qpi.invocator(), qpi.invocationReward());

		locals.adminIdx = findAdminIndex(state, qpi.invocator(), 0);
		if (locals.adminIdx == NULL_INDEX)
		{ output.reasonCode = QSBReasonNotAdmin; return; }

		if (input.proposalType == 0 || input.proposalType > QSBPropUnpause)
		{ output.reasonCode = QSBReasonInvalidRole; return; }

		// Per-type payload validation
		if (input.proposalType == QSBPropAddAdmin)
		{
			if (isZero(input.targetId))
			{ output.reasonCode = QSBReasonInvalidAdmin; return; }
			if (findAdminIndex(state, input.targetId, 0) != NULL_INDEX)
			{ output.reasonCode = QSBReasonAlreadyAdmin; return; }
			if (state.get().adminCount >= QSB_MAX_ADMINS)
			{ output.reasonCode = QSBReasonAdminFull; return; }
		}
		else if (input.proposalType == QSBPropRemoveAdmin)
		{
			if (isZero(input.targetId))
			{ output.reasonCode = QSBReasonInvalidAdmin; return; }
			if (findAdminIndex(state, input.targetId, 0) == NULL_INDEX)
			{ output.reasonCode = QSBReasonRoleMissing; return; }
			if (state.get().adminCount <= 1)
			{ output.reasonCode = QSBReasonWouldLockContract; return; }
			if ((state.get().adminCount - 1) < state.get().adminThreshold)
			{ output.reasonCode = QSBReasonWouldLockContract; return; }
		}
		else if (input.proposalType == QSBPropSetAdminThreshold)
		{
			if (input.newAdminThreshold == 0 || input.newAdminThreshold > state.get().adminCount)
			{ output.reasonCode = QSBReasonInvalidThreshold; return; }
		}
		else if (input.proposalType == QSBPropAddRole || input.proposalType == QSBPropRemoveRole)
		{
			if (isZero(input.targetId))
			{ output.reasonCode = QSBReasonInvalidAdmin; return; }
			if (input.role != (uint8)Role::Oracle && input.role != (uint8)Role::Pauser)
			{ output.reasonCode = QSBReasonInvalidRole; return; }
		}
		else if (input.proposalType == QSBPropEditOracleThreshold)
		{
			if (input.newOracleThreshold == 0 || input.newOracleThreshold > 100)
			{ output.reasonCode = QSBReasonInvalidThreshold; return; }
		}
		else if (input.proposalType == QSBPropEditFeeParameters)
		{
			if (input.bpsFee > QSB_MAX_BPS_FEE || input.protocolFee > QSB_MAX_PROTOCOL_FEE)
			{ output.reasonCode = QSBReasonInvalidFeeParams; return; }
		}
		else if (input.proposalType != QSBPropUnpause)
		{ output.reasonCode = QSBReasonInvalidProposalType; return; }

		// Enforce per-admin concurrent proposal cap
		locals.adminProposalCount = 0;
		for (locals.i = 0; locals.i < QSB_MAX_PROPOSALS; ++locals.i)
		{
			locals.prop = state.get().proposals.get(locals.i);
			if (locals.prop.active && locals.prop.proposer == qpi.invocator())
				++locals.adminProposalCount;
		}
		if (locals.adminProposalCount >= QSB_MAX_PROPOSALS_PER_ADMIN)
		{ output.reasonCode = QSBReasonTooManyProposals; return; }

		// Find free proposal slot
		locals.slotIdx = (uint8)QSB_MAX_PROPOSALS;
		for (locals.i = 0; locals.i < QSB_MAX_PROPOSALS; ++locals.i)
		{
			if (!state.get().proposals.get(locals.i).active)
			{ locals.slotIdx = (uint8)locals.i; break; }
		}
		if (locals.slotIdx >= QSB_MAX_PROPOSALS)
		{ output.reasonCode = QSBReasonProposalFull; return; }

		// Build proposal; proposer auto-approves
		setMemory(locals.prop, 0);
		locals.prop.proposalType        = input.proposalType;
		locals.prop.active              = 1;
		locals.prop.executed            = 0;
		locals.prop.proposer            = qpi.invocator();
		locals.prop.createdEpoch        = qpi.epoch();
		locals.prop.approvedMask        = (uint8)(1u << (uint8)locals.adminIdx);
		locals.prop.approvalCount       = 1;
		locals.prop.targetId            = input.targetId;
		locals.prop.role                = input.role;
		locals.prop.newAdminThreshold   = input.newAdminThreshold;
		locals.prop.newOracleThreshold  = input.newOracleThreshold;
		locals.prop.protocolFeeRecipient = input.protocolFeeRecipient;
		locals.prop.oracleFeeRecipient   = input.oracleFeeRecipient;
		locals.prop.bpsFee              = input.bpsFee;
		locals.prop.protocolFee         = input.protocolFee;
		state.mut().proposals.set(locals.slotIdx, locals.prop);
		output.proposalId = locals.slotIdx;
		output.success    = true;

		// Execute immediately when threshold == 1 (single-admin or bootstrap mode)
		if (state.get().adminThreshold <= 1)
		{
			locals.execOk = executeProposalPayload(state, locals.prop, 0);
			locals.prop = state.get().proposals.get(locals.slotIdx);
			locals.prop.active   = 0;
			locals.prop.executed = locals.execOk ? 1 : 0;
			state.mut().proposals.set(locals.slotIdx, locals.prop);
			if (locals.execOk &&
				(input.proposalType == QSBPropAddAdmin ||
				 input.proposalType == QSBPropRemoveAdmin ||
				 input.proposalType == QSBPropSetAdminThreshold))
			{
				cancelAllPendingProposals(state, 0);
			}
			output.success = locals.execOk;
		}

		locals.logMsg._contractIndex = SELF_INDEX;
		locals.logMsg._type          = QSBLogProposalCreated;
		locals.logMsg.proposalId     = output.proposalId;
		locals.logMsg.proposalType   = input.proposalType;
		locals.logMsg.proposer       = qpi.invocator();
		locals.logMsg.actor          = qpi.invocator();
		locals.logMsg.approvalCount  = 1;
		locals.logMsg.success        = output.success ? 1 : 0;
		locals.logMsg.reasonCode     = output.reasonCode;
		locals.logMsg._terminator    = 0;
		LOG_INFO(locals.logMsg);
	}

	struct ApproveProposal_locals
	{
		sint64 adminIdx;
		uint8  bitPos;
		uint8  propType;
		AdminProposal prop;
		bool   execOk;
		QSBLogProposalMessage logMsg;
	};

	PUBLIC_PROCEDURE_WITH_LOCALS(ApproveProposal)
	{
		output.success    = false;
		output.executed   = false;
		output.reasonCode = QSBReasonNone;

		if (qpi.invocationReward() > 0)
			qpi.transfer(qpi.invocator(), qpi.invocationReward());

		locals.adminIdx = findAdminIndex(state, qpi.invocator(), 0);
		if (locals.adminIdx == NULL_INDEX)
		{ output.reasonCode = QSBReasonNotAdmin; return; }

		if (input.proposalId >= QSB_MAX_PROPOSALS)
		{ output.reasonCode = QSBReasonProposalNotFound; return; }

		locals.prop = state.get().proposals.get(input.proposalId);

		if (!locals.prop.active)
		{ output.reasonCode = QSBReasonProposalNotFound; return; }

		if (qpi.epoch() > locals.prop.createdEpoch + QSB_PROPOSAL_EXPIRY_EPOCHS)
		{
			locals.prop.active = 0;
			state.mut().proposals.set(input.proposalId, locals.prop);
			output.reasonCode = QSBReasonProposalExpired;
			return;
		}

		locals.bitPos = (uint8)locals.adminIdx;
		if (locals.bitPos < 8 && (locals.prop.approvedMask & (uint8)(1u << locals.bitPos)))
		{ output.reasonCode = QSBReasonAlreadyApproved; return; }

		locals.prop.approvedMask |= (uint8)(1u << locals.bitPos);
		locals.prop.approvalCount = countBitsUint8(locals.prop.approvedMask, 0);
		state.mut().proposals.set(input.proposalId, locals.prop);
		output.success = true;

		if (locals.prop.approvalCount >= state.get().adminThreshold)
		{
			locals.propType = locals.prop.proposalType;
			locals.execOk   = executeProposalPayload(state, locals.prop, 0);
			locals.prop = state.get().proposals.get(input.proposalId);
			locals.prop.active   = 0;
			locals.prop.executed = locals.execOk ? 1 : 0;
			state.mut().proposals.set(input.proposalId, locals.prop);
			output.executed = true;
			if (locals.execOk &&
				(locals.propType == QSBPropAddAdmin ||
				 locals.propType == QSBPropRemoveAdmin ||
				 locals.propType == QSBPropSetAdminThreshold))
			{
				cancelAllPendingProposals(state, 0);
			}
		}

		locals.logMsg._contractIndex = SELF_INDEX;
		locals.logMsg._type          = output.executed ? QSBLogProposalExecuted : QSBLogProposalApproved;
		locals.logMsg.proposalId     = input.proposalId;
		locals.logMsg.proposalType   = locals.prop.proposalType;
		locals.logMsg.proposer       = locals.prop.proposer;
		locals.logMsg.actor          = qpi.invocator();
		locals.logMsg.approvalCount  = locals.prop.approvalCount;
		locals.logMsg.success        = output.success ? 1 : 0;
		locals.logMsg.reasonCode     = output.reasonCode;
		locals.logMsg._terminator    = 0;
		LOG_INFO(locals.logMsg);
	}

	struct CancelProposal_locals
	{
		AdminProposal prop;
		QSBLogProposalMessage logMsg;
	};

	PUBLIC_PROCEDURE_WITH_LOCALS(CancelProposal)
	{
		output.success    = false;
		output.reasonCode = QSBReasonNone;

		if (qpi.invocationReward() > 0)
			qpi.transfer(qpi.invocator(), qpi.invocationReward());

		if (!isAdmin(state, qpi.invocator(), 0))
		{ output.reasonCode = QSBReasonNotAdmin; return; }

		if (input.proposalId >= QSB_MAX_PROPOSALS)
		{ output.reasonCode = QSBReasonProposalNotFound; return; }

		locals.prop = state.get().proposals.get(input.proposalId);

		if (!locals.prop.active)
		{ output.reasonCode = QSBReasonProposalNotFound; return; }

		if (locals.prop.proposer != qpi.invocator())
		{ output.reasonCode = QSBReasonNotProposer; return; }

		locals.prop.active = 0;
		state.mut().proposals.set(input.proposalId, locals.prop);
		output.success = true;

		locals.logMsg._contractIndex = SELF_INDEX;
		locals.logMsg._type          = QSBLogProposalCancelled;
		locals.logMsg.proposalId     = input.proposalId;
		locals.logMsg.proposalType   = locals.prop.proposalType;
		locals.logMsg.proposer       = locals.prop.proposer;
		locals.logMsg.actor          = qpi.invocator();
		locals.logMsg.approvalCount  = locals.prop.approvalCount;
		locals.logMsg.success        = 1;
		locals.logMsg.reasonCode     = QSBReasonNone;
		locals.logMsg._terminator    = 0;
		LOG_INFO(locals.logMsg);
	}

	struct Pause_locals
	{
		QSBLogPausedMessage logMsg;
	};

	PUBLIC_PROCEDURE_WITH_LOCALS(Pause)
	{
		output.success = false;

		if (qpi.invocationReward() > 0)
		{
			qpi.transfer(qpi.invocator(), qpi.invocationReward());
		}

		if (!isAdminOrPauser(state, qpi.invocator(), 0)) // Pause stays single-key (emergency brake)
		{
			locals.logMsg._contractIndex = SELF_INDEX;
			locals.logMsg._type = QSBLogPaused;
			locals.logMsg.caller = qpi.invocator();
			locals.logMsg.success = 0;
			locals.logMsg.reasonCode = QSBReasonNotAdminOrPauser;
			locals.logMsg._terminator = 0;
			LOG_INFO(locals.logMsg);
			return;
		}

		state.mut().paused = true;
		output.success = true;

		locals.logMsg._contractIndex = SELF_INDEX;
		locals.logMsg._type = QSBLogPaused;
		locals.logMsg.caller = qpi.invocator();
		locals.logMsg.success = 1;
		locals.logMsg.reasonCode = QSBReasonNone;
		locals.logMsg._terminator = 0;
		LOG_INFO(locals.logMsg);
	}

	REGISTER_USER_FUNCTIONS_AND_PROCEDURES()
	{
		// View functions
		REGISTER_USER_FUNCTION(GetConfig, 1);
		REGISTER_USER_FUNCTION(IsOracle, 2);
		REGISTER_USER_FUNCTION(IsPauser, 3);
		REGISTER_USER_FUNCTION(GetLockedOrder, 4);
		REGISTER_USER_FUNCTION(IsOrderFilled, 5);
		REGISTER_USER_FUNCTION(ComputeOrderHash, 6);
		REGISTER_USER_FUNCTION(GetOracles, 7);
		REGISTER_USER_FUNCTION(GetPausers, 8);
		REGISTER_USER_FUNCTION(GetLockedOrders, 9);
		REGISTER_USER_FUNCTION(GetFilledOrders, 10);
		REGISTER_USER_FUNCTION(GetProposal, 11);
		REGISTER_USER_FUNCTION(GetProposals, 12);

		// User procedures
		REGISTER_USER_PROCEDURE(Lock, 1);
		REGISTER_USER_PROCEDURE(OverrideLock, 2);
		REGISTER_USER_PROCEDURE(Unlock, 3);

		// Emergency pause — single-key, any admin or pauser
		REGISTER_USER_PROCEDURE(Pause, 14);

		// Multisig admin procedures
		REGISTER_USER_PROCEDURE(Propose, 20);
		REGISTER_USER_PROCEDURE(ApproveProposal, 21);
		REGISTER_USER_PROCEDURE(CancelProposal, 22);
	}

	// ---------------------------------------------------------------------
	// Epoch processing
	// ---------------------------------------------------------------------

	struct END_EPOCH_locals
	{
		uint32 i;
		AdminProposal prop;
	};

	END_EPOCH_WITH_LOCALS()
	{
		// Sweep expired proposals
		for (locals.i = 0; locals.i < QSB_MAX_PROPOSALS; ++locals.i)
		{
			locals.prop = state.get().proposals.get(locals.i);
			if (locals.prop.active &&
				qpi.epoch() > locals.prop.createdEpoch + QSB_PROPOSAL_EXPIRY_EPOCHS)
			{
				locals.prop.active = 0;
				state.mut().proposals.set(locals.i, locals.prop);
			}
		}
	}

	// ---------------------------------------------------------------------
	// Initialization
	// ---------------------------------------------------------------------

	INITIALIZE()
	{
		// Multisig admin setup — 2-of-2 from deployment.
		// Replace both keys with real production keys before mainnet deployment.
		// Admin 0: id(100, 200, 300, 400)  — test key, matches ADMIN in contract_qsb.cpp
		// Admin 1: id(101, 201, 301, 401)  — test key, matches ADMIN2 in contract_qsb.cpp
		setMemory(state.mut().admins, 0);
		state.mut().admins.set(0, id(100ULL, 200ULL, 300ULL, 400ULL));
		state.mut().admins.set(1, id(101ULL, 201ULL, 301ULL, 401ULL));
		state.mut().adminCount     = 2;
		state.mut().adminThreshold = 2;
		setMemory(state.mut().proposals, 0);

		state.mut().paused = false;

		state.mut().oracleThreshold                    = 67;
		state.mut().lastFilledOrdersNextOverwriteIdx   = 0;
		state.mut().lastLockedOrdersNextOverwriteIdx   = 0;
		state.mut().oracleCount                        = 0;
		state.mut().pauserCount                        = 0;

		setMemory(state.mut().oracles, 0);
		setMemory(state.mut().pausers, 0);
		setMemory(state.mut().filledOrders, 0);
		setMemory(state.mut().filledOrdersPrev, 0);
		setMemory(state.mut().lockedOrders, 0);

		state.mut().bpsFee               = 0;
		state.mut().protocolFee          = 0;
		state.mut().protocolFeeRecipient = NULL_ID;
		state.mut().oracleFeeRecipient   = NULL_ID;

		state.mut().orderEra = 0;
	}
};
