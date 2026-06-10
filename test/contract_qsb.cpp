#define NO_UEFI

#include "contract_testing.h"


static const id QSB_CONTRACT_ID(QSB_CONTRACT_INDEX, 0, 0, 0);
static const id USER1(123, 456, 789, 876);
static const id USER2(42, 424, 4242, 42424);
static const id ADMIN(100, 200, 300, 400);
static const id ADMIN2(101, 201, 301, 401);   // second admin, matches INITIALIZE slot 1
static const id ORACLE1(500, 600, 700, 800);
static const id ORACLE2(900, 1000, 1100, 1200);
static const id ORACLE3(1300, 1400, 1500, 1600);
static const id PAUSER1(1700, 1800, 1900, 2000);
static const id PROTOCOL_FEE_RECIPIENT(2100, 2200, 2300, 2400);
static const id ORACLE_FEE_RECIPIENT(2500, 2600, 2700, 2800);

class StateCheckerQSB : public QSB, public QSB::StateData
{
public:
    const QPI::ContractState<StateData, QSB_CONTRACT_INDEX>& asState() const {
        return *reinterpret_cast<const QPI::ContractState<StateData, QSB_CONTRACT_INDEX>*>(static_cast<const StateData*>(this));
    }
    QPI::ContractState<StateData, QSB_CONTRACT_INDEX>& asMutState() {
        return *reinterpret_cast<QPI::ContractState<StateData, QSB_CONTRACT_INDEX>*>(static_cast<StateData*>(this));
    }

    void checkAdminCount(uint8 expected) const
    {
        EXPECT_EQ(this->adminCount, expected);
    }

    void checkAdminThreshold(uint8 expected) const
    {
        EXPECT_EQ(this->adminThreshold, expected);
    }

    void checkIsAdmin(const id& who) const
    {
        bool found = false;
        for (uint32 i = 0; i < QSB_MAX_ADMINS; ++i)
        {
            if (this->admins.get(i) == who) { found = true; break; }
        }
        EXPECT_TRUE(found);
    }

    void checkIsNotAdmin(const id& who) const
    {
        bool found = false;
        for (uint32 i = 0; i < QSB_MAX_ADMINS; ++i)
        {
            if (this->admins.get(i) == who) { found = true; break; }
        }
        EXPECT_FALSE(found);
    }

    void checkPaused(bool expectedPaused) const
    {
        EXPECT_EQ((bool)this->paused, expectedPaused);
    }

    void checkOracleThreshold(uint8 expectedThreshold) const
    {
        EXPECT_EQ(this->oracleThreshold, expectedThreshold);
    }

    void checkOracleCount(uint32 expectedCount) const
    {
        EXPECT_EQ(this->oracleCount, expectedCount);
    }

    void checkBpsFee(uint32 expectedFee) const
    {
        EXPECT_EQ(this->bpsFee, expectedFee);
    }

    void checkProtocolFee(uint32 expectedFee) const
    {
        EXPECT_EQ(this->protocolFee, expectedFee);
    }

    void checkProtocolFeeRecipient(const id& expectedRecipient) const
    {
        EXPECT_EQ(this->protocolFeeRecipient, expectedRecipient);
    }

    void checkOracleFeeRecipient(const id& expectedRecipient) const
    {
        EXPECT_EQ(this->oracleFeeRecipient, expectedRecipient);
    }

    // Helper to mark an order hash as filled via the internal ring buffer logic.
    void forceMarkOrderFilled(const QSB::OrderHash& hash)
    {
        markOrderFilled(asMutState(), hash);
    }

    // Directly write an active locked order entry into a slot (bypasses contract call overhead).
    void fillLockedOrderSlot(uint32 slot, uint32 nonce)
    {
        LockedOrderEntry entry = {};
        entry.active = true;
        entry.nonce = nonce;
        entry.sender = id((uint64)(slot + 10000), 0ULL, 0ULL, 0ULL);
        entry.amount = 1;
        asMutState().mut().lockedOrders.set(slot, entry);
    }

    void setLastLockedOrdersNextIdx(uint32 idx)
    {
        asMutState().mut().lastLockedOrdersNextOverwriteIdx = idx;
    }
};

class ContractTestingQSB : protected ContractTesting
{
public:
    ContractTestingQSB()
    {
        initEmptySpectrum();
        initEmptyUniverse();
        INIT_CONTRACT(QSB);
        callSystemProcedure(QSB_CONTRACT_INDEX, INITIALIZE);
        // INITIALIZE sets ADMIN (slot 0) and ADMIN2 (slot 1) with threshold=2.
        checkContractExecCleanup();
    }

    ~ContractTestingQSB()
    {
        checkContractExecCleanup();
    }

    StateCheckerQSB* getState()
    {
        return (StateCheckerQSB*)contractStates[QSB_CONTRACT_INDEX];
    }

    const StateCheckerQSB* getState() const
    {
        return (const StateCheckerQSB*)contractStates[QSB_CONTRACT_INDEX];
    }

    static QSB::Order createTestOrder(
        const id& fromAddress,
        const id& toAddress,
        uint64 amount,
        uint64 relayerFee,
        const Array<uint8, 32>& nonce32,
        uint32 orderEra = 0)
    {
        QSB::Order order;
        order.fromAddress = fromAddress;
        order.toAddress = toAddress;
        setMemory(order.tokenIn, 0);
        setMemory(order.tokenOut, 0);
        order.amount = amount;
        order.relayerFee = relayerFee;
        order.networkIn = 2;
        order.networkOut = 1;
        order.nonce = nonce32;
        order.orderEra = orderEra;
        return order;
    }

    static QSB::Order createTestOrderFromU32Nonce(
        const id& fromAddress,
        const id& toAddress,
        uint64 amount,
        uint64 relayerFee,
        uint32 nonce,
        uint32 orderEra = 0)
    {
        Array<uint8, 32> nonce32;
        setMemory(nonce32, 0);
        nonce32.set(0, (uint8)(nonce & 0xFF));
        nonce32.set(1, (uint8)((nonce >> 8) & 0xFF));
        nonce32.set(2, (uint8)((nonce >> 16) & 0xFF));
        nonce32.set(3, (uint8)((nonce >> 24) & 0xFF));
        return createTestOrder(fromAddress, toAddress, amount, relayerFee, nonce32, orderEra);
    }

    // Helper to create signature data (mock - in real tests would need actual signatures)
    QSB::SignatureData createMockSignature(const id& signer) const
    {
        QSB::SignatureData sig;
        sig.signer = signer;
        // In real implementation, this would be a valid signature
        // For testing, we'll use zeros (signature validation will fail, but structure is correct)
        setMemory(sig.signature, 0);
        return sig;
    }

    // Derive a deterministic key pair from a u64 seed.
    struct OracleKey {
        id subseed;
        id publicKey;
    };
    static OracleKey makeOracleKey(uint64 seed)
    {
        OracleKey k;
        k.subseed = id(seed, seed ^ 0xDEADBEEFULL, seed ^ 0xCAFEBABEULL, seed ^ 0xFEEDFACEULL);
        id privateKey;
        getPrivateKey(k.subseed.m256i_u8, privateKey.m256i_u8);
        getPublicKey(privateKey.m256i_u8, k.publicKey.m256i_u8);
        return k;
    }

    // Create a cryptographically valid SignatureData for the given order.
    // The signature is over the same K12(QSBOrderMessage) digest the contract verifies.
    QSB::SignatureData createOrderSignature(const OracleKey& key, const QSB::Order& order) const
    {
        // Build order message the same way the contract does
        QSBOrderMessage msg;
        QSB::OrderHash tmpHash;
        setMemory(msg, 0);
        msg.protocolNameLen = 11;
        msg.protocolName.set(0, 81);  msg.protocolName.set(1, 117); msg.protocolName.set(2, 98);
        msg.protocolName.set(3, 105); msg.protocolName.set(4, 99);  msg.protocolName.set(5, 66);
        msg.protocolName.set(6, 114); msg.protocolName.set(7, 105); msg.protocolName.set(8, 100);
        msg.protocolName.set(9, 103); msg.protocolName.set(10, 101);
        msg.protocolVersionLen = 1;
        msg.protocolVersion.set(0, 49);
        msg.contractAddress.set(0, (uint8)(QSB_CONTRACT_INDEX & 0xFF));
        msg.contractAddress.set(1, (uint8)((QSB_CONTRACT_INDEX >> 8) & 0xFF));
        msg.networkIn = order.networkIn;
        msg.networkOut = order.networkOut;
        for (uint32 i = 0; i < 32; ++i) msg.tokenIn.set(i, order.tokenIn.get(i));
        for (uint32 i = 0; i < 32; ++i) msg.tokenOut.set(i, order.tokenOut.get(i));
        tmpHash.setMem(order.fromAddress);
        for (uint32 i = 0; i < 32; ++i) msg.fromAddress.set(i, tmpHash.get(i));
        tmpHash.setMem(order.toAddress);
        for (uint32 i = 0; i < 32; ++i) msg.toAddress.set(i, tmpHash.get(i));
        msg.amount = order.amount;
        msg.relayerFee = order.relayerFee;
        for (uint32 i = 0; i < 32; ++i) msg.nonce.set(i, order.nonce.get(i));
        msg.orderEra = order.orderEra;

        m256i digest;
        KangarooTwelve(&msg, sizeof(msg), &digest, sizeof(digest));

        // getPrivateKey/sign require non-const pointers — copy to local buffers
        id subseedCopy = key.subseed;
        id pubKeyCopy = key.publicKey;
        id privateKey;
        getPrivateKey(subseedCopy.m256i_u8, privateKey.m256i_u8);

        QSB::SignatureData sig;
        sig.signer = key.publicKey;
        sign(subseedCopy.m256i_u8, pubKeyCopy.m256i_u8, digest.m256i_u8,
             reinterpret_cast<unsigned char*>(&sig.signature));
        return sig;
    }

    // Helper to create a zero-initialized address array
    static Array<uint8, 64> createZeroAddress()
    {
        Array<uint8, 64> addr;
        setMemory(addr, 0);
        return addr;
    }

    // ============================================================================
    // User Procedure Helpers
    // ============================================================================

    QSB::Lock_output lock(const id& user, uint64 amount, uint64 relayerFee, uint32 networkOut, uint32 nonce, const Array<uint8, 64>& toAddress, uint64 energyAmount)
    {
        QSB::Lock_input input;
        QSB::Lock_output output;

        input.amount = amount;
        input.relayerFee = relayerFee;
        input.networkOut = networkOut;
        input.nonce = nonce;
        copyToBuffer(input.toAddress, toAddress, true);

        invokeUserProcedure(QSB_CONTRACT_INDEX, 1, input, output, user, energyAmount);
        return output;
    }

    QSB::OverrideLock_output overrideLock(const id& user, uint32 nonce, uint64 relayerFee, const Array<uint8, 64>& toAddress)
    {
        QSB::OverrideLock_input input;
        QSB::OverrideLock_output output;

        input.nonce = nonce;
        input.relayerFee = relayerFee;
        copyToBuffer(input.toAddress, toAddress, true);

        invokeUserProcedure(QSB_CONTRACT_INDEX, 2, input, output, user, 0);
        return output;
    }

    QSB::Unlock_output unlock(const id& user, const QSB::Order& order, uint32 numSignatures, const Array<QSB::SignatureData, QSB_MAX_ORACLES>& signatures)
    {
        QSB::Unlock_input input;
        QSB::Unlock_output output;

        input.order = order;
        input.numSignatures = numSignatures;
        uint32 toCopy = numSignatures < QSB_MAX_UNLOCK_SIGNATURES ? numSignatures : QSB_MAX_UNLOCK_SIGNATURES;
        for (uint32 i = 0; i < toCopy; i++)
            input.signatures.set(i, signatures.get(i));

        invokeUserProcedure(QSB_CONTRACT_INDEX, 3, input, output, user, 0);
        return output;
    }

    // ============================================================================
    // Admin Procedure Helpers (proposal system)
    // ============================================================================

    QSB::Pause_output pause(const id& user)
    {
        QSB::Pause_input input;
        QSB::Pause_output output;
        increaseEnergy(user, 1);
        invokeUserProcedure(QSB_CONTRACT_INDEX, 14, input, output, user, 0);
        return output;
    }

    QSB::Propose_output propose(const id& user, const QSB::Propose_input& input)
    {
        QSB::Propose_output output;
        increaseEnergy(user, 1);
        invokeUserProcedure(QSB_CONTRACT_INDEX, 20, input, output, user, 0);
        return output;
    }

    QSB::ApproveProposal_output approveProposal(const id& user, uint8 proposalId)
    {
        QSB::ApproveProposal_input input;
        QSB::ApproveProposal_output output;
        input.proposalId = proposalId;
        increaseEnergy(user, 1);
        invokeUserProcedure(QSB_CONTRACT_INDEX, 21, input, output, user, 0);
        return output;
    }

    QSB::CancelProposal_output cancelProposal(const id& user, uint8 proposalId)
    {
        QSB::CancelProposal_input input;
        QSB::CancelProposal_output output;
        input.proposalId = proposalId;
        increaseEnergy(user, 1);
        invokeUserProcedure(QSB_CONTRACT_INDEX, 22, input, output, user, 0);
        return output;
    }

    // Propose-then-approve convenience wrapper for 2-of-2 operations.
    // Returns the approval output; expects both steps to succeed.
    QSB::ApproveProposal_output proposeAndApprove(const id& proposer, const id& approver, const QSB::Propose_input& input)
    {
        QSB::Propose_output propOut = propose(proposer, input);
        EXPECT_TRUE((bool)propOut.success);
        QSB::ApproveProposal_output approveOut = approveProposal(approver, propOut.proposalId);
        return approveOut;
    }

    // ============================================================================
    // Named proposal helpers
    // ============================================================================

    QSB::ApproveProposal_output proposeAddRole(const id& proposer, const id& approver, uint8 role, const id& account)
    {
        QSB::Propose_input input;
        setMemory(input, 0);
        input.proposalType = QSBPropAddRole;
        input.role = role;
        input.targetId = account;
        return proposeAndApprove(proposer, approver, input);
    }

    QSB::ApproveProposal_output proposeRemoveRole(const id& proposer, const id& approver, uint8 role, const id& account)
    {
        QSB::Propose_input input;
        setMemory(input, 0);
        input.proposalType = QSBPropRemoveRole;
        input.role = role;
        input.targetId = account;
        return proposeAndApprove(proposer, approver, input);
    }

    QSB::ApproveProposal_output proposeEditOracleThreshold(const id& proposer, const id& approver, uint8 newThreshold)
    {
        QSB::Propose_input input;
        setMemory(input, 0);
        input.proposalType = QSBPropEditOracleThreshold;
        input.newOracleThreshold = newThreshold;
        return proposeAndApprove(proposer, approver, input);
    }

    QSB::ApproveProposal_output proposeEditFeeParameters(
        const id& proposer, const id& approver,
        uint32 bpsFee, uint32 protocolFee,
        const id& protocolFeeRecipient, const id& oracleFeeRecipient)
    {
        QSB::Propose_input input;
        setMemory(input, 0);
        input.proposalType = QSBPropEditFeeParameters;
        input.bpsFee = bpsFee;
        input.protocolFee = protocolFee;
        input.protocolFeeRecipient = protocolFeeRecipient;
        input.oracleFeeRecipient = oracleFeeRecipient;
        return proposeAndApprove(proposer, approver, input);
    }

    QSB::ApproveProposal_output proposeUnpause(const id& proposer, const id& approver)
    {
        QSB::Propose_input input;
        setMemory(input, 0);
        input.proposalType = QSBPropUnpause;
        return proposeAndApprove(proposer, approver, input);
    }

    QSB::ApproveProposal_output proposeAddAdmin(const id& proposer, const id& approver, const id& newAdmin)
    {
        QSB::Propose_input input;
        setMemory(input, 0);
        input.proposalType = QSBPropAddAdmin;
        input.targetId = newAdmin;
        return proposeAndApprove(proposer, approver, input);
    }

    QSB::ApproveProposal_output proposeRemoveAdmin(const id& proposer, const id& approver, const id& target)
    {
        QSB::Propose_input input;
        setMemory(input, 0);
        input.proposalType = QSBPropRemoveAdmin;
        input.targetId = target;
        return proposeAndApprove(proposer, approver, input);
    }

    QSB::ApproveProposal_output proposeSetAdminThreshold(const id& proposer, const id& approver, uint8 newThreshold)
    {
        QSB::Propose_input input;
        setMemory(input, 0);
        input.proposalType = QSBPropSetAdminThreshold;
        input.newAdminThreshold = newThreshold;
        return proposeAndApprove(proposer, approver, input);
    }

    // ============================================================================
    // View / helper function wrappers
    // ============================================================================

    void runEndEpoch()
    {
        callSystemProcedure(QSB_CONTRACT_INDEX, END_EPOCH);
    }

    QSB::GetConfig_output getConfig() const
    {
        QSB::GetConfig_input input;
        QSB::GetConfig_output output;
        callFunction(QSB_CONTRACT_INDEX, 1, input, output);
        return output;
    }

    QSB::IsOracle_output isOracle(const id& account) const
    {
        QSB::IsOracle_input input;
        QSB::IsOracle_output output;
        input.account = account;
        callFunction(QSB_CONTRACT_INDEX, 2, input, output);
        return output;
    }

    QSB::IsPauser_output isPauser(const id& account) const
    {
        QSB::IsPauser_input input;
        QSB::IsPauser_output output;
        input.account = account;
        callFunction(QSB_CONTRACT_INDEX, 3, input, output);
        return output;
    }

    QSB::GetLockedOrder_output getLockedOrder(uint32 nonce) const
    {
        QSB::GetLockedOrder_input input;
        QSB::GetLockedOrder_output output;
        input.nonce = nonce;
        callFunction(QSB_CONTRACT_INDEX, 4, input, output);
        return output;
    }

    QSB::IsOrderFilled_output isOrderFilled(const QSB::OrderHash& hash) const
    {
        QSB::IsOrderFilled_input input;
        QSB::IsOrderFilled_output output;
        for (uint32 i = 0; i < input.hash.capacity(); ++i)
            input.hash.set(i, hash.get(i));
        callFunction(QSB_CONTRACT_INDEX, 5, input, output);
        return output;
    }

    QSB::ComputeOrderHash_output computeOrderHash(const QSB::Order& order) const
    {
        QSB::ComputeOrderHash_input input;
        QSB::ComputeOrderHash_output output;
        input.order = order;
        callFunction(QSB_CONTRACT_INDEX, 6, input, output);
        return output;
    }

    QSB::GetOracles_output getOracles() const
    {
        QSB::GetOracles_input input;
        QSB::GetOracles_output output;
        callFunction(QSB_CONTRACT_INDEX, 7, input, output);
        return output;
    }

    QSB::GetPausers_output getPausers() const
    {
        QSB::GetPausers_input input;
        QSB::GetPausers_output output;
        callFunction(QSB_CONTRACT_INDEX, 8, input, output);
        return output;
    }

    QSB::GetLockedOrders_output getLockedOrders(uint32 offset, uint32 limit) const
    {
        QSB::GetLockedOrders_input input;
        QSB::GetLockedOrders_output output;
        input.offset = offset;
        input.limit = limit;
        callFunction(QSB_CONTRACT_INDEX, 9, input, output);
        return output;
    }

    QSB::GetFilledOrders_output getFilledOrders(uint32 offset, uint32 limit) const
    {
        QSB::GetFilledOrders_input input;
        QSB::GetFilledOrders_output output;
        input.offset = offset;
        input.limit = limit;
        callFunction(QSB_CONTRACT_INDEX, 10, input, output);
        return output;
    }

    QSB::GetProposal_output getProposal(uint8 proposalId) const
    {
        QSB::GetProposal_input input;
        QSB::GetProposal_output output;
        input.proposalId = proposalId;
        callFunction(QSB_CONTRACT_INDEX, 11, input, output);
        return output;
    }

    QSB::GetProposals_output getProposals() const
    {
        QSB::GetProposals_input input;
        QSB::GetProposals_output output;
        callFunction(QSB_CONTRACT_INDEX, 12, input, output);
        return output;
    }
};

// ============================================================================
// View helper function tests (GetConfig, IsOracle, IsPauser, GetLockedOrder, IsOrderFilled)
// ============================================================================

TEST(ContractTestingQSB, TestGetConfig_ReturnsInitialState)
{
    ContractTestingQSB test;

    QSB::GetConfig_output config = test.getConfig();

    EXPECT_EQ(config.adminCount, 2);
    EXPECT_EQ(config.adminThreshold, 2);
    EXPECT_EQ(config.admins.get(0), ADMIN);
    EXPECT_EQ(config.admins.get(1), ADMIN2);
    EXPECT_EQ(config.protocolFeeRecipient, NULL_ID);
    EXPECT_EQ(config.oracleFeeRecipient, NULL_ID);
    EXPECT_EQ(config.bpsFee, 0u);
    EXPECT_EQ(config.protocolFee, 0u);
    EXPECT_EQ(config.oracleCount, 0u);
    EXPECT_EQ(config.oracleThreshold, 67);
    EXPECT_EQ((bool)config.paused, false);
}

TEST(ContractTestingQSB, TestGetConfig_ReflectsAdminAndFeeChanges)
{
    ContractTestingQSB test;

    test.proposeEditFeeParameters(ADMIN, ADMIN2, 50, 20, PROTOCOL_FEE_RECIPIENT, ORACLE_FEE_RECIPIENT);

    QSB::GetConfig_output config = test.getConfig();

    EXPECT_EQ(config.bpsFee, 50u);
    EXPECT_EQ(config.protocolFee, 20u);
    EXPECT_EQ(config.protocolFeeRecipient, PROTOCOL_FEE_RECIPIENT);
    EXPECT_EQ(config.oracleFeeRecipient, ORACLE_FEE_RECIPIENT);
}

TEST(ContractTestingQSB, TestIsOracle_ReturnsFalseWhenNotOracle)
{
    ContractTestingQSB test;

    QSB::IsOracle_output out = test.isOracle(ORACLE1);
    EXPECT_FALSE((bool)out.isOracle);

    out = test.isOracle(USER1);
    EXPECT_FALSE((bool)out.isOracle);
}

TEST(ContractTestingQSB, TestIsOracle_ReturnsTrueAfterAddRole)
{
    ContractTestingQSB test;

    test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Oracle, ORACLE1);

    QSB::IsOracle_output out = test.isOracle(ORACLE1);
    EXPECT_TRUE((bool)out.isOracle);

    out = test.isOracle(ORACLE2);
    EXPECT_FALSE((bool)out.isOracle);
}

TEST(ContractTestingQSB, TestIsPauser_ReturnsFalseWhenNotPauser)
{
    ContractTestingQSB test;

    test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Pauser, PAUSER1);

    QSB::IsPauser_output out = test.isPauser(PAUSER1);
    EXPECT_TRUE((bool)out.isPauser);

    out = test.isPauser(ORACLE1);
    EXPECT_FALSE((bool)out.isPauser);
}

TEST(ContractTestingQSB, TestIsPauser_ReturnsTrueAfterAddRole)
{
    ContractTestingQSB test;

    test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Pauser, PAUSER1);

    QSB::IsPauser_output out = test.isPauser(PAUSER1);
    EXPECT_TRUE((bool)out.isPauser);

    out = test.isPauser(USER1);
    EXPECT_FALSE((bool)out.isPauser);
}

TEST(ContractTestingQSB, TestGetLockedOrder_ReturnsNotExistsForUnknownNonce)
{
    ContractTestingQSB test;

    QSB::GetLockedOrder_output out = test.getLockedOrder(999);
    EXPECT_FALSE((bool)out.exists);
}

TEST(ContractTestingQSB, TestGetLockedOrder_ReturnsOrderAfterLock)
{
    ContractTestingQSB test;

    const uint64 amount = 1000000;
    const uint64 relayerFee = 10000;
    const uint32 nonce = 42;

    increaseEnergy(USER1, amount);
    test.lock(USER1, amount, relayerFee, 1, nonce, ContractTestingQSB::createZeroAddress(), amount);

    QSB::GetLockedOrder_output out = test.getLockedOrder(nonce);
    EXPECT_TRUE((bool)out.exists);
    EXPECT_TRUE(out.order.active);
    EXPECT_EQ(out.order.sender, USER1);
    EXPECT_EQ(out.order.amount, amount);
    EXPECT_EQ(out.order.relayerFee, relayerFee);
    EXPECT_EQ(out.order.nonce, nonce);
}

TEST(ContractTestingQSB, TestIsOrderFilled_ReturnsFalseForUnknownHash)
{
    ContractTestingQSB test;

    QSB::OrderHash unknownHash;
    for (uint32 i = 0; i < unknownHash.capacity(); ++i)
        unknownHash.set(i, (uint8)(i & 0xff));

    QSB::IsOrderFilled_output out = test.isOrderFilled(unknownHash);
    EXPECT_FALSE((bool)out.filled);

    // After marking the hash as filled via the internal helper, it should report true.
    test.getState()->forceMarkOrderFilled(unknownHash);
    QSB::IsOrderFilled_output out2 = test.isOrderFilled(unknownHash);
    EXPECT_TRUE((bool)out2.filled);
}

// ============================================================================
// New query function tests (ComputeOrderHash, GetOracles, GetPausers, GetLockedOrders, GetFilledOrders)
// ============================================================================

TEST(ContractTestingQSB, TestComputeOrderHash_ReturnsConsistentHash)
{
    ContractTestingQSB test;

    QSB::Order order = ContractTestingQSB::createTestOrderFromU32Nonce(USER1, USER2, 1000000, 10000, 99);
    QSB::ComputeOrderHash_output out = test.computeOrderHash(order);

    // Hash should be non-zero
    bool hashNonZero = false;
    for (uint32 i = 0; i < out.hash.capacity(); ++i)
    {
        if (out.hash.get(i) != 0)
        {
            hashNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(hashNonZero);

    // Same order should produce same hash
    QSB::ComputeOrderHash_output out2 = test.computeOrderHash(order);
    for (uint32 i = 0; i < out.hash.capacity(); ++i)
        EXPECT_EQ(out.hash.get(i), out2.hash.get(i));
}

TEST(ContractTestingQSB, TestComputeOrderHash_MatchesLockOutput)
{
    ContractTestingQSB test;

    const uint64 amount = 1000000;
    const uint64 relayerFee = 10000;
    const uint32 nonce = 50;

    increaseEnergy(USER1, amount);
    QSB::Lock_output lockOut = test.lock(USER1, amount, relayerFee, 1, nonce, ContractTestingQSB::createZeroAddress(), amount);
    EXPECT_TRUE(lockOut.success);

    QSB::Order order;
    order.fromAddress = USER1;
    order.toAddress = NULL_ID;
    setMemory(order.tokenIn, 0);
    setMemory(order.tokenOut, 0);
    order.amount = amount;
    order.relayerFee = relayerFee;
    order.networkIn = 1;
    order.networkOut = 1;
    setMemory(order.nonce, 0);
    order.nonce.set(0, (uint8)(nonce & 0xFF));
    order.nonce.set(1, (uint8)((nonce >> 8) & 0xFF));
    order.nonce.set(2, (uint8)((nonce >> 16) & 0xFF));
    order.nonce.set(3, (uint8)((nonce >> 24) & 0xFF));
    order.orderEra = 0;

    QSB::ComputeOrderHash_output computed = test.computeOrderHash(order);
    for (uint32 i = 0; i < lockOut.orderHash.capacity(); ++i)
        EXPECT_EQ(lockOut.orderHash.get(i), computed.hash.get(i));
}

TEST(ContractTestingQSB, TestGetOracles_ReturnsEmptyWhenNoOracles)
{
    ContractTestingQSB test;

    QSB::GetOracles_output out = test.getOracles();
    EXPECT_EQ(out.count, 0u);
}

TEST(ContractTestingQSB, TestGetOracles_ReturnsAllOraclesAfterAddRole)
{
    ContractTestingQSB test;

    test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Oracle, ORACLE1);
    test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Oracle, ORACLE2);

    QSB::GetOracles_output out = test.getOracles();
    EXPECT_EQ(out.count, 2u);
    EXPECT_EQ(out.accounts.get(0), ORACLE1);
    EXPECT_EQ(out.accounts.get(1), ORACLE2);
}

TEST(ContractTestingQSB, TestGetPausers_ReturnsEmptyWhenNoPausers)
{
    ContractTestingQSB test;

    QSB::GetPausers_output out = test.getPausers();
    EXPECT_EQ(out.count, 0u);
}

TEST(ContractTestingQSB, TestGetPausers_ReturnsAllPausersAfterAddRole)
{
    ContractTestingQSB test;

    test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Pauser, PAUSER1);

    QSB::GetPausers_output out = test.getPausers();
    EXPECT_EQ(out.count, 1u);
    EXPECT_EQ(out.accounts.get(0), PAUSER1);
}

TEST(ContractTestingQSB, TestGetLockedOrders_ReturnsEmptyWhenNoLocks)
{
    ContractTestingQSB test;

    QSB::GetLockedOrders_output out = test.getLockedOrders(0, 64);
    EXPECT_EQ(out.totalActive, 0u);
    EXPECT_EQ(out.returned, 0u);
}

TEST(ContractTestingQSB, TestGetLockedOrders_ReturnsLockedOrdersAfterLock)
{
    ContractTestingQSB test;

    const uint64 amount = 1000000;
    const uint64 relayerFee = 10000;
    const uint32 nonce = 77;

    increaseEnergy(USER1, amount);
    test.lock(USER1, amount, relayerFee, 1, nonce, ContractTestingQSB::createZeroAddress(), amount);

    QSB::GetLockedOrders_output out = test.getLockedOrders(0, 64);
    EXPECT_EQ(out.totalActive, 1u);
    EXPECT_EQ(out.returned, 1u);
    EXPECT_TRUE(out.entries.get(0).active);
    EXPECT_EQ(out.entries.get(0).sender, USER1);
    EXPECT_EQ(out.entries.get(0).amount, amount);
    EXPECT_EQ(out.entries.get(0).nonce, nonce);
}

TEST(ContractTestingQSB, TestGetLockedOrders_Pagination)
{
    ContractTestingQSB test;

    const uint64 amount = 1;
    increaseEnergy(USER1, amount * 5);

    for (uint32 i = 0; i < 5; ++i)
    {
        test.lock(USER1, amount, 0, 1, i, ContractTestingQSB::createZeroAddress(), amount);
    }

    QSB::GetLockedOrders_output out = test.getLockedOrders(0, 2);
    EXPECT_EQ(out.totalActive, 5u);
    EXPECT_EQ(out.returned, 2u);

    out = test.getLockedOrders(2, 2);
    EXPECT_EQ(out.totalActive, 5u);
    EXPECT_EQ(out.returned, 2u);

    out = test.getLockedOrders(4, 2);
    EXPECT_EQ(out.totalActive, 5u);
    EXPECT_EQ(out.returned, 1u);
}

TEST(ContractTestingQSB, TestGetLockedOrders_MostRecentFirst)
{
    ContractTestingQSB test;

    const uint64 amount = 1;
    increaseEnergy(USER1, amount * 3);

    // Lock 3 orders with nonces 10, 20, 30 (in that order)
    test.lock(USER1, amount, 0, 1, 10, ContractTestingQSB::createZeroAddress(), amount);
    test.lock(USER1, amount, 0, 1, 20, ContractTestingQSB::createZeroAddress(), amount);
    test.lock(USER1, amount, 0, 1, 30, ContractTestingQSB::createZeroAddress(), amount);

    // First page should be most-recent-first: nonce 30, 20, 10
    QSB::GetLockedOrders_output out = test.getLockedOrders(0, 64);
    EXPECT_EQ(out.totalActive, 3u);
    EXPECT_EQ(out.returned, 3u);
    EXPECT_EQ(out.entries.get(0).nonce, 30u);
    EXPECT_EQ(out.entries.get(1).nonce, 20u);
    EXPECT_EQ(out.entries.get(2).nonce, 10u);
}

TEST(ContractTestingQSB, TestGetFilledOrders_ReturnsEmptyWhenNoFills)
{
    ContractTestingQSB test;

    QSB::GetFilledOrders_output out = test.getFilledOrders(0, 64);
    EXPECT_EQ(out.totalActive, 0u);
    EXPECT_EQ(out.returned, 0u);
}

TEST(ContractTestingQSB, TestGetFilledOrders_MostRecentFirst)
{
    ContractTestingQSB test;

    // Insert 3 hashes in order: [0x01], [0x02], [0x03]
    for (uint32 i = 1; i <= 3; ++i)
    {
        QSB::OrderHash hash;
        setMemory(hash, 0);
        hash.set(0, (uint8)i);
        test.getState()->forceMarkOrderFilled(hash);
    }

    // First page should be most-recent-first: 0x03, 0x02, 0x01
    QSB::GetFilledOrders_output out = test.getFilledOrders(0, 64);
    EXPECT_EQ(out.totalActive, 3u);
    EXPECT_EQ(out.returned, 3u);
    EXPECT_EQ(out.hashes.get(0).get(0), 3u);
    EXPECT_EQ(out.hashes.get(1).get(0), 2u);
    EXPECT_EQ(out.hashes.get(2).get(0), 1u);
}

TEST(ContractTestingQSB, TestFilledOrders_RingBufferOverwritesOldEntries)
{
    ContractTestingQSB test;

    // Fill exactly one full buffer: entries 0..QSB_MAX_FILLED_ORDERS-1
    for (uint32 i = 0; i < QSB_MAX_FILLED_ORDERS; ++i)
    {
        QSB::OrderHash hash;
        setMemory(hash, 0);
        hash.set(0, (uint8)(i & 0xff));
        hash.set(1, (uint8)((i >> 8) & 0xff));
        test.getState()->forceMarkOrderFilled(hash);
    }

    // One more entry triggers era transition: hash 0 moves to filledOrdersPrev
    {
        QSB::OrderHash hash;
        setMemory(hash, 0);
        hash.set(0, (uint8)(QSB_MAX_FILLED_ORDERS & 0xff));
        hash.set(1, (uint8)((QSB_MAX_FILLED_ORDERS >> 8) & 0xff));
        test.getState()->forceMarkOrderFilled(hash);
    }

    EXPECT_EQ(test.getState()->orderEra, 1u);

    // Hash 0 is still found — it lives in filledOrdersPrev (grace window)
    QSB::OrderHash hash0;
    setMemory(hash0, 0);
    QSB::IsOrderFilled_output out0 = test.isOrderFilled(hash0);
    EXPECT_TRUE((bool)out0.filled);

    // Fill a second full buffer to push hash 0 out of filledOrdersPrev too
    for (uint32 i = QSB_MAX_FILLED_ORDERS + 1; i < QSB_MAX_FILLED_ORDERS * 2; ++i)
    {
        QSB::OrderHash hash;
        setMemory(hash, 0);
        hash.set(0, (uint8)(i & 0xff));
        hash.set(1, (uint8)((i >> 8) & 0xff));
        hash.set(2, 1); // round tag to avoid hash collisions
        test.getState()->forceMarkOrderFilled(hash);
    }
    EXPECT_EQ(test.getState()->orderEra, 2u);

    // Now hash 0 is gone from both buffers
    QSB::IsOrderFilled_output out0after = test.isOrderFilled(hash0);
    EXPECT_FALSE((bool)out0after.filled);

    // The last inserted hash (from round 2) is still present
    QSB::OrderHash hashLast;
    setMemory(hashLast, 0);
    hashLast.set(0, (uint8)((QSB_MAX_FILLED_ORDERS * 2 - 1) & 0xff));
    hashLast.set(1, (uint8)(((QSB_MAX_FILLED_ORDERS * 2 - 1) >> 8) & 0xff));
    hashLast.set(2, 1);
    QSB::IsOrderFilled_output outLast = test.isOrderFilled(hashLast);
    EXPECT_TRUE((bool)outLast.filled);
}

// ============================================================================
// Initialization Tests
// ============================================================================

TEST(ContractTestingQSB, TestInitialization)
{
    ContractTestingQSB test;

    // Check initial state
    test.getState()->checkAdminCount(2);
    test.getState()->checkAdminThreshold(2);
    test.getState()->checkIsAdmin(ADMIN);
    test.getState()->checkIsAdmin(ADMIN2);
    test.getState()->checkPaused(false);
    test.getState()->checkOracleThreshold(67); // Default 67%
    test.getState()->checkOracleCount(0);
    test.getState()->checkBpsFee(0);
    test.getState()->checkProtocolFee(0);

    test.getState()->checkProtocolFeeRecipient(NULL_ID);
    test.getState()->checkOracleFeeRecipient(NULL_ID);
}

// ============================================================================
// Lock Function Tests
// ============================================================================

TEST(ContractTestingQSB, TestLock_Success)
{
    ContractTestingQSB test;

    const uint64 amount = 1000000;
    const uint64 relayerFee = 10000;
    const uint32 networkOut = 1; // Solana
    const uint32 nonce = 1;

    // User should have enough balance
    increaseEnergy(USER1, amount);

    QSB::Lock_output output = test.lock(USER1, amount, relayerFee, networkOut, nonce, ContractTestingQSB::createZeroAddress(), amount);
    EXPECT_TRUE(output.success);

    // Check that orderHash is non-zero
    bool hashNonZero = false;
    for (uint32 i = 0; i < output.orderHash.capacity(); ++i)
    {
        if (output.orderHash.get(i) != 0)
        {
            hashNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(hashNonZero);
}

TEST(ContractTestingQSB, TestLock_FailsWhenPaused)
{
    ContractTestingQSB test;

    increaseEnergy(USER1, 1000000);

    // Pause (single-key, ADMIN is an admin/pauser)
    test.pause(ADMIN);

    // Now try to lock - should fail
    const uint64 amount = 1000000;
    long long balanceBefore = getBalance(USER1);

    QSB::Lock_output output = test.lock(USER1, amount, 10000, 1, 2, ContractTestingQSB::createZeroAddress(), amount);
    EXPECT_FALSE(output.success);

    long long balanceAfter = getBalance(USER1);
    EXPECT_EQ(balanceAfter, balanceBefore);
}

TEST(ContractTestingQSB, TestLock_FailsWhenRelayerFeeTooHigh)
{
    ContractTestingQSB test;

    const uint64 amount = 1000000;
    increaseEnergy(USER1, amount);

    QSB::Lock_output output = test.lock(USER1, amount, 1000000, 1, 3, ContractTestingQSB::createZeroAddress(), amount);
    EXPECT_FALSE(output.success);
}

TEST(ContractTestingQSB, TestLock_FailsWhenAmountIsZero)
{
    ContractTestingQSB test;

    const uint64 amount = 0;
    const uint64 relayerFee = 0;
    const uint32 nonce = 40;

    // No energy needed since amount is zero, but helper still expects an energyAmount argument
    QSB::Lock_output output = test.lock(USER1, amount, relayerFee, 1, nonce, ContractTestingQSB::createZeroAddress(), 0);
    EXPECT_FALSE(output.success);
}

TEST(ContractTestingQSB, TestLock_SucceedsWhenRelayerFeeIsAmountMinusOne)
{
    ContractTestingQSB test;

    const uint64 amount = 1000000;
    const uint64 relayerFee = amount - 1;
    const uint32 nonce = 41;

    increaseEnergy(USER1, amount);

    QSB::Lock_output output = test.lock(USER1, amount, relayerFee, 1, nonce, ContractTestingQSB::createZeroAddress(), amount);
    EXPECT_TRUE(output.success);
}

TEST(ContractTestingQSB, TestLock_FailsWhenInvocationRewardTooLowAndIsRefunded)
{
    ContractTestingQSB test;

    const uint64 amount = 1000000;
    const uint64 relayerFee = 10000;
    const uint32 nonce = 42;

    // User only sends half the required amount as invocationReward
    increaseEnergy(USER1, amount / 2);
    long long balanceBefore = getBalance(USER1);

    QSB::Lock_output output = test.lock(USER1, amount, relayerFee, 1, nonce, ContractTestingQSB::createZeroAddress(), amount / 2);
    EXPECT_FALSE(output.success);

    long long balanceAfter = getBalance(USER1);
    EXPECT_EQ(balanceAfter, balanceBefore);
}

TEST(ContractTestingQSB, TestLock_RingBufferOverwritesOldestSlot)
{
    ContractTestingQSB test;
    const uint64 amount = 1;

    // Fill all QSB_MAX_LOCKED_ORDERS slots sequentially.
    for (uint32 i = 0; i < QSB_MAX_LOCKED_ORDERS; ++i)
    {
        increaseEnergy(USER1, amount);
        QSB::Lock_output out = test.lock(USER1, amount, 0, 1, i, ContractTestingQSB::createZeroAddress(), amount);
        ASSERT_TRUE(out.success);
    }

    // Nonce 0 exists before overwrite.
    QSB::GetLockedOrder_output first = test.getLockedOrder(0);
    ASSERT_TRUE((bool)first.exists);
    EXPECT_EQ(first.order.nonce, 0u);

    // One more lock (nonce=QSB_MAX_LOCKED_ORDERS) must overwrite slot 0.
    increaseEnergy(USER1, amount);
    QSB::Lock_output overflow = test.lock(USER1, amount, 0, 1, QSB_MAX_LOCKED_ORDERS, ContractTestingQSB::createZeroAddress(), amount);
    EXPECT_TRUE(overflow.success);

    // Nonce 0 is gone: its slot was overwritten, GetLockedOrder searches by nonce.
    QSB::GetLockedOrder_output evicted = test.getLockedOrder(0);
    EXPECT_FALSE((bool)evicted.exists);

    // The new nonce is findable.
    QSB::GetLockedOrder_output newest = test.getLockedOrder(QSB_MAX_LOCKED_ORDERS);
    ASSERT_TRUE((bool)newest.exists);
    EXPECT_EQ(newest.order.nonce, QSB_MAX_LOCKED_ORDERS);
}

TEST(ContractTestingQSB, TestLock_FailsWhenNonceAlreadyUsedAndRefunds)
{
    ContractTestingQSB test;

    const uint64 amount = 1000000;
    const uint64 relayerFee = 10000;
    const uint32 nonce = 43;

    increaseEnergy(USER1, amount);
    QSB::Lock_output first = test.lock(USER1, amount, relayerFee, 1, nonce, ContractTestingQSB::createZeroAddress(), amount);
    EXPECT_TRUE(first.success);

    // Second attempt with same nonce should fail and refund invocationReward
    increaseEnergy(USER1, amount);
    long long balanceBefore = getBalance(USER1);

    QSB::Lock_output second = test.lock(USER1, amount, relayerFee, 1, nonce, ContractTestingQSB::createZeroAddress(), amount);
    EXPECT_FALSE(second.success);

    long long balanceAfter = getBalance(USER1);
    EXPECT_EQ(balanceAfter, balanceBefore);
}

TEST(ContractTestingQSB, TestLock_FailsWhenNonceAlreadyUsed)
{
    ContractTestingQSB test;

    const uint64 amount = 1000000;
    const uint64 relayerFee = 10000;
    const uint32 nonce = 4;

    increaseEnergy(USER1, amount);

    // First lock should succeed
    QSB::Lock_output output = test.lock(USER1, amount, relayerFee, 1, nonce, ContractTestingQSB::createZeroAddress(), amount);
    EXPECT_TRUE(output.success);

    // Second lock with same nonce should fail
    increaseEnergy(USER1, amount);
    QSB::Lock_output output2 = test.lock(USER1, amount, relayerFee, 1, nonce, ContractTestingQSB::createZeroAddress(), amount);
    EXPECT_FALSE(output2.success);
}

// ============================================================================
// OverrideLock Function Tests
// ============================================================================

TEST(ContractTestingQSB, TestOverrideLock_Success)
{
    ContractTestingQSB test;

    const uint64 amount = 1000000;
    const uint64 relayerFee = 10000;
    const uint32 nonce = 5;

    // First, create a lock
    increaseEnergy(USER1, amount);
    test.lock(USER1, amount, relayerFee, 1, nonce, ContractTestingQSB::createZeroAddress(), amount);

    // Now override it
    Array<uint8, 64> newAddress = ContractTestingQSB::createZeroAddress();
    newAddress.set(0, 0xFF); // Change address

    QSB::OverrideLock_output overrideOutput = test.overrideLock(USER1, nonce, 5000, newAddress);
    EXPECT_TRUE(overrideOutput.success);
}

TEST(ContractTestingQSB, TestOverrideLock_FailsWhenNotOriginalSender)
{
    ContractTestingQSB test;

    const uint64 amount = 1000000;
    const uint64 relayerFee = 10000;
    const uint32 nonce = 6;

    // USER1 creates a lock
    increaseEnergy(USER1, amount);
    test.lock(USER1, amount, relayerFee, 1, nonce, ContractTestingQSB::createZeroAddress(), amount);

    // USER2 tries to override - should fail
    QSB::OverrideLock_output overrideOutput = test.overrideLock(USER2, nonce, 5000, ContractTestingQSB::createZeroAddress());
    EXPECT_FALSE(overrideOutput.success);
}

TEST(ContractTestingQSB, TestOverrideLock_OrderNotFound)
{
    ContractTestingQSB test;

    increaseEnergy(USER1, 1);

    // Nonce 999 was never locked — should fail with OrderNotFound, not NonceUsed
    QSB::OverrideLock_output output = test.overrideLock(USER1, 999, 0, ContractTestingQSB::createZeroAddress());
    EXPECT_FALSE(output.success);
}

TEST(ContractTestingQSB, TestOverrideLock_CounterIncrements)
{
    ContractTestingQSB test;

    const uint64 amount = 1000000;
    const uint64 relayerFee = 10000;
    const uint32 nonce = 77;

    increaseEnergy(USER1, amount);
    test.lock(USER1, amount, relayerFee, 1, nonce, ContractTestingQSB::createZeroAddress(), amount);

    // Counter starts at 0
    EXPECT_EQ(test.getLockedOrder(nonce).order.overrideLockCount, 0u);

    // Each successful override increments the counter
    EXPECT_TRUE(test.overrideLock(USER1, nonce, 100, ContractTestingQSB::createZeroAddress()).success);
    EXPECT_EQ(test.getLockedOrder(nonce).order.overrideLockCount, 1u);

    EXPECT_TRUE(test.overrideLock(USER1, nonce, 200, ContractTestingQSB::createZeroAddress()).success);
    EXPECT_EQ(test.getLockedOrder(nonce).order.overrideLockCount, 2u);

    EXPECT_TRUE(test.overrideLock(USER1, nonce, 300, ContractTestingQSB::createZeroAddress()).success);
    EXPECT_EQ(test.getLockedOrder(nonce).order.overrideLockCount, 3u);
}

TEST(ContractTestingQSB, TestOverrideLock_BlockedAfterMaxAttempts)
{
    ContractTestingQSB test;

    const uint64 amount = 1000000;
    const uint64 relayerFee = 10000;
    const uint32 nonce = 78;

    increaseEnergy(USER1, amount);
    test.lock(USER1, amount, relayerFee, 1, nonce, ContractTestingQSB::createZeroAddress(), amount);

    // Three overrides succeed
    EXPECT_TRUE(test.overrideLock(USER1, nonce, 100, ContractTestingQSB::createZeroAddress()).success);
    EXPECT_TRUE(test.overrideLock(USER1, nonce, 200, ContractTestingQSB::createZeroAddress()).success);
    EXPECT_TRUE(test.overrideLock(USER1, nonce, 300, ContractTestingQSB::createZeroAddress()).success);

    // Fourth attempt is blocked
    QSB::OverrideLock_output blocked = test.overrideLock(USER1, nonce, 400, ContractTestingQSB::createZeroAddress());
    EXPECT_FALSE(blocked.success);
    // Counter must not have advanced
    EXPECT_EQ(test.getLockedOrder(nonce).order.overrideLockCount, 3u);
}

// ============================================================================
// Admin Multisig Tests
// ============================================================================

TEST(ContractTestingQSB, TestPropose_FailsWhenNotAdmin)
{
    ContractTestingQSB test;

    QSB::Propose_input input;
    setMemory(input, 0);
    input.proposalType = QSBPropAddRole;
    input.role = (uint8)QSB::Role::Oracle;
    input.targetId = ORACLE1;

    QSB::Propose_output out = test.propose(USER1, input);
    EXPECT_FALSE((bool)out.success);
    EXPECT_EQ(out.reasonCode, QSBReasonNotAdmin);
}

TEST(ContractTestingQSB, TestPropose_FailsWhenAdminExceedsConcurrentCap)
{
    ContractTestingQSB test;

    // Fill ADMIN's quota (QSB_MAX_PROPOSALS_PER_ADMIN = 3)
    QSB::Propose_input input;
    for (uint32 i = 0; i < QSB_MAX_PROPOSALS_PER_ADMIN; ++i)
    {
        setMemory(input, 0);
        input.proposalType = QSBPropAddRole;
        input.role         = (uint8)QSB::Role::Oracle;
        input.targetId     = ORACLE1;
        QSB::Propose_output out = test.propose(ADMIN, input);
        EXPECT_TRUE((bool)out.success) << "proposal " << i << " should succeed";
    }

    // The (cap+1)-th proposal from the same admin must be rejected
    setMemory(input, 0);
    input.proposalType = QSBPropAddRole;
    input.role         = (uint8)QSB::Role::Oracle;
    input.targetId     = ORACLE1;
    QSB::Propose_output overflow = test.propose(ADMIN, input);
    EXPECT_FALSE((bool)overflow.success);
    EXPECT_EQ(overflow.reasonCode, QSBReasonTooManyProposals);

    // Other admins are unaffected — ADMIN2 can still propose
    setMemory(input, 0);
    input.proposalType = QSBPropAddRole;
    input.role         = (uint8)QSB::Role::Oracle;
    input.targetId     = ORACLE1;
    QSB::Propose_output admin2Out = test.propose(ADMIN2, input);
    EXPECT_TRUE((bool)admin2Out.success);
}

TEST(ContractTestingQSB, TestPropose_AutoApprovesForProposer)
{
    ContractTestingQSB test;

    // With threshold=2, one approval doesn't execute yet
    QSB::Propose_input input;
    setMemory(input, 0);
    input.proposalType = QSBPropAddRole;
    input.role = (uint8)QSB::Role::Oracle;
    input.targetId = ORACLE1;

    QSB::Propose_output propOut = test.propose(ADMIN, input);
    EXPECT_TRUE((bool)propOut.success);

    // Proposal exists with approvalCount=1 (proposer auto-approved)
    QSB::GetProposal_output propState = test.getProposal(propOut.proposalId);
    EXPECT_TRUE((bool)propState.exists);
    EXPECT_EQ(propState.proposal.approvalCount, 1);
    EXPECT_EQ(propState.proposal.executed, 0);

    // Role not yet added
    QSB::IsOracle_output oracleOut = test.isOracle(ORACLE1);
    EXPECT_FALSE((bool)oracleOut.isOracle);
}

TEST(ContractTestingQSB, TestApproveProposal_ExecutesAtThreshold)
{
    ContractTestingQSB test;

    QSB::Propose_input input;
    setMemory(input, 0);
    input.proposalType = QSBPropAddRole;
    input.role = (uint8)QSB::Role::Oracle;
    input.targetId = ORACLE1;

    QSB::Propose_output propOut = test.propose(ADMIN, input);
    EXPECT_TRUE((bool)propOut.success);

    // Second admin approves → threshold=2 reached → executes
    QSB::ApproveProposal_output approveOut = test.approveProposal(ADMIN2, propOut.proposalId);
    EXPECT_TRUE((bool)approveOut.success);
    EXPECT_TRUE((bool)approveOut.executed);

    // Role was added
    QSB::IsOracle_output oracleOut = test.isOracle(ORACLE1);
    EXPECT_TRUE((bool)oracleOut.isOracle);
}

TEST(ContractTestingQSB, TestApproveProposal_FailsWhenNotAdmin)
{
    ContractTestingQSB test;

    QSB::Propose_input input;
    setMemory(input, 0);
    input.proposalType = QSBPropAddRole;
    input.role = (uint8)QSB::Role::Oracle;
    input.targetId = ORACLE1;

    QSB::Propose_output propOut = test.propose(ADMIN, input);
    EXPECT_TRUE((bool)propOut.success);

    QSB::ApproveProposal_output approveOut = test.approveProposal(USER1, propOut.proposalId);
    EXPECT_FALSE((bool)approveOut.success);
    EXPECT_EQ(approveOut.reasonCode, QSBReasonNotAdmin);
}

TEST(ContractTestingQSB, TestApproveProposal_FailsOnDuplicateApproval)
{
    ContractTestingQSB test;

    QSB::Propose_input input;
    setMemory(input, 0);
    input.proposalType = QSBPropAddRole;
    input.role = (uint8)QSB::Role::Oracle;
    input.targetId = ORACLE1;

    QSB::Propose_output propOut = test.propose(ADMIN, input);
    EXPECT_TRUE((bool)propOut.success);

    // ADMIN already auto-approved when proposing; approving again should fail
    QSB::ApproveProposal_output approveOut = test.approveProposal(ADMIN, propOut.proposalId);
    EXPECT_FALSE((bool)approveOut.success);
    EXPECT_EQ(approveOut.reasonCode, QSBReasonAlreadyApproved);
}

TEST(ContractTestingQSB, TestCancelProposal_ByProposer)
{
    ContractTestingQSB test;

    QSB::Propose_input input;
    setMemory(input, 0);
    input.proposalType = QSBPropAddRole;
    input.role = (uint8)QSB::Role::Oracle;
    input.targetId = ORACLE1;

    QSB::Propose_output propOut = test.propose(ADMIN, input);
    EXPECT_TRUE((bool)propOut.success);

    QSB::CancelProposal_output cancelOut = test.cancelProposal(ADMIN, propOut.proposalId);
    EXPECT_TRUE((bool)cancelOut.success);

    // Proposal no longer active
    QSB::GetProposal_output propState = test.getProposal(propOut.proposalId);
    EXPECT_FALSE((bool)propState.exists);
}

TEST(ContractTestingQSB, TestCancelProposal_FailsWhenNotProposer)
{
    ContractTestingQSB test;

    QSB::Propose_input input;
    setMemory(input, 0);
    input.proposalType = QSBPropAddRole;
    input.role = (uint8)QSB::Role::Oracle;
    input.targetId = ORACLE1;

    QSB::Propose_output propOut = test.propose(ADMIN, input);
    EXPECT_TRUE((bool)propOut.success);

    // ADMIN2 is not the proposer
    QSB::CancelProposal_output cancelOut = test.cancelProposal(ADMIN2, propOut.proposalId);
    EXPECT_FALSE((bool)cancelOut.success);
    EXPECT_EQ(cancelOut.reasonCode, QSBReasonNotProposer);
}

TEST(ContractTestingQSB, TestAddAdmin_Success)
{
    ContractTestingQSB test;

    // Add a third admin
    QSB::ApproveProposal_output out = test.proposeAddAdmin(ADMIN, ADMIN2, USER1);
    EXPECT_TRUE((bool)out.success);
    EXPECT_TRUE((bool)out.executed);

    test.getState()->checkAdminCount(3);
    test.getState()->checkIsAdmin(USER1);
}

TEST(ContractTestingQSB, TestAddAdmin_FailsWhenAlreadyAdmin)
{
    ContractTestingQSB test;

    QSB::Propose_input input;
    setMemory(input, 0);
    input.proposalType = QSBPropAddAdmin;
    input.targetId = ADMIN2;  // already an admin

    // Proposal creation should fail (or execution should fail)
    QSB::Propose_output propOut = test.propose(ADMIN, input);
    if ((bool)propOut.success)
    {
        QSB::ApproveProposal_output approveOut = test.approveProposal(ADMIN2, propOut.proposalId);
        // Execution must reject AlreadyAdmin
        EXPECT_FALSE((bool)approveOut.executed);
    }
    // Either way admin count stays 2
    test.getState()->checkAdminCount(2);
}

TEST(ContractTestingQSB, TestRemoveAdmin_Success)
{
    ContractTestingQSB test;

    // Start: 2 admins, threshold=2. Lower threshold first so we don't lock.
    test.proposeSetAdminThreshold(ADMIN, ADMIN2, 1);
    test.getState()->checkAdminThreshold(1);

    // Now remove ADMIN2
    test.proposeRemoveAdmin(ADMIN, ADMIN2, ADMIN2);
    test.getState()->checkAdminCount(1);
    test.getState()->checkIsNotAdmin(ADMIN2);
}

TEST(ContractTestingQSB, TestRemoveAdmin_FailsWouldLockContract)
{
    ContractTestingQSB test;

    // With threshold=2 and adminCount=2, removing one admin would leave 1 < threshold.
    // The Propose procedure itself rejects this before creating a proposal.
    QSB::Propose_input input;
    setMemory(input, 0);
    input.proposalType = QSBPropRemoveAdmin;
    input.targetId = ADMIN2;

    QSB::Propose_output propOut = test.propose(ADMIN, input);
    EXPECT_FALSE((bool)propOut.success);
    EXPECT_EQ(propOut.reasonCode, QSBReasonWouldLockContract);

    test.getState()->checkAdminCount(2);
}

TEST(ContractTestingQSB, TestSetAdminThreshold_Success)
{
    ContractTestingQSB test;

    // First add a third admin so threshold can be set to 3
    test.proposeAddAdmin(ADMIN, ADMIN2, USER1);
    test.getState()->checkAdminCount(3);

    test.proposeSetAdminThreshold(ADMIN, ADMIN2, 3);
    test.getState()->checkAdminThreshold(3);
}

TEST(ContractTestingQSB, TestSetAdminThreshold_FailsWouldLockContract)
{
    ContractTestingQSB test;

    // threshold=2 and adminCount=2: trying to set threshold=3 would exceed adminCount.
    // The Propose procedure itself rejects this before creating a proposal.
    QSB::Propose_input input;
    setMemory(input, 0);
    input.proposalType = QSBPropSetAdminThreshold;
    input.newAdminThreshold = 3;

    QSB::Propose_output propOut = test.propose(ADMIN, input);
    EXPECT_FALSE((bool)propOut.success);
    EXPECT_EQ(propOut.reasonCode, QSBReasonInvalidThreshold);

    test.getState()->checkAdminThreshold(2);
}

TEST(ContractTestingQSB, TestAdminSetChange_CancelsAllPendingProposals)
{
    ContractTestingQSB test;

    // Create a pending proposal
    QSB::Propose_input pendingInput;
    setMemory(pendingInput, 0);
    pendingInput.proposalType = QSBPropAddRole;
    pendingInput.role = (uint8)QSB::Role::Oracle;
    pendingInput.targetId = ORACLE1;

    QSB::Propose_output pendingProp = test.propose(ADMIN, pendingInput);
    EXPECT_TRUE((bool)pendingProp.success);

    // Confirm it's active
    QSB::GetProposal_output before = test.getProposal(pendingProp.proposalId);
    EXPECT_TRUE((bool)before.exists);

    // Execute an AddAdmin proposal (admin set change)
    test.proposeAddAdmin(ADMIN, ADMIN2, USER2);

    // The pending AddRole proposal should have been cancelled
    QSB::GetProposal_output after = test.getProposal(pendingProp.proposalId);
    EXPECT_FALSE((bool)after.exists);
}

TEST(ContractTestingQSB, TestProposalExpiry_ExpiredProposalIsInactive)
{
    ContractTestingQSB test;

    QSB::Propose_input input;
    setMemory(input, 0);
    input.proposalType = QSBPropAddRole;
    input.role = (uint8)QSB::Role::Oracle;
    input.targetId = ORACLE1;

    QSB::Propose_output propOut = test.propose(ADMIN, input);
    EXPECT_TRUE((bool)propOut.success);

    // Advance system.epoch past QSB_PROPOSAL_EXPIRY_EPOCHS and trigger END_EPOCH sweep.
    // (qpi.epoch() reads system.epoch which is 0 by default in tests)
    system.epoch = QSB_PROPOSAL_EXPIRY_EPOCHS + 1;
    test.runEndEpoch();

    // Proposal should be expired (inactive)
    QSB::GetProposal_output propState = test.getProposal(propOut.proposalId);
    EXPECT_FALSE((bool)propState.exists);

    // Trying to approve the expired proposal should fail
    QSB::ApproveProposal_output approveOut = test.approveProposal(ADMIN2, propOut.proposalId);
    EXPECT_FALSE((bool)approveOut.success);

    // Restore epoch for subsequent tests
    system.epoch = 0;
}

// ============================================================================
// Admin Function Tests (proposal-based)
// ============================================================================

TEST(ContractTestingQSB, TestEditOracleThreshold_Success)
{
    ContractTestingQSB test;

    QSB::ApproveProposal_output out = test.proposeEditOracleThreshold(ADMIN, ADMIN2, 75);
    EXPECT_TRUE((bool)out.success);
    EXPECT_TRUE((bool)out.executed);

    test.getState()->checkOracleThreshold(75);
}

TEST(ContractTestingQSB, TestAddRole_Oracle)
{
    ContractTestingQSB test;

    QSB::ApproveProposal_output out = test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Oracle, ORACLE1);
    EXPECT_TRUE((bool)out.success);
    EXPECT_TRUE((bool)out.executed);

    test.getState()->checkOracleCount(1);
}

TEST(ContractTestingQSB, TestAddRole_Pauser)
{
    ContractTestingQSB test;

    QSB::ApproveProposal_output out = test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Pauser, PAUSER1);
    EXPECT_TRUE((bool)out.success);
    EXPECT_TRUE((bool)out.executed);
}

TEST(ContractTestingQSB, TestAddRole_InvalidRole)
{
    ContractTestingQSB test;

    QSB::Propose_input input;
    setMemory(input, 0);
    input.proposalType = QSBPropAddRole;
    input.role = 99;  // invalid
    input.targetId = USER1;

    QSB::Propose_output propOut = test.propose(ADMIN, input);
    if ((bool)propOut.success)
    {
        QSB::ApproveProposal_output approveOut = test.approveProposal(ADMIN2, propOut.proposalId);
        EXPECT_FALSE((bool)approveOut.executed);
    }

    test.getState()->checkOracleCount(0);
}

TEST(ContractTestingQSB, TestAddRole_OracleFull)
{
    ContractTestingQSB test;

    for (uint32_t i = 0; i < QSB_MAX_ORACLES; ++i)
    {
        id oracle(i + 1, 0, 0, 0);
        QSB::ApproveProposal_output out = test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Oracle, oracle);
        EXPECT_TRUE((bool)out.executed);
    }
    test.getState()->checkOracleCount(QSB_MAX_ORACLES);

    id extra(QSB_MAX_ORACLES + 1, 0, 0, 0);
    QSB::Propose_input input;
    setMemory(input, 0);
    input.proposalType = QSBPropAddRole;
    input.role = (uint8)QSB::Role::Oracle;
    input.targetId = extra;
    QSB::Propose_output propOut = test.propose(ADMIN, input);
    if ((bool)propOut.success)
    {
        // executed=true means threshold was reached, not that the payload succeeded
        test.approveProposal(ADMIN2, propOut.proposalId);
    }

    // Extra oracle must not have been added
    QSB::IsOracle_output isOracleOut = test.isOracle(extra);
    EXPECT_FALSE((bool)isOracleOut.isOracle);
    test.getState()->checkOracleCount(QSB_MAX_ORACLES);
}

TEST(ContractTestingQSB, TestAddRole_PauserFull)
{
    ContractTestingQSB test;

    for (uint32_t i = 0; i < QSB_MAX_PAUSERS; ++i)
    {
        id pauser(i + 1, 0, 0, 0);
        QSB::ApproveProposal_output out = test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Pauser, pauser);
        EXPECT_TRUE((bool)out.executed);
    }

    id extra(QSB_MAX_PAUSERS + 1, 0, 0, 0);
    QSB::Propose_input input;
    setMemory(input, 0);
    input.proposalType = QSBPropAddRole;
    input.role = (uint8)QSB::Role::Pauser;
    input.targetId = extra;
    QSB::Propose_output propOut = test.propose(ADMIN, input);
    if ((bool)propOut.success)
    {
        // executed=true means threshold was reached, not that the payload succeeded
        test.approveProposal(ADMIN2, propOut.proposalId);
    }

    // Extra pauser must not have been added
    QSB::IsPauser_output isPauserOut = test.isPauser(extra);
    EXPECT_FALSE((bool)isPauserOut.isPauser);
}

TEST(ContractTestingQSB, TestRemoveRole_Oracle)
{
    ContractTestingQSB test;

    test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Oracle, ORACLE1);

    QSB::ApproveProposal_output out = test.proposeRemoveRole(ADMIN, ADMIN2, (uint8)QSB::Role::Oracle, ORACLE1);
    EXPECT_TRUE((bool)out.success);
    EXPECT_TRUE((bool)out.executed);

    test.getState()->checkOracleCount(0);
}

TEST(ContractTestingQSB, TestPause_ByAdmin)
{
    ContractTestingQSB test;

    QSB::Pause_output output = test.pause(ADMIN);
    EXPECT_TRUE(output.success);

    test.getState()->checkPaused(true);
}

TEST(ContractTestingQSB, TestPause_ByPauser)
{
    ContractTestingQSB test;

    test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Pauser, PAUSER1);

    // Pauser can pause without going through a proposal
    QSB::Pause_output output = test.pause(PAUSER1);
    EXPECT_TRUE(output.success);

    test.getState()->checkPaused(true);
}

TEST(ContractTestingQSB, TestUnpause_ByAdminProposal)
{
    ContractTestingQSB test;

    test.pause(ADMIN);
    test.getState()->checkPaused(true);

    QSB::ApproveProposal_output out = test.proposeUnpause(ADMIN, ADMIN2);
    EXPECT_TRUE((bool)out.success);
    EXPECT_TRUE((bool)out.executed);

    test.getState()->checkPaused(false);
}

TEST(ContractTestingQSB, TestUnpause_FailsForNonAdmin)
{
    ContractTestingQSB test;

    test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Pauser, PAUSER1);
    test.pause(PAUSER1);
    test.getState()->checkPaused(true);

    // Pauser must not be able to unpause (it's admin-only via proposal)
    QSB::Propose_input input;
    setMemory(input, 0);
    input.proposalType = QSBPropUnpause;

    QSB::Propose_output propOut = test.propose(PAUSER1, input);
    EXPECT_FALSE((bool)propOut.success);
    EXPECT_EQ(propOut.reasonCode, QSBReasonNotAdmin);

    test.getState()->checkPaused(true);
}

TEST(ContractTestingQSB, TestEditFeeParameters)
{
    ContractTestingQSB test;

    QSB::ApproveProposal_output out = test.proposeEditFeeParameters(ADMIN, ADMIN2, 100, 30, PROTOCOL_FEE_RECIPIENT, ORACLE_FEE_RECIPIENT);
    EXPECT_TRUE((bool)out.success);
    EXPECT_TRUE((bool)out.executed);

    test.getState()->checkBpsFee(100);
    test.getState()->checkProtocolFee(30);
    test.getState()->checkProtocolFeeRecipient(PROTOCOL_FEE_RECIPIENT);
    test.getState()->checkOracleFeeRecipient(ORACLE_FEE_RECIPIENT);
}

TEST(ContractTestingQSB, TestEditFeeParameters_RejectsTooHighBpsFee)
{
    ContractTestingQSB test;

    QSB::Propose_input input;
    setMemory(input, 0);
    input.proposalType = QSBPropEditFeeParameters;
    input.bpsFee = QSB_MAX_BPS_FEE + 1;

    QSB::Propose_output propOut = test.propose(ADMIN, input);
    if ((bool)propOut.success)
    {
        QSB::ApproveProposal_output out = test.approveProposal(ADMIN2, propOut.proposalId);
        EXPECT_FALSE((bool)out.executed);
    }

    // State should remain unchanged
    test.getState()->checkBpsFee(0);
}

TEST(ContractTestingQSB, TestEditFeeParameters_RejectsTooHighProtocolFee)
{
    ContractTestingQSB test;

    // Set an initial valid configuration first
    test.proposeEditFeeParameters(ADMIN, ADMIN2, 100, 10, PROTOCOL_FEE_RECIPIENT, ORACLE_FEE_RECIPIENT);

    // Attempt to set protocolFee above the allowed maximum
    QSB::Propose_input input;
    setMemory(input, 0);
    input.proposalType = QSBPropEditFeeParameters;
    input.bpsFee = 0;
    input.protocolFee = QSB_MAX_PROTOCOL_FEE + 1;

    QSB::Propose_output propOut = test.propose(ADMIN, input);
    if ((bool)propOut.success)
    {
        QSB::ApproveProposal_output out = test.approveProposal(ADMIN2, propOut.proposalId);
        EXPECT_FALSE((bool)out.executed);
    }

    // State should still reflect the previous valid configuration
    test.getState()->checkProtocolFee(10);
}

// ============================================================================
// Unlock Function Tests
// ============================================================================
// Note: Full unlock testing would require valid oracle signatures
// These tests verify the structure and basic validation logic

TEST(ContractTestingQSB, TestUnlock_FailsWhenNoOracles)
{
    ContractTestingQSB test;

    const uint64 contractFund = 2000000;
    increaseEnergy(USER1, contractFund);
    test.lock(USER1, contractFund, 0, 1, 1, ContractTestingQSB::createZeroAddress(), contractFund);

    Array<uint8, 32> nonce32;
    setMemory(nonce32, 0);
    nonce32.set(0, 100);
    QSB::Order order = ContractTestingQSB::createTestOrder(USER1, USER2, 1000000, 10000, nonce32);

    Array<QSB::SignatureData, QSB_MAX_ORACLES> signatures;
    setMemory(signatures, 0);

    QSB::Unlock_output output = test.unlock(USER1, order, 0, signatures);
    EXPECT_FALSE(output.success);
}

TEST(ContractTestingQSB, TestUnlock_FailsWhenPaused)
{
    ContractTestingQSB test;

    test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Oracle, ORACLE1);
    test.pause(ADMIN);

    QSB::Order order = ContractTestingQSB::createTestOrderFromU32Nonce(USER1, USER2, 1000000, 10000, 101);
    Array<QSB::SignatureData, QSB_MAX_ORACLES> signatures;
    setMemory(signatures, 0);
    signatures.set(0, test.createMockSignature(ORACLE1));

    QSB::Unlock_output output = test.unlock(USER1, order, 1, signatures);
    EXPECT_FALSE(output.success); // Should fail - contract is paused
}

TEST(ContractTestingQSB, TestUnlock_FailsWhenContractBalanceTooLow)
{
    ContractTestingQSB test;

    increaseEnergy(USER1, 1);
    increaseEnergy(USER2, 1);
    increaseEnergy(ORACLE1, 1);
    // No prior locks or deposits -> contract balance should be zero
    QSB::Order order = ContractTestingQSB::createTestOrderFromU32Nonce(USER1, USER2, 1000000, 10000, 102);
    Array<QSB::SignatureData, QSB_MAX_ORACLES> signatures;
    setMemory(signatures, 0);
    signatures.set(0, test.createMockSignature(ORACLE1));

    QSB::Unlock_output output = test.unlock(USER1, order, 1, signatures);
    EXPECT_FALSE(output.success);
}

TEST(ContractTestingQSB, TestUnlock_FailsWhenOrderAlreadyFilledBeforeOracleChecks)
{
    ContractTestingQSB test;

    // Provide some contract balance via a lock, but the specific lock
    // is intentionally unrelated to the unlock order in this model.
    const uint64 amountLocked = 1000000;
    increaseEnergy(USER1, amountLocked);
    test.lock(USER1, amountLocked, 0, 1, 500, ContractTestingQSB::createZeroAddress(), amountLocked);

    // Prepare an unlock order and compute its hash
    const uint64 amount = 100000;
    const uint64 relayerFee = 1000;
    QSB::Order order = ContractTestingQSB::createTestOrderFromU32Nonce(USER1, USER2, amount, relayerFee, 600);
    QSB::ComputeOrderHash_output hashOut = test.computeOrderHash(order);

    // Mark this order hash as already filled via the state helper
    test.getState()->forceMarkOrderFilled(hashOut.hash);

    // Attempt unlock with no oracles and no signatures — Unlock should
    // still fail with AlreadyFilled before it ever reaches oracle checks.
    Array<QSB::SignatureData, QSB_MAX_ORACLES> signatures;
    setMemory(signatures, 0);
    QSB::Unlock_output output = test.unlock(USER1, order, 0, signatures);
    EXPECT_FALSE(output.success);
}

TEST(ContractTestingQSB, TestUnlock_DoesNotRequireMatchingLock)
{
    ContractTestingQSB test;

    const uint64 contractFund = 2000000;
    increaseEnergy(USER1, contractFund);
    QSB::Lock_output lockOut = test.lock(USER1, contractFund, 0, 1, 1, ContractTestingQSB::createZeroAddress(), contractFund);
    EXPECT_TRUE(lockOut.success);

    Array<uint8, 32> nonce32;
    setMemory(nonce32, 0);
    nonce32.set(0, 0xAB);
    nonce32.set(1, 0xCD);
    QSB::Order order = ContractTestingQSB::createTestOrder(USER2, USER2, 500000, 5000, nonce32);

    Array<QSB::SignatureData, QSB_MAX_ORACLES> signatures;
    setMemory(signatures, 0);
    QSB::Unlock_output output = test.unlock(USER2, order, 0, signatures);
    EXPECT_FALSE(output.success);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(ContractTestingQSB, TestFullWorkflow_LockAndOverride)
{
    ContractTestingQSB test;

    const uint64 amount = 1000000;
    const uint64 initialRelayerFee = 10000;
    const uint64 newRelayerFee = 5000;
    const uint32 nonce = 200;

    // Step 1: Lock
    increaseEnergy(USER1, amount);
    QSB::Lock_output lockOutput = test.lock(USER1, amount, initialRelayerFee, 1, nonce, ContractTestingQSB::createZeroAddress(), amount);
    EXPECT_TRUE(lockOutput.success);

    // Step 2: Override
    QSB::OverrideLock_output overrideOutput = test.overrideLock(USER1, nonce, newRelayerFee, ContractTestingQSB::createZeroAddress());
    EXPECT_TRUE(overrideOutput.success);

    // OrderHash should be different after override
    bool hashesDifferent = false;
    for (uint32 i = 0; i < lockOutput.orderHash.capacity(); ++i)
    {
        if (lockOutput.orderHash.get(i) != overrideOutput.orderHash.get(i))
        {
            hashesDifferent = true;
            break;
        }
    }
    EXPECT_TRUE(hashesDifferent);
}

TEST(ContractTestingQSB, TestAdminWorkflow_SetupAndConfigure)
{
    ContractTestingQSB test;

    // Add oracles via multisig proposals
    test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Oracle, ORACLE1);
    test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Oracle, ORACLE2);
    test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Oracle, ORACLE3);

    test.getState()->checkOracleCount(3);

    // Update oracle threshold
    test.proposeEditOracleThreshold(ADMIN, ADMIN2, 67); // 2/3
    test.getState()->checkOracleThreshold(67);

    // Configure fees
    test.proposeEditFeeParameters(ADMIN, ADMIN2, 50, 20, PROTOCOL_FEE_RECIPIENT, ORACLE_FEE_RECIPIENT);

    test.getState()->checkBpsFee(50);
    test.getState()->checkProtocolFee(20);
}

// =============================================================================
// Order Era Tests
// =============================================================================

TEST(ContractTestingQSB, TestGetConfig_ReturnsOrderEra)
{
    ContractTestingQSB test;
    QSB::GetConfig_output config = test.getConfig();
    EXPECT_EQ(config.orderEra, 0u);
}

TEST(ContractTestingQSB, TestLock_StoresCurrentEra)
{
    ContractTestingQSB test;
    uint64 amount = 10000;
    uint32 nonce = 1;

    increaseEnergy(USER1, amount);
    QSB::Lock_output lockOutput = test.lock(USER1, amount, 0, 1, nonce, ContractTestingQSB::createZeroAddress(), amount);
    EXPECT_TRUE(lockOutput.success);

    QSB::GetLockedOrder_output lockedOrder = test.getLockedOrder(nonce);
    EXPECT_TRUE(lockedOrder.exists);
    EXPECT_EQ(lockedOrder.order.orderEra, 0u);
}

TEST(ContractTestingQSB, TestComputeOrderHash_DiffersByEra)
{
    ContractTestingQSB test;

    QSB::Order order0 = ContractTestingQSB::createTestOrderFromU32Nonce(USER1, USER2, 1000, 10, 1, 0);
    QSB::Order order1 = ContractTestingQSB::createTestOrderFromU32Nonce(USER1, USER2, 1000, 10, 1, 1);

    QSB::ComputeOrderHash_output hash0 = test.computeOrderHash(order0);
    QSB::ComputeOrderHash_output hash1 = test.computeOrderHash(order1);

    bool different = false;
    for (uint32 i = 0; i < hash0.hash.capacity(); ++i)
    {
        if (hash0.hash.get(i) != hash1.hash.get(i))
        {
            different = true;
            break;
        }
    }
    EXPECT_TRUE(different);
}

TEST(ContractTestingQSB, TestFilledOrders_EraIncrementsOnWrap)
{
    ContractTestingQSB test;

    // era should start at 0
    EXPECT_EQ(test.getState()->orderEra, 0u);

    // Force-fill 2048 orders to trigger wrap
    for (uint32 i = 0; i < QSB_MAX_FILLED_ORDERS; ++i)
    {
        QSB::OrderHash hash;
        setMemory(hash, 0);
        hash.set(0, (uint8)(i & 0xFF));
        hash.set(1, (uint8)((i >> 8) & 0xFF));
        test.getState()->forceMarkOrderFilled(hash);
    }

    // After 2048 fills, era should have incremented to 1
    EXPECT_EQ(test.getState()->orderEra, 1u);
}

TEST(ContractTestingQSB, TestOverrideLock_PreservesOriginalEra)
{
    ContractTestingQSB test;
    uint64 amount = 10000;
    uint32 nonce = 42;

    increaseEnergy(USER1, amount);
    QSB::Lock_output lockOutput = test.lock(USER1, amount, 100, 1, nonce, ContractTestingQSB::createZeroAddress(), amount);
    EXPECT_TRUE(lockOutput.success);

    QSB::GetLockedOrder_output before = test.getLockedOrder(nonce);
    EXPECT_TRUE(before.exists);
    EXPECT_EQ(before.order.orderEra, 0u);

    // Force era to 1 by filling the ring buffer
    for (uint32 i = 0; i < QSB_MAX_FILLED_ORDERS; ++i)
    {
        QSB::OrderHash hash;
        setMemory(hash, 0);
        hash.set(0, (uint8)(i & 0xFF));
        hash.set(1, (uint8)((i >> 8) & 0xFF));
        test.getState()->forceMarkOrderFilled(hash);
    }
    EXPECT_EQ(test.getState()->orderEra, 1u);

    // OverrideLock should preserve the original era (0)
    QSB::OverrideLock_output overrideOutput = test.overrideLock(USER1, nonce, 50, ContractTestingQSB::createZeroAddress());
    EXPECT_TRUE(overrideOutput.success);

    QSB::GetLockedOrder_output after = test.getLockedOrder(nonce);
    EXPECT_TRUE(after.exists);
    EXPECT_EQ(after.order.orderEra, 0u);
}

TEST(ContractTestingQSB, TestUnlock_FailsWhenEraMismatch)
{
    ContractTestingQSB test;

    // Setup: add oracle, set threshold to 1 via proposals
    test.proposeAddRole(ADMIN, ADMIN2, (uint8)QSB::Role::Oracle, ORACLE1);
    test.proposeEditOracleThreshold(ADMIN, ADMIN2, 1);

    // Fund contract with some balance
    uint64 amount = 10000;
    increaseEnergy(USER1, amount);
    test.lock(USER1, amount, 0, 1, 1, ContractTestingQSB::createZeroAddress(), amount);

    // Create order with era=5 while state is at era=0
    QSB::Order order = ContractTestingQSB::createTestOrderFromU32Nonce(USER1, USER2, amount, 10, 99, 5);

    Array<QSB::SignatureData, QSB_MAX_ORACLES> sigs;
    setMemory(sigs, 0);
    sigs.set(0, test.createMockSignature(ORACLE1));

    QSB::Unlock_output unlockOutput = test.unlock(USER1, order, 1, sigs);
    EXPECT_FALSE(unlockOutput.success);
}
