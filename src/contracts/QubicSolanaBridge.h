using namespace QPI;

// ---------------------------------------------------------------------
// Constants / configuration
// ---------------------------------------------------------------------

static constexpr uint32 QSB_MAX_ORACLES = 64;
static constexpr uint32 QSB_MAX_PAUSERS = 32;
static constexpr uint32 QSB_MAX_FILLED_ORDERS = 256; // QPI::Array requires power-of-2; 256 = 5x the ~50 max concurrent orders
static constexpr uint32 QSB_MAX_LOCKED_ORDERS = 1024;
static constexpr uint32 QSB_MAX_BPS_FEE = 1000;		// max 10% fee (1000 / 10000)
static constexpr uint32 QSB_MAX_PROTOCOL_FEE = 100; // max 100% of bps fee
static constexpr uint8 QSB_OVERRIDE_LOCK_MAX_ATTEMPTS = 3;
static constexpr uint32 QSB_MAX_UNLOCK_SIGNATURES = 8; // max sigs per Unlock call; keeps Unlock_input ≤ 1024 bytes

// Domain-prefixed order message for K12 hashing and signature verification.
// Layout: 245 bytes total. protocolName is padded to 16 (next power of 2 above 11).
struct QSBOrderMessage
{
	uint32 protocolNameLen;			  // 0:  always 11
	Array<uint8, 16> protocolName;	  // 4:  QubicBridge (11 used, 5 zero-padded)
	uint32 protocolVersionLen;		  // 20: always 1
	Array<uint8, 1> protocolVersion;  // 24: version byte (49 = ASCII '1')
	Array<uint8, 32> contractAddress; // 25: destination contract address (LE-padded index)
	uint32 networkIn;				  // 57
	uint32 networkOut;				  // 61
	Array<uint8, 32> tokenIn;		  // 65
	Array<uint8, 32> tokenOut;		  // 97
	Array<uint8, 32> fromAddress;	  // 129
	Array<uint8, 32> toAddress;		  // 161
	uint64 amount;					  // 193
	uint64 relayerFee;				  // 201
	Array<uint8, 32> nonce;			  // 209
	uint32 orderEra;				  // 241
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
static constexpr uint32 QSBLogProposalCreated = 11;
static constexpr uint32 QSBLogProposalApproved = 12;
static constexpr uint32 QSBLogProposalExecuted = 13;
static constexpr uint32 QSBLogProposalCancelled = 14;

// Multisig admin constants
static constexpr uint32 QSB_MAX_ADMINS = 8; // approvedMask is uint8; must stay ≤ 8
static constexpr uint32 QSB_MAX_PROPOSALS = 16;
static constexpr uint32 QSB_MAX_PROPOSALS_PER_ADMIN = 3;
static constexpr uint32 QSB_PROPOSAL_EXPIRY_EPOCHS = 4; // ~4 weeks

// Proposal types
static constexpr uint8 QSBPropAddAdmin = 1;
static constexpr uint8 QSBPropRemoveAdmin = 2;
static constexpr uint8 QSBPropSetAdminThreshold = 3;
static constexpr uint8 QSBPropAddRole = 4;
static constexpr uint8 QSBPropRemoveRole = 5;
static constexpr uint8 QSBPropEditOracleThreshold = 6;
static constexpr uint8 QSBPropEditFeeParameters = 7;
static constexpr uint8 QSBPropUnpause = 8;

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
static constexpr uint8 QSBReasonProposalNotFound = 25;
static constexpr uint8 QSBReasonProposalExpired = 26;
static constexpr uint8 QSBReasonAlreadyApproved = 27;
static constexpr uint8 QSBReasonProposalFull = 28;
static constexpr uint8 QSBReasonWouldLockContract = 29;
static constexpr uint8 QSBReasonNotProposer = 30;
static constexpr uint8 QSBReasonAlreadyAdmin = 31;
static constexpr uint8 QSBReasonAdminFull = 32;
static constexpr uint8 QSBReasonTooManyProposals = 33;
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
		id signer;					// oracle id (public key)
		Array<sint8, 64> signature; // raw 64-byte signature
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
		uint8 overrideLockCount; // at +161; 6 bytes padding follow to keep struct at 168 bytes
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
		uint8 proposalId;
		uint8 proposalType;
		id proposer;
		id actor;
		uint8 approvalCount;
		uint8 success;
		uint8 reasonCode;
		sint8 _terminator;
	};

	// Union-style: fields used depend on proposalType. Unused fields are zero.
	struct AdminProposal
	{
		uint8 proposalType; // QSBProp* constant
		uint8 active;		// 1 = slot in use
		uint8 executed;		// 1 = executed successfully

		id proposer;		 // admin who created this proposal
		uint32 createdEpoch; // for expiry: createdEpoch + QSB_PROPOSAL_EXPIRY_EPOCHS

		uint8 approvalCount; // cached popcount of approvedMask
		uint8 approvedMask;	 // bit i = admins[i] approved (max 8 admins)

		// Payload — fields used depend on proposalType
		id targetId;			  // AddAdmin, RemoveAdmin, AddRole/RemoveRole account
		uint8 role;				  // AddRole, RemoveRole: (uint8)Role::Oracle or Role::Pauser
		uint8 newAdminThreshold;  // SetAdminThreshold
		uint8 newOracleThreshold; // EditOracleThreshold
		id protocolFeeRecipient;
		id oracleFeeRecipient;
		uint32 bpsFee;
		uint32 protocolFee;
	};

	// ---------------------------------------------------------------------
	// User-facing I/O structures
	// ---------------------------------------------------------------------

	// 1) lock()
	struct Lock_input
	{
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
		uint8 role; // see Role enum
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

	typedef Pause_input Unpause_input;
	typedef Pause_output Unpause_output;

	// 9) editFeeParameters()
	struct EditFeeParameters_input
	{
		id protocolFeeRecipient; // updated when not zero-id
		id oracleFeeRecipient;	 // updated when not zero-id
		uint32 bpsFee;			 // basis points fee (0..10000)
		uint32 protocolFee;		 // share of BPS fee for protocol (0..100)
	};

	struct EditFeeParameters_output
	{
		bit success;
	};

	// Propose: create a typed admin proposal (proposer auto-approves)
	struct Propose_input
	{
		uint8 proposalType;
		id targetId;
		uint8 role;
		uint8 newAdminThreshold;
		uint8 newOracleThreshold;
		id protocolFeeRecipient;
		id oracleFeeRecipient;
		uint32 bpsFee;
		uint32 protocolFee;
	};
	struct Propose_output
	{
		uint8 proposalId; // slot index; valid only when success == true
		bit success;
		uint8 reasonCode;
	};

	struct ApproveProposal_input
	{
		uint8 proposalId;
	};
	struct ApproveProposal_output
	{
		bit success;
		bit executed;
		uint8 reasonCode;
	};

	struct CancelProposal_input
	{
		uint8 proposalId;
	};
	struct CancelProposal_output
	{
		bit success;
		uint8 reasonCode;
	};

	struct GetProposal_input
	{
		uint8 proposalId;
	};
	struct GetProposal_output
	{
		bit exists;
		AdminProposal proposal;
	};

	struct GetProposals_input
	{
	};
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
		uint8 adminCount;
		uint8 adminThreshold;
		Array<id, QSB_MAX_ADMINS> admins;
		id protocolFeeRecipient;
		id oracleFeeRecipient;
		uint32 bpsFee;
		uint32 protocolFee;
		uint32 oracleCount;
		uint32 pauserCount;
		uint8 oracleThreshold;
		bit paused;
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
		Array<id, QSB_MAX_ADMINS> admins; // zero entry = empty slot
		uint8 adminCount;				  // number of active admins
		uint8 adminThreshold;			  // M in M-of-N (always ≥ 1, always ≤ adminCount)
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
		uint8 oracleThreshold; // percent [1..100]
		bit paused;
		uint32 orderEra;
	};

protected:
	// ---------------------------------------------------------------------
	// Low-level helpers
	// ---------------------------------------------------------------------

	inline static void digestToOrderHash(const id &digest, OrderHash &outHash)
	{
		outHash.setMem(digest);
	}

	inline static void initDomainPrefix(QSBOrderMessage &msg)
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
		QSBOrderMessage &msg,
		const Order &order,
		OrderHash &tmpIdBytes)
	{
		uint32 i;
		initDomainPrefix(msg);
		msg.networkIn = order.networkIn;
		msg.networkOut = order.networkOut;
		for (i = 0; i < 32; ++i)
			msg.tokenIn.set(i, order.tokenIn.get(i));
		for (i = 0; i < 32; ++i)
			msg.tokenOut.set(i, order.tokenOut.get(i));
		tmpIdBytes.setMem(order.fromAddress);
		for (i = 0; i < 32; ++i)
			msg.fromAddress.set(i, tmpIdBytes.get(i));
		tmpIdBytes.setMem(order.toAddress);
		for (i = 0; i < 32; ++i)
			msg.toAddress.set(i, tmpIdBytes.get(i));
		msg.amount = order.amount;
		msg.relayerFee = order.relayerFee;
		for (i = 0; i < 32; ++i)
			msg.nonce.set(i, order.nonce.get(i));
		msg.orderEra = order.orderEra;
	}

	inline static uint8 countBitsUint8(uint8 mask)
	{
		uint8 count = 0;
		for (uint8 i = 0; i < 8; ++i)
		{
			if (mask & (uint8)(1u << i))
				++count;
		}
		return count;
	}

	inline static bool isAdmin(const QPI::ContractState<StateData, CONTRACT_INDEX> &state, const id &who)
	{
		for (uint32 i = 0; i < QSB_MAX_ADMINS; ++i)
		{
			if (!isZero(state.get().admins.get(i)) && state.get().admins.get(i) == who)
				return true;
		}
		return false;
	}

	inline static sint64 findAdminIndex(const QPI::ContractState<StateData, CONTRACT_INDEX> &state, const id &who)
	{
		for (uint32 i = 0; i < QSB_MAX_ADMINS; ++i)
		{
			if (!isZero(state.get().admins.get(i)) && state.get().admins.get(i) == who)
				return (sint64)i;
		}
		return NULL_INDEX;
	}

	inline static bool isAdminOrPauser(const QPI::ContractState<StateData, CONTRACT_INDEX> &state, const id &who)
	{
		if (isAdmin(state, who))
			return true;
		for (uint32 i = 0; i < state.get().pausers.capacity(); ++i)
		{
			if (state.get().pausers.get(i).active && state.get().pausers.get(i).account == who)
				return true;
		}
		return false;
	}

	// Cancel all pending proposals — called when the admin set changes to invalidate stale votes.
	inline static void cancelAllPendingProposals(QPI::ContractState<StateData, CONTRACT_INDEX> &state)
	{
		AdminProposal prop;
		for (uint32 i = 0; i < QSB_MAX_PROPOSALS; ++i)
		{
			prop = state.get().proposals.get(i);
			if (prop.active)
			{
				prop.active = 0;
				state.mut().proposals.set(i, prop);
			}
		}
	}

	// Pure state mutation — no qpi access.
	inline static bool executeProposalPayload(QPI::ContractState<StateData, CONTRACT_INDEX> &state, const AdminProposal &prop)
	{
		uint32 i;
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
			idx = findAdminIndex(state, prop.targetId);
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
				if (findOracleIndex(state, prop.targetId) != NULL_INDEX)
					return true;
				for (i = 0; i < state.get().oracles.capacity(); ++i)
				{
					entry = state.get().oracles.get(i);
					if (!entry.active)
					{
						entry.account = prop.targetId;
						entry.active = true;
						state.mut().oracles.set(i, entry);
						++state.mut().oracleCount;
						return true;
					}
				}
				return false;
			}
			else if (prop.role == (uint8)Role::Pauser)
			{
				if (findPauserIndex(state, prop.targetId) != NULL_INDEX)
					return true;
				for (i = 0; i < state.get().pausers.capacity(); ++i)
				{
					entry = state.get().pausers.get(i);
					if (!entry.active)
					{
						entry.account = prop.targetId;
						entry.active = true;
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
				idx = findOracleIndex(state, prop.targetId);
				if (idx == NULL_INDEX)
					return true;
				entry = state.get().oracles.get((uint32)idx);
				entry.active = false;
				state.mut().oracles.set((uint32)idx, entry);
				if (state.get().oracleCount > 0)
					--state.mut().oracleCount;
				return true;
			}
			else if (prop.role == (uint8)Role::Pauser)
			{
				idx = findPauserIndex(state, prop.targetId);
				if (idx == NULL_INDEX)
					return true;
				entry = state.get().pausers.get((uint32)idx);
				entry.active = false;
				state.mut().pausers.set((uint32)idx, entry);
				if (state.get().pauserCount > 0)
					--state.mut().pauserCount;
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

	inline static sint64 findOracleIndex(const QPI::ContractState<StateData, CONTRACT_INDEX> &state, const id &account)
	{
		for (uint32 i = 0; i < state.get().oracles.capacity(); ++i)
		{
			if (state.get().oracles.get(i).active && state.get().oracles.get(i).account == account)
				return (sint32)i;
		}
		return NULL_INDEX;
	}

	inline static sint64 findPauserIndex(const QPI::ContractState<StateData, CONTRACT_INDEX> &state, const id &account)
	{
		for (uint32 i = 0; i < state.get().pausers.capacity(); ++i)
		{
			if (state.get().pausers.get(i).active && state.get().pausers.get(i).account == account)
				return (sint32)i;
		}
		return NULL_INDEX;
	}

	// Idempotent insert into ring-buffer filled-order storage.
	inline static void markOrderFilled(QPI::ContractState<StateData, CONTRACT_INDEX> &state, const OrderHash &hash)
	{
		uint32 i, j;
		bool same;
		FilledOrderEntry entry;

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

	// Checks current and previous era buffers to cover in-flight orders across a ring wrap.
	inline static bit isOrderFilled(const QPI::ContractState<StateData, CONTRACT_INDEX> &state, const OrderHash &hash)
	{
		uint32 i, j;
		bool same;
		FilledOrderEntry entry;

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

	inline static sint64 findLockedOrderIndexByNonce(const QPI::ContractState<StateData, CONTRACT_INDEX> &state, uint32 nonce)
	{
		for (uint32 i = 0; i < QSB_MAX_LOCKED_ORDERS; ++i)
		{
			if (state.get().lockedOrders.get(i).active && state.get().lockedOrders.get(i).nonce == nonce)
				return (sint32)i;
		}
		return NULL_INDEX;
	}

	// Sweep expired proposals — called from END_EPOCH.
	inline static void sweepExpiredProposals(
		const QPI::QpiContextProcedureCall &qpi,
		QPI::ContractState<StateData, CONTRACT_INDEX> &state)
	{
		AdminProposal prop;
		for (uint32 i = 0; i < QSB_MAX_PROPOSALS; ++i)
		{
			prop = state.get().proposals.get(i);
			if (prop.active && qpi.epoch() > prop.createdEpoch + QSB_PROPOSAL_EXPIRY_EPOCHS)
			{
				prop.active = 0;
				state.mut().proposals.set(i, prop);
			}
		}
	}

	// ---------------------------------------------------------------------
	// Procedure result structs
	// ---------------------------------------------------------------------

	struct LockResult
	{
		bit success;
		uint8 reasonCode;
		OrderHash orderHash;
		uint32 orderEra;
	};

	struct OverrideLockResult
	{
		bit success;
		uint8 reasonCode;
		OrderHash orderHash;
		uint64 amount;
		uint64 relayerFee;
		uint32 networkOut;
		uint32 orderEra;
	};

	struct UnlockResult
	{
		bit success;
		uint8 reasonCode;
		OrderHash orderHash;
	};

	struct ProposeResult
	{
		bit success;
		uint8 reasonCode;
		uint8 proposalId;
	};

	struct ApproveProposalResult
	{
		bit success;
		bit executed;
		uint8 reasonCode;
		uint8 proposalType;
		id proposer;
		uint8 approvalCount;
	};

	struct CancelProposalResult
	{
		bit success;
		uint8 reasonCode;
		uint8 proposalType;
		id proposer;
		uint8 approvalCount;
	};

	struct PauseResult
	{
		bit success;
		uint8 reasonCode;
	};

	// ---------------------------------------------------------------------
	// Procedure logic helpers (try*)
	// All logic lives here; LOG_INFO stays in the thin procedure shell.
	// ---------------------------------------------------------------------

	inline static LockResult tryLock(
		const QPI::QpiContextProcedureCall &qpi,
		QPI::ContractState<StateData, CONTRACT_INDEX> &state,
		const Lock_input &input)
	{
		LockResult result = { false, QSBReasonNone, {}, 0 };

		if (state.get().paused)
		{
			if (qpi.invocationReward() > 0)
				qpi.transfer(qpi.invocator(), qpi.invocationReward());
			result.reasonCode = QSBReasonPaused;
			return result;
		}

		if (input.amount == 0 || input.relayerFee >= input.amount)
		{
			if (qpi.invocationReward() > 0)
				qpi.transfer(qpi.invocator(), qpi.invocationReward());
			result.reasonCode = QSBReasonInvalidAmount;
			return result;
		}

		if (qpi.invocationReward() < (sint64)input.amount)
		{
			if (qpi.invocationReward() > 0)
				qpi.transfer(qpi.invocator(), qpi.invocationReward());
			result.reasonCode = QSBReasonInsufficientReward;
			return result;
		}

		// Any excess over `amount` is refunded; exactly `amount` stays locked.
		if (qpi.invocationReward() > (sint64)input.amount)
			qpi.transfer(qpi.invocator(), qpi.invocationReward() - input.amount);

		if (findLockedOrderIndexByNonce(state, input.nonce) != NULL_INDEX)
		{
			qpi.transfer(qpi.invocator(), input.amount);
			result.reasonCode = QSBReasonNonceUsed;
			return result;
		}

		Order tmpOrder;
		tmpOrder.networkIn = 1;
		tmpOrder.networkOut = input.networkOut;
		setMemory(tmpOrder.tokenIn, 0);
		setMemory(tmpOrder.tokenOut, 0);
		tmpOrder.fromAddress = qpi.invocator();
		tmpOrder.toAddress = NULL_ID;
		tmpOrder.amount = input.amount;
		tmpOrder.relayerFee = input.relayerFee;
		setMemory(tmpOrder.nonce, 0);
		tmpOrder.nonce.set(0, (uint8)(input.nonce & 0xFF));
		tmpOrder.nonce.set(1, (uint8)((input.nonce >> 8) & 0xFF));
		tmpOrder.nonce.set(2, (uint8)((input.nonce >> 16) & 0xFF));
		tmpOrder.nonce.set(3, (uint8)((input.nonce >> 24) & 0xFF));
		tmpOrder.orderEra = state.get().orderEra;

		QSBOrderMessage msgBuffer;
		OrderHash tmpIdBytes;
		buildOrderMessage(msgBuffer, tmpOrder, tmpIdBytes);
		id digest = qpi.K12(msgBuffer);
		digestToOrderHash(digest, result.orderHash);
		result.orderEra = state.get().orderEra;

		// Persist in ring buffer; oldest slot overwritten when full.
		// By the time the ring wraps (1024 orders), off-chain tooling has indexed earlier entries.
		LockedOrderEntry entry;
		entry.active = true;
		entry.sender = qpi.invocator();
		entry.networkOut = input.networkOut;
		entry.amount = input.amount;
		entry.relayerFee = input.relayerFee;
		entry.nonce = input.nonce;
		copyMemory(entry.toAddress, input.toAddress);
		entry.orderHash = result.orderHash;
		entry.lockEpoch = qpi.epoch();
		entry.orderEra = state.get().orderEra;
		entry.overrideLockCount = 0;
		state.mut().lockedOrders.set(state.get().lastLockedOrdersNextOverwriteIdx, entry);
		state.mut().lastLockedOrdersNextOverwriteIdx =
			(state.get().lastLockedOrdersNextOverwriteIdx + 1) & (QSB_MAX_LOCKED_ORDERS - 1);

		result.success = true;
		return result;
	}

	inline static OverrideLockResult tryOverrideLock(
		const QPI::QpiContextProcedureCall &qpi,
		QPI::ContractState<StateData, CONTRACT_INDEX> &state,
		const OverrideLock_input &input)
	{
		OverrideLockResult result = { false, QSBReasonNone, {}, 0, 0, 0, 0 };

		// Always refund — locking was done in the original lock() call.
		if (qpi.invocationReward() > 0)
			qpi.transfer(qpi.invocator(), qpi.invocationReward());

		if (state.get().paused)
		{
			result.reasonCode = QSBReasonPaused;
			return result;
		}

		sint64 idx = findLockedOrderIndexByNonce(state, input.nonce);
		if (idx == NULL_INDEX)
		{
			result.reasonCode = QSBReasonOrderNotFound;
			return result;
		}

		LockedOrderEntry entry = state.get().lockedOrders.get((uint32)idx);

		if (entry.sender != qpi.invocator())
		{
			result.reasonCode = QSBReasonNotSender;
			return result;
		}

		if (entry.overrideLockCount >= QSB_OVERRIDE_LOCK_MAX_ATTEMPTS)
		{
			result.reasonCode = QSBReasonOverrideLimitReached;
			return result;
		}

		if (input.relayerFee >= entry.amount)
		{
			result.reasonCode = QSBReasonBadRelayerFee;
			return result;
		}

		copyMemory(entry.toAddress, input.toAddress);
		entry.relayerFee = input.relayerFee;

		Order tmpOrder;
		tmpOrder.networkIn = 1;
		tmpOrder.networkOut = entry.networkOut;
		setMemory(tmpOrder.tokenIn, 0);
		setMemory(tmpOrder.tokenOut, 0);
		tmpOrder.fromAddress = entry.sender;
		tmpOrder.toAddress = NULL_ID;
		tmpOrder.amount = entry.amount;
		tmpOrder.relayerFee = entry.relayerFee;
		setMemory(tmpOrder.nonce, 0);
		tmpOrder.nonce.set(0, (uint8)(entry.nonce & 0xFF));
		tmpOrder.nonce.set(1, (uint8)((entry.nonce >> 8) & 0xFF));
		tmpOrder.nonce.set(2, (uint8)((entry.nonce >> 16) & 0xFF));
		tmpOrder.nonce.set(3, (uint8)((entry.nonce >> 24) & 0xFF));
		tmpOrder.orderEra = entry.orderEra; // preserve original era

		QSBOrderMessage msgBuffer;
		OrderHash tmpIdBytes;
		buildOrderMessage(msgBuffer, tmpOrder, tmpIdBytes);
		id digest = qpi.K12(msgBuffer);
		digestToOrderHash(digest, entry.orderHash);

		entry.overrideLockCount++;
		state.mut().lockedOrders.set((uint32)idx, entry);

		result.orderHash = entry.orderHash;
		result.amount = entry.amount;
		result.relayerFee = entry.relayerFee;
		result.networkOut = entry.networkOut;
		result.orderEra = entry.orderEra;
		result.success = true;
		return result;
	}

	inline static UnlockResult tryUnlock(
		const QPI::QpiContextProcedureCall &qpi,
		QPI::ContractState<StateData, CONTRACT_INDEX> &state,
		const Unlock_input &input)
	{
		UnlockResult result = { false, QSBReasonNone, {} };

		if (state.get().paused)
		{
			if (qpi.invocationReward() > 0)
				qpi.transfer(qpi.invocator(), qpi.invocationReward());
			result.reasonCode = QSBReasonPaused;
			return result;
		}

		// Refund invocation reward — relayer is paid from order.amount, not from reward.
		if (qpi.invocationReward() > 0)
			qpi.transfer(qpi.invocator(), qpi.invocationReward());

		if (input.order.amount == 0 || input.order.relayerFee >= input.order.amount)
		{
			result.reasonCode = QSBReasonInvalidAmount;
			return result;
		}

		// Defensive balance check — should never fail under normal operation since
		// Lock keeps funds inside the contract, but guards against unexpected discrepancies.
		Entity entity;
		qpi.getEntity(SELF, entity);
		uint64 contractBalance = (entity.incomingAmount >= entity.outgoingAmount)
			? entity.incomingAmount - entity.outgoingAmount
			: 0;

		if (contractBalance < input.order.amount)
		{
			result.reasonCode = QSBReasonInsufficientReward;
			return result;
		}

		// Accept current era or immediately previous era.
		// The N-1 grace window covers in-flight orders signed just before a ring-buffer wrap;
		// isOrderFilled checks both buffers to prevent replays across the boundary.
		if (input.order.orderEra != state.get().orderEra &&
			!(state.get().orderEra > 0 && input.order.orderEra == state.get().orderEra - 1))
		{
			result.reasonCode = QSBReasonEraMismatch;
			return result;
		}

		// We intentionally do not require a matching lock() entry here.
		// Unlock is driven solely by oracle signatures over the burn/unlock order
		// on the other chain, replay protection via filledOrders, and a balance check.
		// This models a fungible lock/mint ↔ burn/unlock bridge where minted tokens
		// can be freely transferred and aggregated.

		QSBOrderMessage msgBuffer;
		OrderHash tmpIdBytes;
		buildOrderMessage(msgBuffer, input.order, tmpIdBytes);
		id digest = qpi.K12(msgBuffer);
		digestToOrderHash(digest, result.orderHash);

		FilledOrderEntry entry;
		if (isOrderFilled(state, result.orderHash))
		{
			result.reasonCode = QSBReasonAlreadyFilled;
			return result;
		}

		if (state.get().oracleCount == 0 || input.numSignatures == 0)
		{
			result.reasonCode = QSBReasonNoOracles;
			return result;
		}

		// requiredSignatures = ceil(oracleCount * oracleThreshold / 100)
		uint128 tmpMul = uint128(state.get().oracleCount) * uint128(state.get().oracleThreshold);
		uint128 tmpMul2 = div(tmpMul, uint128(100));
		uint32 requiredSignatures = (uint32)tmpMul2.low;
		if (requiredSignatures * 100 < state.get().oracleCount * state.get().oracleThreshold)
			++requiredSignatures;
		if (requiredSignatures == 0)
			requiredSignatures = 1;

		uint32 validSignatureCount = 0;
		uint32 seenCount = 0;
		Array<id, QSB_MAX_ORACLES> seenSigners;

		for (uint32 i = 0; i < input.numSignatures && i < input.signatures.capacity(); ++i)
		{
			SignatureData sig = input.signatures.get(i);

			if (findOracleIndex(state, sig.signer) == NULL_INDEX)
			{
				result.reasonCode = QSBReasonInvalidSignature;
				return result;
			}

			for (uint32 j = 0; j < seenCount; ++j)
			{
				if (seenSigners.get(j) == sig.signer)
				{
					result.reasonCode = QSBReasonDuplicateSigner;
					return result;
				}
			}

			if (!qpi.signatureValidity(sig.signer, digest, sig.signature))
			{
				result.reasonCode = QSBReasonInvalidSignature;
				return result;
			}

			if (seenCount < seenSigners.capacity())
			{
				seenSigners.set(seenCount, sig.signer);
				++seenCount;
			}
			++validSignatureCount;
		}

		if (validSignatureCount < requiredSignatures)
		{
			result.reasonCode = QSBReasonThresholdFailed;
			return result;
		}

		// bpsFeeAmount = netAmount * bpsFee / 10000
		uint64 netAmount = input.order.amount - input.order.relayerFee;
		tmpMul = uint128(netAmount) * uint128(state.get().bpsFee);
		tmpMul2 = div(tmpMul, uint128(10000));
		uint64 bpsFeeAmount = (uint64)tmpMul2.low;

		// protocolFeeAmount = bpsFeeAmount * protocolFee / 100
		tmpMul = uint128(bpsFeeAmount) * uint128(state.get().protocolFee);
		tmpMul2 = div(tmpMul, uint128(100));
		uint64 protocolFeeAmount = (uint64)tmpMul2.low;

		// oracleFeeAmount = bpsFeeAmount - protocolFeeAmount
		uint64 oracleFeeAmount = (bpsFeeAmount >= protocolFeeAmount) ? bpsFeeAmount - protocolFeeAmount : 0;

		// recipientAmount = netAmount - bpsFeeAmount
		uint64 recipientAmount = (netAmount >= bpsFeeAmount) ? netAmount - bpsFeeAmount : 0;

		// Mark filled BEFORE transfers to prevent replay on partial transfer failure.
		// The balance check above guarantees the contract has enough funds.
		markOrderFilled(state, result.orderHash);

		bool allTransfersOk = true;

		// Recipient payout first (most important transfer)
		if (recipientAmount > 0 && !isZero(input.order.toAddress))
		{
			if (qpi.transfer(input.order.toAddress, (sint64)recipientAmount) < 0)
				allTransfersOk = false;
		}

		if (input.order.relayerFee > 0)
		{
			if (qpi.transfer(qpi.invocator(), (sint64)input.order.relayerFee) < 0)
				allTransfersOk = false;
		}

		if (protocolFeeAmount > 0 && !isZero(state.get().protocolFeeRecipient))
		{
			if (qpi.transfer(state.get().protocolFeeRecipient, (sint64)protocolFeeAmount) < 0)
				allTransfersOk = false;
		}

		if (oracleFeeAmount > 0 && !isZero(state.get().oracleFeeRecipient))
		{
			if (qpi.transfer(state.get().oracleFeeRecipient, (sint64)oracleFeeAmount) < 0)
				allTransfersOk = false;
		}

		if (!allTransfersOk)
		{
			result.reasonCode = QSBReasonTransferFailed;
			return result;
		}

		result.success = true;
		return result;
	}

	inline static ProposeResult tryPropose(
		const QPI::QpiContextProcedureCall &qpi,
		QPI::ContractState<StateData, CONTRACT_INDEX> &state,
		const Propose_input &input)
	{
		ProposeResult result = { false, QSBReasonNone, 0 };

		if (qpi.invocationReward() > 0)
			qpi.transfer(qpi.invocator(), qpi.invocationReward());

		sint64 adminIdx = findAdminIndex(state, qpi.invocator());
		if (adminIdx == NULL_INDEX)
		{
			result.reasonCode = QSBReasonNotAdmin;
			return result;
		}

		if (input.proposalType == 0 || input.proposalType > QSBPropUnpause)
		{
			result.reasonCode = QSBReasonInvalidRole;
			return result;
		}

		if (input.proposalType == QSBPropAddAdmin)
		{
			if (isZero(input.targetId))
			{
				result.reasonCode = QSBReasonInvalidAdmin;
				return result;
			}
			if (findAdminIndex(state, input.targetId) != NULL_INDEX)
			{
				result.reasonCode = QSBReasonAlreadyAdmin;
				return result;
			}
			if (state.get().adminCount >= QSB_MAX_ADMINS)
			{
				result.reasonCode = QSBReasonAdminFull;
				return result;
			}
		}
		else if (input.proposalType == QSBPropRemoveAdmin)
		{
			if (isZero(input.targetId))
			{
				result.reasonCode = QSBReasonInvalidAdmin;
				return result;
			}
			if (findAdminIndex(state, input.targetId) == NULL_INDEX)
			{
				result.reasonCode = QSBReasonRoleMissing;
				return result;
			}
			if (state.get().adminCount <= 1)
			{
				result.reasonCode = QSBReasonWouldLockContract;
				return result;
			}
			if ((state.get().adminCount - 1) < state.get().adminThreshold)
			{
				result.reasonCode = QSBReasonWouldLockContract;
				return result;
			}
		}
		else if (input.proposalType == QSBPropSetAdminThreshold)
		{
			if (input.newAdminThreshold == 0 || input.newAdminThreshold > state.get().adminCount)
			{
				result.reasonCode = QSBReasonInvalidThreshold;
				return result;
			}
		}
		else if (input.proposalType == QSBPropAddRole || input.proposalType == QSBPropRemoveRole)
		{
			if (isZero(input.targetId))
			{
				result.reasonCode = QSBReasonInvalidAdmin;
				return result;
			}
			if (input.role != (uint8)Role::Oracle && input.role != (uint8)Role::Pauser)
			{
				result.reasonCode = QSBReasonInvalidRole;
				return result;
			}
		}
		else if (input.proposalType == QSBPropEditOracleThreshold)
		{
			if (input.newOracleThreshold == 0 || input.newOracleThreshold > 100)
			{
				result.reasonCode = QSBReasonInvalidThreshold;
				return result;
			}
		}
		else if (input.proposalType == QSBPropEditFeeParameters)
		{
			if (input.bpsFee > QSB_MAX_BPS_FEE || input.protocolFee > QSB_MAX_PROTOCOL_FEE)
			{
				result.reasonCode = QSBReasonInvalidFeeParams;
				return result;
			}
		}
		else if (input.proposalType != QSBPropUnpause)
		{
			result.reasonCode = QSBReasonInvalidProposalType;
			return result;
		}

		// Enforce per-admin concurrent proposal cap
		uint8 adminProposalCount = 0;
		AdminProposal prop;
		for (uint32 i = 0; i < QSB_MAX_PROPOSALS; ++i)
		{
			prop = state.get().proposals.get(i);
			if (prop.active && prop.proposer == qpi.invocator())
				++adminProposalCount;
		}
		if (adminProposalCount >= QSB_MAX_PROPOSALS_PER_ADMIN)
		{
			result.reasonCode = QSBReasonTooManyProposals;
			return result;
		}

		uint8 slotIdx = (uint8)QSB_MAX_PROPOSALS;
		for (uint32 i = 0; i < QSB_MAX_PROPOSALS; ++i)
		{
			if (!state.get().proposals.get(i).active)
			{
				slotIdx = (uint8)i;
				break;
			}
		}
		if (slotIdx >= QSB_MAX_PROPOSALS)
		{
			result.reasonCode = QSBReasonProposalFull;
			return result;
		}

		// Build proposal; proposer auto-approves (bit set at their admin index).
		setMemory(prop, 0);
		prop.proposalType = input.proposalType;
		prop.active = 1;
		prop.executed = 0;
		prop.proposer = qpi.invocator();
		prop.createdEpoch = qpi.epoch();
		prop.approvedMask = (uint8)(1u << (uint8)adminIdx);
		prop.approvalCount = 1;
		prop.targetId = input.targetId;
		prop.role = input.role;
		prop.newAdminThreshold = input.newAdminThreshold;
		prop.newOracleThreshold = input.newOracleThreshold;
		prop.protocolFeeRecipient = input.protocolFeeRecipient;
		prop.oracleFeeRecipient = input.oracleFeeRecipient;
		prop.bpsFee = input.bpsFee;
		prop.protocolFee = input.protocolFee;
		state.mut().proposals.set(slotIdx, prop);
		result.proposalId = slotIdx;
		result.success = true;

		// Execute immediately when threshold == 1 (single-admin or bootstrap mode).
		if (state.get().adminThreshold <= 1)
		{
			bool execOk = executeProposalPayload(state, prop);
			prop = state.get().proposals.get(slotIdx);
			prop.active = 0;
			prop.executed = execOk ? 1 : 0;
			state.mut().proposals.set(slotIdx, prop);
			if (execOk &&
				(input.proposalType == QSBPropAddAdmin ||
				 input.proposalType == QSBPropRemoveAdmin ||
				 input.proposalType == QSBPropSetAdminThreshold))
			{
				cancelAllPendingProposals(state);
			}
			result.success = execOk;
		}

		return result;
	}

	inline static ApproveProposalResult tryApproveProposal(
		const QPI::QpiContextProcedureCall &qpi,
		QPI::ContractState<StateData, CONTRACT_INDEX> &state,
		const ApproveProposal_input &input)
	{
		ApproveProposalResult result = { false, false, QSBReasonNone, 0, NULL_ID, 0 };

		if (qpi.invocationReward() > 0)
			qpi.transfer(qpi.invocator(), qpi.invocationReward());

		sint64 adminIdx = findAdminIndex(state, qpi.invocator());
		if (adminIdx == NULL_INDEX)
		{
			result.reasonCode = QSBReasonNotAdmin;
			return result;
		}

		if (input.proposalId >= QSB_MAX_PROPOSALS)
		{
			result.reasonCode = QSBReasonProposalNotFound;
			return result;
		}

		AdminProposal prop = state.get().proposals.get(input.proposalId);
		result.proposalType = prop.proposalType;
		result.proposer = prop.proposer;
		result.approvalCount = prop.approvalCount;

		if (!prop.active)
		{
			result.reasonCode = QSBReasonProposalNotFound;
			return result;
		}

		if (qpi.epoch() > prop.createdEpoch + QSB_PROPOSAL_EXPIRY_EPOCHS)
		{
			prop.active = 0;
			state.mut().proposals.set(input.proposalId, prop);
			result.reasonCode = QSBReasonProposalExpired;
			return result;
		}

		uint8 bitPos = (uint8)adminIdx;
		if (bitPos < 8 && (prop.approvedMask & (uint8)(1u << bitPos)))
		{
			result.reasonCode = QSBReasonAlreadyApproved;
			return result;
		}

		prop.approvedMask |= (uint8)(1u << bitPos);
		prop.approvalCount = countBitsUint8(prop.approvedMask);
		state.mut().proposals.set(input.proposalId, prop);
		result.success = true;
		result.approvalCount = prop.approvalCount;

		if (prop.approvalCount >= state.get().adminThreshold)
		{
			uint8 propType = prop.proposalType;
			bool execOk = executeProposalPayload(state, prop);
			prop = state.get().proposals.get(input.proposalId);
			prop.active = 0;
			prop.executed = execOk ? 1 : 0;
			state.mut().proposals.set(input.proposalId, prop);
			result.executed = true;
			if (execOk &&
				(propType == QSBPropAddAdmin ||
				 propType == QSBPropRemoveAdmin ||
				 propType == QSBPropSetAdminThreshold))
			{
				cancelAllPendingProposals(state);
			}
		}

		return result;
	}

	inline static CancelProposalResult tryCancelProposal(
		const QPI::QpiContextProcedureCall &qpi,
		QPI::ContractState<StateData, CONTRACT_INDEX> &state,
		const CancelProposal_input &input)
	{
		CancelProposalResult result = { false, QSBReasonNone, 0, NULL_ID, 0 };

		if (qpi.invocationReward() > 0)
			qpi.transfer(qpi.invocator(), qpi.invocationReward());

		if (!isAdmin(state, qpi.invocator()))
		{
			result.reasonCode = QSBReasonNotAdmin;
			return result;
		}

		if (input.proposalId >= QSB_MAX_PROPOSALS)
		{
			result.reasonCode = QSBReasonProposalNotFound;
			return result;
		}

		AdminProposal prop = state.get().proposals.get(input.proposalId);
		result.proposalType = prop.proposalType;
		result.proposer = prop.proposer;
		result.approvalCount = prop.approvalCount;

		if (!prop.active)
		{
			result.reasonCode = QSBReasonProposalNotFound;
			return result;
		}

		if (prop.proposer != qpi.invocator())
		{
			result.reasonCode = QSBReasonNotProposer;
			return result;
		}

		prop.active = 0;
		state.mut().proposals.set(input.proposalId, prop);
		result.success = true;
		return result;
	}

	inline static PauseResult tryPause(
		const QPI::QpiContextProcedureCall &qpi,
		QPI::ContractState<StateData, CONTRACT_INDEX> &state)
	{
		PauseResult result = { false, QSBReasonNone };

		if (qpi.invocationReward() > 0)
			qpi.transfer(qpi.invocator(), qpi.invocationReward());

		if (!isAdminOrPauser(state, qpi.invocator()))
		{
			result.reasonCode = QSBReasonNotAdminOrPauser;
			return result;
		}

		state.mut().paused = true;
		result.success = true;
		return result;
	}

public:
	// ---------------------------------------------------------------------
	// Core user procedures
	// ---------------------------------------------------------------------

	struct Lock_locals
	{
		LockResult result;
		QSBLogLockMessage logMsg;
	};

	PUBLIC_PROCEDURE_WITH_LOCALS(Lock)
	{
		locals.result = tryLock(qpi, state, input);
		output.success = locals.result.success;
		output.orderHash = locals.result.orderHash;

		locals.logMsg._contractIndex = SELF_INDEX;
		locals.logMsg._type = QSBLogLock;
		locals.logMsg.from = qpi.invocator();
		copyFromBuffer(locals.logMsg.to, input.toAddress);
		locals.logMsg.amount = input.amount;
		locals.logMsg.relayerFee = input.relayerFee;
		locals.logMsg.networkOut = input.networkOut;
		locals.logMsg.nonce = input.nonce;
		locals.logMsg.orderHash = locals.result.orderHash;
		locals.logMsg.success = locals.result.success ? 1 : 0;
		locals.logMsg.reasonCode = locals.result.reasonCode;
		locals.logMsg.orderEra = locals.result.orderEra;
		locals.logMsg._terminator = 0;
		LOG_INFO(locals.logMsg);
	}

	struct OverrideLock_locals
	{
		OverrideLockResult result;
		QSBLogOverrideLockMessage logMsg;
	};

	PUBLIC_PROCEDURE_WITH_LOCALS(OverrideLock)
	{
		locals.result = tryOverrideLock(qpi, state, input);
		output.success = locals.result.success;
		output.orderHash = locals.result.orderHash;

		locals.logMsg._contractIndex = SELF_INDEX;
		locals.logMsg._type = QSBLogOverrideLock;
		locals.logMsg.from = qpi.invocator();
		setMemory(locals.logMsg.to, 0);
		if (locals.result.success)
			copyFromBuffer(locals.logMsg.to, input.toAddress);
		locals.logMsg.amount = locals.result.amount;
		locals.logMsg.relayerFee = locals.result.relayerFee;
		locals.logMsg.networkOut = locals.result.networkOut;
		locals.logMsg.nonce = input.nonce;
		locals.logMsg.orderHash = locals.result.orderHash;
		locals.logMsg.success = locals.result.success ? 1 : 0;
		locals.logMsg.reasonCode = locals.result.reasonCode;
		locals.logMsg.orderEra = locals.result.orderEra;
		locals.logMsg._terminator = 0;
		LOG_INFO(locals.logMsg);
	}

	struct Unlock_locals
	{
		UnlockResult result;
		QSBLogUnlockMessage logMsg;
	};

	PUBLIC_PROCEDURE_WITH_LOCALS(Unlock)
	{
		locals.result = tryUnlock(qpi, state, input);
		output.success = locals.result.success;
		output.orderHash = locals.result.orderHash;

		locals.logMsg._contractIndex = SELF_INDEX;
		locals.logMsg._type = QSBLogUnlock;
		locals.logMsg.orderHash = locals.result.orderHash;
		locals.logMsg.toAddress = input.order.toAddress;
		locals.logMsg.amount = input.order.amount;
		locals.logMsg.relayerFee = input.order.relayerFee;
		locals.logMsg.relayer = qpi.invocator();
		locals.logMsg.success = locals.result.success ? 1 : 0;
		locals.logMsg.reasonCode = locals.result.reasonCode;
		locals.logMsg.orderEra = input.order.orderEra;
		locals.logMsg._terminator = 0;
		LOG_INFO(locals.logMsg);
	}

	// View functions
	PUBLIC_FUNCTION(GetConfig)
	{
		output.adminCount = state.get().adminCount;
		output.adminThreshold = state.get().adminThreshold;
		output.admins = state.get().admins;
		output.protocolFeeRecipient = state.get().protocolFeeRecipient;
		output.oracleFeeRecipient = state.get().oracleFeeRecipient;
		output.bpsFee = state.get().bpsFee;
		output.protocolFee = state.get().protocolFee;
		output.oracleCount = state.get().oracleCount;
		output.pauserCount = state.get().pauserCount;
		output.oracleThreshold = state.get().oracleThreshold;
		output.paused = state.get().paused;
		output.orderEra = state.get().orderEra;
	}

	PUBLIC_FUNCTION(GetProposal)
	{
		output.exists = false;
		if (input.proposalId < QSB_MAX_PROPOSALS)
		{
			output.proposal = state.get().proposals.get(input.proposalId);
			output.exists = output.proposal.active;
		}
	}

	struct GetProposals_locals
	{
		uint32 i;
		AdminProposal prop;
	};
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
		output.isOracle = (findOracleIndex(state, input.account) != NULL_INDEX);
	}

	PUBLIC_FUNCTION(IsPauser)
	{
		output.isPauser = (findPauserIndex(state, input.account) != NULL_INDEX);
	}

	struct GetLockedOrder_locals
	{
		sint64 idx;
	};

	PUBLIC_FUNCTION_WITH_LOCALS(GetLockedOrder)
	{
		locals.idx = findLockedOrderIndexByNonce(state, input.nonce);
		output.exists = (locals.idx != NULL_INDEX);
		if (output.exists)
			output.order = state.get().lockedOrders.get((uint32)locals.idx);
	}

	PUBLIC_FUNCTION(IsOrderFilled)
	{
		output.filled = isOrderFilled(state, input.hash);
	}

	struct ComputeOrderHash_locals
	{
		id digest;
		QSBOrderMessage msgBuffer;
		OrderHash tmpIdBytes;
	};

	PUBLIC_FUNCTION_WITH_LOCALS(ComputeOrderHash)
	{
		buildOrderMessage(locals.msgBuffer, input.order, locals.tmpIdBytes);
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

	// ---------------------------------------------------------------------
	// Admin procedures (multisig)
	// ---------------------------------------------------------------------

	struct Propose_locals
	{
		ProposeResult result;
		QSBLogProposalMessage logMsg;
	};

	PUBLIC_PROCEDURE_WITH_LOCALS(Propose)
	{
		locals.result = tryPropose(qpi, state, input);
		output.success = locals.result.success;
		output.reasonCode = locals.result.reasonCode;
		output.proposalId = locals.result.proposalId;

		locals.logMsg._contractIndex = SELF_INDEX;
		locals.logMsg._type = QSBLogProposalCreated;
		locals.logMsg.proposalId = output.proposalId;
		locals.logMsg.proposalType = input.proposalType;
		locals.logMsg.proposer = qpi.invocator();
		locals.logMsg.actor = qpi.invocator();
		locals.logMsg.approvalCount = 1;
		locals.logMsg.success = output.success ? 1 : 0;
		locals.logMsg.reasonCode = output.reasonCode;
		locals.logMsg._terminator = 0;
		LOG_INFO(locals.logMsg);
	}

	struct ApproveProposal_locals
	{
		ApproveProposalResult result;
		QSBLogProposalMessage logMsg;
	};

	PUBLIC_PROCEDURE_WITH_LOCALS(ApproveProposal)
	{
		locals.result = tryApproveProposal(qpi, state, input);
		output.success = locals.result.success;
		output.executed = locals.result.executed;
		output.reasonCode = locals.result.reasonCode;

		locals.logMsg._contractIndex = SELF_INDEX;
		locals.logMsg._type = locals.result.executed ? QSBLogProposalExecuted : QSBLogProposalApproved;
		locals.logMsg.proposalId = input.proposalId;
		locals.logMsg.proposalType = locals.result.proposalType;
		locals.logMsg.proposer = locals.result.proposer;
		locals.logMsg.actor = qpi.invocator();
		locals.logMsg.approvalCount = locals.result.approvalCount;
		locals.logMsg.success = output.success ? 1 : 0;
		locals.logMsg.reasonCode = output.reasonCode;
		locals.logMsg._terminator = 0;
		LOG_INFO(locals.logMsg);
	}

	struct CancelProposal_locals
	{
		CancelProposalResult result;
		QSBLogProposalMessage logMsg;
	};

	PUBLIC_PROCEDURE_WITH_LOCALS(CancelProposal)
	{
		locals.result = tryCancelProposal(qpi, state, input);
		output.success = locals.result.success;
		output.reasonCode = locals.result.reasonCode;

		locals.logMsg._contractIndex = SELF_INDEX;
		locals.logMsg._type = QSBLogProposalCancelled;
		locals.logMsg.proposalId = input.proposalId;
		locals.logMsg.proposalType = locals.result.proposalType;
		locals.logMsg.proposer = locals.result.proposer;
		locals.logMsg.actor = qpi.invocator();
		locals.logMsg.approvalCount = locals.result.approvalCount;
		locals.logMsg.success = output.success ? 1 : 0;
		locals.logMsg.reasonCode = output.reasonCode;
		locals.logMsg._terminator = 0;
		LOG_INFO(locals.logMsg);
	}

	struct Pause_locals
	{
		PauseResult result;
		QSBLogPausedMessage logMsg;
	};

	PUBLIC_PROCEDURE_WITH_LOCALS(Pause)
	{
		locals.result = tryPause(qpi, state);
		output.success = locals.result.success;

		locals.logMsg._contractIndex = SELF_INDEX;
		locals.logMsg._type = QSBLogPaused;
		locals.logMsg.caller = qpi.invocator();
		locals.logMsg.success = output.success ? 1 : 0;
		locals.logMsg.reasonCode = locals.result.reasonCode;
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

	struct END_EPOCH_locals {};

	END_EPOCH_WITH_LOCALS()
	{
		sweepExpiredProposals(qpi, state);
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
		state.mut().adminCount = 2;
		state.mut().adminThreshold = 2;
		setMemory(state.mut().proposals, 0);

		state.mut().paused = false;

		state.mut().oracleThreshold = 67;
		state.mut().lastFilledOrdersNextOverwriteIdx = 0;
		state.mut().lastLockedOrdersNextOverwriteIdx = 0;
		state.mut().oracleCount = 0;
		state.mut().pauserCount = 0;

		setMemory(state.mut().oracles, 0);
		setMemory(state.mut().pausers, 0);
		setMemory(state.mut().filledOrders, 0);
		setMemory(state.mut().filledOrdersPrev, 0);
		setMemory(state.mut().lockedOrders, 0);

		state.mut().bpsFee = 0;
		state.mut().protocolFee = 0;
		state.mut().protocolFeeRecipient = NULL_ID;
		state.mut().oracleFeeRecipient = NULL_ID;

		state.mut().orderEra = 0;
	}
};
