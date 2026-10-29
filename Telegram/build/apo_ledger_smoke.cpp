#include "apo/apo_behavior_ledger.h"
#include "apo/apo_executor.h"
#include "apo/apo_verifier.h"
#include "apo/apo_state_bridge.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QThread>
#include <thread>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 3) return 30;
    auto mode = QString::fromLocal8Bit(argv[1]);
    if (mode == QStringLiteral("action-state-domain")) {
        const auto inspect = QStringLiteral("dialogs.inspect_server");
        const auto send = QStringLiteral("messages.send_text");
        if (!apo::ApoActionStateDomainMatches(inspect, inspect)) return 211;
        if (apo::ApoActionStateDomainMatches(send, inspect)) return 212;
        if (apo::ApoActionStateDomainMatches(inspect, send)) return 213;
        if (apo::ApoActionStateDomainMatches(send, send)) return 214;
        if (apo::ApoActionStateDomainMatches(QStringLiteral("unknown"), inspect)) return 215;
        return 0;
    }
    if (mode == QStringLiteral("storage-policy")) {
        constexpr auto mebibyte = apo::int64(1024) * 1024;
        if (!apo::LedgerStorageAllowsAppend(511 * mebibyte,
            mebibyte, 2 * 1024 * mebibyte)) return 106;
        if (apo::LedgerStorageAllowsAppend(512 * mebibyte,
            1, 2 * 1024 * mebibyte)) return 107;
        if (apo::LedgerStorageAllowsAppend(1, 1,
            1024 * mebibyte)) return 108;
        if (apo::LedgerStorageAllowsAppend(-1, 1,
            2 * 1024 * mebibyte)) return 109;
        return 0;
    }
    if (mode == QStringLiteral("dialogs-postcondition")) {
        if (apo::NativeDialogsPostconditionConfirmed(true, 16, 1)) return 53;
        if (apo::NativeDialogsPostconditionConfirmed(true, 16, 15)) return 54;
        if (apo::NativeDialogsPostconditionConfirmed(false, 16, 16)) return 55;
        if (apo::NativeDialogsPostconditionConfirmed(true, 0, 0)) return 56;
        if (apo::NativeDialogsPostconditionConfirmed(true, 16, 17)) return 57;
        if (!apo::NativeDialogsPostconditionConfirmed(true, 16, 16)) return 58;
        return 0;
    }
    if (mode == QStringLiteral("send-approval-preview")) {
        const auto target = QStringLiteral("peer:") + QString(160, QChar('9'));
        const auto body = QStringLiteral("Line 1 <test> & ") + QChar(10)
            + QStringLiteral("Line 2");
        const auto preview = apo::NativeSendApprovalPreview(target, body);
        if (!preview.contains(target)) return 201;
        if (!preview.contains(body)) return 202;
        if (!preview.contains(QStringLiteral("messages.send_text"))) return 203;
        if (!preview.contains(QString::number(body.size()))) return 204;
        if (preview.contains(QStringLiteral("..."))) return 205;
        return 0;
    }
    if (mode == QStringLiteral("account-binding")) {
        if (apo::NativeAccountBindingConfirmed(0, 123)) return 131;
        if (apo::NativeAccountBindingConfirmed(123, 0)) return 132;
        if (apo::NativeAccountBindingConfirmed(-1, -1)) return 133;
        if (apo::NativeAccountBindingConfirmed(123, 456)) return 134;
        if (!apo::NativeAccountBindingConfirmed(123, 123)) return 135;
        return 0;
    }
    if (mode == QStringLiteral("account-scope-binding")) {
        // Independent SHA-256 oracle for "123|C:/OmegaTest".
        const auto scope = QStringLiteral(
            "83f952818c6696dc92f62cc90c5082e2033df38eba0986dbad7915bcc7670520");
        if (!apo::NativeAccountScopeConfirmed(
            123, QStringLiteral("C:/OmegaTest"), scope)) return 136;
        if (apo::NativeAccountScopeConfirmed(
            456, QStringLiteral("C:/OmegaTest"), scope)) return 137;
        if (apo::NativeAccountScopeConfirmed(
            123, QStringLiteral("C:/Other"), scope)) return 138;
        if (apo::NativeAccountScopeConfirmed(
            0, QStringLiteral("C:/OmegaTest"), scope)) return 139;
        if (apo::NativeAccountScopeConfirmed(
            123, QString(), scope)) return 140;
        if (apo::NativeAccountScopeConfirmed(
            123, QStringLiteral("C:/OmegaTest"), QString())) return 141;
        if (apo::NativeAccountScopeConfirmed(
            123, QStringLiteral("C:/OmegaTest"), scope.toUpper())) return 142;
        return 0;
    }
    if (mode == QStringLiteral("native-body-contract")) {
        apo::NativeBodyProjection state;
        state.accountUserId = 123;
        state.accountScope = QStringLiteral("account-scope-test");
        state.sourceTraceId = QStringLiteral("body-cycle-test");
        state.measuredAtMs = QDateTime::currentMSecsSinceEpoch();
        state.sourceLoaded = true;
        state.totalLoadedRows = 7;
        state.inspectedRows = 7;
        state.historyRows = 6;
        state.userDialogs = 2;
        state.basicGroups = 1;
        state.supergroups = 1;
        state.broadcastChannels = 2;
        state.knownUnreadRows = 5;
        state.unreadDialogs = 3;
        state.mentionedDialogs = 1;
        state.complete = true;
        if (!state.structurallyValid()) return 81;
        const auto payload = state.toJson();
        if (payload.value(QStringLiteral("native_dispatch")).toBool(true)) return 82;
        if (payload.contains(QStringLiteral("account_user_id"))) return 83;
        ++state.unclassifiedPeers;
        if (state.structurallyValid()) return 84;
        --state.unclassifiedPeers;
        ++state.knownUnreadRows;
        ++state.knownUnreadRows;
        if (state.structurallyValid()) return 85;
        state.knownUnreadRows = 5;
        state.totalLoadedRows = 8;
        if (state.structurallyValid()) return 86;
        state.complete = false;
        if (!state.structurallyValid()) return 87;
        state.accountScope.clear();
        if (state.structurallyValid()) return 88;
        return 0;
    }
    auto path = QString::fromLocal8Bit(argv[2]);
    auto &ledger = apo::BehaviorLedger::Instance();
    auto limits = apo::LedgerLimits();
    if (mode == QStringLiteral("overflow")) {
        limits.maxAsyncEvents = 0;
    } else if (mode == QStringLiteral("write-failure")) {
        limits.maxLedgerBytes = 1;
        limits.reservedStorageBytes = 0;
    }
    ledger.init(path, limits);
    // Probe an existing copied native ledger without mutating it.
    if (mode == QStringLiteral("ledger-replay-probe")) {
        if (!ledger.isInitialized()) return 191;
        return ledger.totalEvents() > 0 ? 0 : 192;
    }
    if (mode == QStringLiteral("mixed-order")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 113;
        QJsonObject first;
        first[QStringLiteral("ordinal")] = 1;
        if (!ledger.recordEventAsync(apo::EventType::UiEvent,
            QStringLiteral("async-a"), QStringLiteral("test.async-ledger"), first)) return 114;
        QJsonObject second;
        second[QStringLiteral("ordinal")] = 2;
        if (!ledger.recordEvent(apo::EventType::Decision,
            QStringLiteral("sync-b"), QStringLiteral("test.sync-ledger"), second)) return 115;
        if (ledger.totalEvents() != 2) return 116;
        ledger.flush();
        return 0;
    }
    if (mode == QStringLiteral("seal-order")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 117;
        QJsonObject accepted;
        accepted[QStringLiteral("ordinal")] = 3;
        if (!ledger.recordEventAsync(apo::EventType::ExternalInput,
            QStringLiteral("async-c"), QStringLiteral("test.async-ledger"), accepted)) return 118;
        QJsonObject stopped;
        stopped[QStringLiteral("status")] = QStringLiteral("APO_RUNTIME_STOPPED");
        if (!ledger.seal(apo::EventType::StateReduce,
            QStringLiteral("shutdown"), QStringLiteral("apo.runtime"), stopped)) return 119;
        QJsonObject rejected;
        rejected[QStringLiteral("ordinal")] = 4;
        if (ledger.recordEventAsync(apo::EventType::UiEvent,
            QStringLiteral("after-seal-async"), QStringLiteral("test.async-ledger"), rejected)) return 120;
        if (ledger.recordEvent(apo::EventType::Decision,
            QStringLiteral("after-seal-sync"), QStringLiteral("test.sync-ledger"), rejected)) return 121;
        ledger.flush();
        return !ledger.isInitialized() && ledger.totalEvents() == 2 ? 0 : 122;
    }
    if (mode == QStringLiteral("overflow")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 123;
        QJsonObject payload;
        payload[QStringLiteral("ordinal")] = 5;
        if (ledger.recordEventAsync(apo::EventType::UiEvent,
            QStringLiteral("overflow-attempt"), QStringLiteral("test.async-ledger"), payload)) return 124;
        if (ledger.recordEvent(apo::EventType::Decision,
            QStringLiteral("after-overflow"), QStringLiteral("test.sync-ledger"), payload)) return 125;
        ledger.flush();
        return !ledger.isInitialized() && ledger.totalEvents() == 1 ? 0 : 126;
    }
    if (mode == QStringLiteral("overflow-reopen")) {
        if (ledger.isInitialized() || ledger.totalEvents() != 1) return 127;
        QJsonObject payload;
        if (ledger.recordEvent(apo::EventType::Decision,
            QStringLiteral("reopen-after-gap"), QStringLiteral("test.sync-ledger"), payload)) return 128;
        return 0;
    }
    if (mode == QStringLiteral("write-failure")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 129;
        QJsonObject payload;
        payload[QStringLiteral("ordinal")] = 6;
        auto firstPersisted = true;
        auto secondPersisted = true;
        auto first = std::thread([&] {
            firstPersisted = ledger.recordEvent(apo::EventType::Decision,
                QStringLiteral("write-failure-a"), QStringLiteral("test.sync-ledger"), payload);
        });
        auto second = std::thread([&] {
            secondPersisted = ledger.recordEvent(apo::EventType::Decision,
                QStringLiteral("write-failure-b"), QStringLiteral("test.sync-ledger"), payload);
        });
        first.join();
        second.join();
        if (firstPersisted || secondPersisted) return 130;
        ledger.flush();
        return !ledger.isInitialized() && ledger.totalEvents() == 0 ? 0 : 131;
    }
    if (mode == QStringLiteral("async-write")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 110;
        for (auto i = 0; i != 256; ++i) {
            QJsonObject payload;
            payload[QStringLiteral("item")] = i;
            if (!ledger.recordEventAsync(apo::EventType::UiEvent,
                QString(), QStringLiteral("test.async-ledger"), payload)) return 111;
        }
        ledger.flush();
        return ledger.totalEvents() == 256 ? 0 : 112;
    }
    if (mode == QStringLiteral("state-bridge-known-zero")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 180;
        auto &bridge = apo::StateBridge::Instance();
        bridge.updateUserProfile(81, QString(), QString(), QString(), QString(), false);
        bridge.updateStarsCredits(81, 0, 0);
        bridge.updateTelegramTonCredits(81, 0, 0);
        const auto loaded = bridge.currentSelf();
        if (!loaded.starsBalanceKnown || loaded.starsBalance != 0
            || loaded.starsBalanceNano != 0 || !loaded.telegramTonKnown
            || loaded.telegramTonWhole != 0 || loaded.telegramTonNano != 0) return 181;
        if (ledger.totalEvents() != 3) return 182;
        bridge.updateStarsCredits(81, 0, 0);
        bridge.updateTelegramTonCredits(81, 0, 0);
        if (ledger.totalEvents() != 3) return 183;
        bridge.updateUnreadCount(81, 4);
        bridge.updateUserProfile(82, QString(), QString(), QString(), QString(), false);
        const auto switched = bridge.currentSelf();
        return switched.userId == 82 && switched.unreadTotal == 0
            && !switched.starsBalanceKnown && !switched.telegramTonKnown
            && !switched.tonWalletBalanceKnown && ledger.totalEvents() == 5
            ? 0 : 184;
    }
    if (mode == QStringLiteral("state-bridge-persistence-failure")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 185;
        auto &bridge = apo::StateBridge::Instance();
        bridge.updateUserProfile(91, QString(), QString(), QString(), QString(), false);
        bridge.updateUnreadCount(91, 4);
        bridge.updateStarsCredits(91, 5, 6);
        bridge.updateTelegramTonCredits(91, 7, 8);
        const auto before = bridge.currentSelf();
        QJsonObject stopped;
        stopped[QStringLiteral("status")] = QStringLiteral("TEST_LEDGER_SEALED");
        if (!ledger.seal(apo::EventType::StateReduce,
            QStringLiteral("state-bridge-seal"), QStringLiteral("test.state-bridge"), stopped)) return 186;
        bridge.updateUnreadCount(91, 40);
        bridge.updateStarsCredits(91, 50, 60);
        bridge.updateTelegramTonCredits(91, 70, 80);
        const auto after = bridge.currentSelf();
        return !ledger.isInitialized() && ledger.totalEvents() == 5
            && after.unreadTotal == before.unreadTotal
            && after.starsBalance == before.starsBalance
            && after.starsBalanceNano == before.starsBalanceNano
            && after.starsBalanceKnown == before.starsBalanceKnown
            && after.telegramTonWhole == before.telegramTonWhole
            && after.telegramTonNano == before.telegramTonNano
            && after.telegramTonKnown == before.telegramTonKnown
            && after.lastUpdatedMs == before.lastUpdatedMs
            ? 0 : 187;
    }
    if (mode == QStringLiteral("state-bridge-profile-persistence-failure")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 193;
        auto &bridge = apo::StateBridge::Instance();
        bridge.updateUserProfile(101, QStringLiteral("synthetic-101"),
            QStringLiteral("+19995550101"), QStringLiteral("Synthetic"),
            QStringLiteral("One"), true);
        bridge.updateUnreadCount(101, 9);
        bridge.updateBalances(101, 11, 12);
        bridge.updateTelegramTonCredits(101, 13, 14);
        apo::NativeBodyProjection projection;
        projection.accountUserId = 101;
        projection.accountScope = QStringLiteral("profile-failure-account-scope");
        projection.sourceTraceId = QStringLiteral("profile-failure-projection");
        projection.measuredAtMs = QDateTime::currentMSecsSinceEpoch();
        projection.totalLoadedRows = 2;
        projection.inspectedRows = 2;
        projection.historyRows = 2;
        projection.userDialogs = 1;
        projection.basicGroups = 1;
        projection.knownUnreadRows = 2;
        projection.unreadDialogs = 1;
        projection.sourceLoaded = true;
        projection.complete = true;
        if (!bridge.updateNativeBodyProjection(projection)) return 197;
        const auto activeProjection = bridge.currentNativeBodyProjection(101);
        const auto active = bridge.currentSelf();
        if (active.userId != 101 || active.username != QStringLiteral("synthetic-101")
            || active.phone != QStringLiteral("+19995550101")
            || active.unreadTotal != 9 || !active.tonWalletBalanceKnown
            || active.tonBalanceNano != 11 || !active.starsBalanceKnown
            || active.starsBalance != 12 || !active.telegramTonKnown
            || active.telegramTonWhole != 13 || active.telegramTonNano != 14
            || !activeProjection
            || activeProjection->sourceTraceId != projection.sourceTraceId
            || ledger.totalEvents() != 5) return 194;
        QJsonObject stopped;
        stopped[QStringLiteral("status")] = QStringLiteral("TEST_LEDGER_SEALED");
        if (!ledger.seal(apo::EventType::StateReduce,
            QStringLiteral("state-bridge-profile-seal"),
            QStringLiteral("test.state-bridge"), stopped)) return 195;
        bridge.updateUserProfile(102, QStringLiteral("synthetic-102"),
            QStringLiteral("+19995550102"), QStringLiteral("Synthetic"),
            QStringLiteral("Two"), false);
        const auto failedSwitch = bridge.currentSelf();
        return !ledger.isInitialized() && ledger.totalEvents() == 6
            && failedSwitch.userId == 0 && failedSwitch.username.isEmpty()
            && failedSwitch.phone.isEmpty() && failedSwitch.firstName.isEmpty()
            && failedSwitch.lastName.isEmpty() && !failedSwitch.isPremium
            && failedSwitch.unreadTotal == 0 && failedSwitch.activeDialogsCount == 0
            && failedSwitch.tonBalanceNano == 0
            && !failedSwitch.tonWalletBalanceKnown
            && failedSwitch.telegramTonWhole == 0
            && failedSwitch.telegramTonNano == 0
            && !failedSwitch.telegramTonKnown && failedSwitch.starsBalance == 0
            && failedSwitch.starsBalanceNano == 0
            && !failedSwitch.starsBalanceKnown && failedSwitch.lastUpdatedMs == 0
            && !bridge.currentNativeBodyProjection(101)
            && !bridge.currentNativeBodyProjection(102)
            ? 0 : 196;
    }
    if (mode == QStringLiteral("native-body-bridge")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 91;
        auto &bridge = apo::StateBridge::Instance();
        bridge.updateUserProfile(77, QString(), QString(), QString(), QString(), false);
        apo::NativeBodyProjection state;
        state.accountUserId = 77;
        state.accountScope = QStringLiteral("bridge-test-account-scope");
        state.sourceTraceId = QStringLiteral("bridge-test-observed-cycle");
        state.measuredAtMs = QDateTime::currentMSecsSinceEpoch();
        state.totalLoadedRows = 3;
        state.inspectedRows = 3;
        state.historyRows = 3;
        state.userDialogs = 1;
        state.basicGroups = 1;
        state.broadcastChannels = 1;
        state.knownUnreadRows = 3;
        state.unreadDialogs = 1;
        state.sourceLoaded = true;
        state.complete = true;
        if (!bridge.updateNativeBodyProjection(state)) return 92;
        const auto known = bridge.currentNativeBodyProjection(77);
        if (!known || known->sourceTraceId != state.sourceTraceId) return 93;
        if (bridge.currentNativeBodyProjection(78)) return 94;
        if (bridge.updateNativeBodyProjection(state)) return 95;
        state.accountUserId = 78;
        state.measuredAtMs += 1;
        if (bridge.updateNativeBodyProjection(state)) return 96;
        bridge.updateUserProfile(78, QString(), QString(), QString(), QString(), false);
        if (bridge.currentNativeBodyProjection(77)) return 97;
        if (bridge.currentNativeBodyProjection(78)) return 98;
        if (ledger.totalEvents() != 3) return 99;
        return 0;
    }
    if (mode == QStringLiteral("generated-trace-unique")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 100;
        QJsonObject payload;
        payload[QStringLiteral("kind")] = QStringLiteral("TRACE_UNIQUENESS_FIXTURE");
        if (!ledger.recordEvent(apo::EventType::Computation,
            QString(), QStringLiteral("test.ledger"), payload)) return 101;
        if (!ledger.recordEvent(apo::EventType::Computation,
            QString(), QStringLiteral("test.ledger"), payload)) return 102;
        return ledger.totalEvents() == 2 ? 0 : 103;
    }
    if (mode == QStringLiteral("append-legacy-v1")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 1) return 104;
        QJsonObject payload;
        payload[QStringLiteral("kind")] = QStringLiteral("HASH_V2_UPGRADE_FIXTURE");
        return ledger.recordEvent(apo::EventType::Computation,
            QStringLiteral("v2-upgrade"), QStringLiteral("test.ledger"), payload,
            QStringLiteral("v1-fixture")) && ledger.totalEvents() == 2 ? 0 : 105;
    }
    if (mode == QStringLiteral("write")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 31;
        for (int i = 0; i != 3; ++i) {
            QJsonObject payload;
            payload[QStringLiteral("item")] = i;
            if (!ledger.recordEvent(apo::EventType::Computation,
                QStringLiteral("smoke-test"), QStringLiteral("test.ledger"), payload)) return 32;
        }
        ledger.flush();
        return ledger.totalEvents() == 3 ? 0 : 33;
    }
    if (mode == QStringLiteral("reopen")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 3) return 34;
        QJsonObject payload; payload[QStringLiteral("item")] = 3;
        return ledger.recordEvent(apo::EventType::Computation,
            QStringLiteral("smoke-test"), QStringLiteral("test.ledger"), payload)
            && ledger.totalEvents() == 4 ? 0 : 35;
    }
    if (mode == QStringLiteral("verify4")) {
        return ledger.isInitialized() && ledger.totalEvents() == 4 ? 0 : 36;
    }
    if (mode == QStringLiteral("reject")) {
        if (ledger.isInitialized()) return 37;
        QJsonObject payload;
        return ledger.recordEvent(apo::EventType::Computation,
            QStringLiteral("smoke-test"), QStringLiteral("test.ledger"), payload) ? 38 : 0;
    }
    // Two independent observations must survive a process restart and
    // cannot be replaced by baseline priors, a different account or order.
    const auto scope = QStringLiteral("test-account-scope");
    // The pre-dispatch intent must survive a crash BEFORE an RPC request ID
    // exists. Reopening must never silently mark it failed or successful.
    if (mode == QStringLiteral("recovery-intent-write")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 143;
        QJsonObject planned;
        planned[QStringLiteral("action_id")] = QStringLiteral("read-action-intent");
        planned[QStringLiteral("action_type")] = QStringLiteral("dialogs.inspect_server");
        planned[QStringLiteral("account_scope")] = scope;
        planned[QStringLiteral("phase")] = QStringLiteral("PLANNED");
        if (!ledger.recordEvent(apo::EventType::Decision, QStringLiteral("read-action-intent"),
            QStringLiteral("apo.agent_core"), planned)) return 144;
        QJsonObject intent;
        intent[QStringLiteral("action_id")] = QStringLiteral("read-action-intent");
        intent[QStringLiteral("native_method")] = QStringLiteral("messages.getDialogs");
        intent[QStringLiteral("new_phase")] = QStringLiteral("PREPARING_DISPATCH");
        intent[QStringLiteral("result")] = QStringLiteral("NATIVE_DISPATCH_INTENT_PERSISTED");
        if (!ledger.recordEvent(apo::EventType::Execution, QStringLiteral("read-action-intent"),
            QStringLiteral("apo.executor.native"), intent)) return 145;
        const auto open = ledger.unresolvedNativeReads(scope);
        return open.size() == 1 && open[0].requestId.isEmpty()
            && open[0].phase == QStringLiteral("PREPARING_DISPATCH") ? 0 : 146;
    }
    if (mode == QStringLiteral("recovery-intent-reopen")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 2) return 147;
        const auto open = ledger.unresolvedNativeReads(scope);
        if (open.size() != 1 || !open[0].requestId.isEmpty()
            || open[0].phase != QStringLiteral("PREPARING_DISPATCH")
            || !ledger.unresolvedNativeReads(QStringLiteral("other-account")).isEmpty()) return 148;
        QJsonObject closed;
        closed[QStringLiteral("action_id")] = QStringLiteral("read-action-intent");
        closed[QStringLiteral("new_phase")] = QStringLiteral("CLOSED");
        closed[QStringLiteral("result")] = QStringLiteral("SUCCESS_READ_ONLY");
        if (!ledger.recordEvent(apo::EventType::StateReduce, QStringLiteral("read-action-intent"),
            QStringLiteral("apo.executor.native"), closed)) return 149;
        return ledger.unresolvedNativeReads(scope).size() == 1 ? 0 : 150;
    }
    if (mode == QStringLiteral("recovery-intent-still-open")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 3) return 151;
        const auto open = ledger.unresolvedNativeReads(scope);
        return open.size() == 1
            && open[0].phase == QStringLiteral("CLOSED_WITHOUT_POSTCONDITION_EVIDENCE")
            ? 0 : 152;
    }
    // A transport/RPC callback after dispatch does not by itself prove
    // whether the remote side applied the operation. Keep it unresolved
    // across restart rather than silently erasing recovery evidence.
    if (mode == QStringLiteral("recovery-ambiguous-write")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 153;
        QJsonObject planned;
        planned[QStringLiteral("action_id")] = QStringLiteral("read-ambiguous");
        planned[QStringLiteral("action_type")] = QStringLiteral("dialogs.inspect_server");
        planned[QStringLiteral("account_scope")] = scope;
        planned[QStringLiteral("phase")] = QStringLiteral("PLANNED");
        if (!ledger.recordEvent(apo::EventType::Decision, QStringLiteral("read-ambiguous"),
            QStringLiteral("apo.agent_core"), planned)) return 154;
        QJsonObject intent;
        intent[QStringLiteral("action_id")] = QStringLiteral("read-ambiguous");
        intent[QStringLiteral("new_phase")] = QStringLiteral("PREPARING_DISPATCH");
        if (!ledger.recordEvent(apo::EventType::Execution, QStringLiteral("read-ambiguous"),
            QStringLiteral("apo.executor.native"), intent)) return 155;
        QJsonObject sent;
        sent[QStringLiteral("action_id")] = QStringLiteral("read-ambiguous");
        sent[QStringLiteral("request_id")] = 601;
        sent[QStringLiteral("new_phase")] = QStringLiteral("DISPATCHED");
        if (!ledger.recordEvent(apo::EventType::Execution, QStringLiteral("read-ambiguous"),
            QStringLiteral("apo.executor.native"), sent)) return 156;
        QJsonObject error;
        error[QStringLiteral("action_id")] = QStringLiteral("read-ambiguous");
        error[QStringLiteral("request_id")] = 601;
        error[QStringLiteral("new_phase")] = QStringLiteral("FAILED");
        error[QStringLiteral("result")] = QStringLiteral("NATIVE_RPC_ERROR");
        if (!ledger.recordEvent(apo::EventType::ObservedResult, QStringLiteral("read-ambiguous"),
            QStringLiteral("apo.executor.native"), error)) return 157;
        const auto open = ledger.unresolvedNativeReads(scope);
        return open.size() == 1 && open[0].requestId == QStringLiteral("601")
            && open[0].phase == QStringLiteral("UNKNOWN_OUTCOME") ? 0 : 158;
    }
    if (mode == QStringLiteral("recovery-ambiguous-reopen")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 4) return 159;
        const auto open = ledger.unresolvedNativeReads(scope);
        return open.size() == 1 && open[0].actionId == QStringLiteral("read-ambiguous")
            && open[0].requestId == QStringLiteral("601")
            && open[0].phase == QStringLiteral("UNKNOWN_OUTCOME")
            && ledger.unresolvedNativeReads(QStringLiteral("foreign-scope")).isEmpty()
            ? 0 : 160;
    }
    // Durable native send: never erase an UNKNOWN write after restart,
    // and never accept CLOSED until ACK and native postcondition both exist.
    if (mode == QStringLiteral("send-unknown-write")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 163;
        QJsonObject intent;
        intent[QStringLiteral("action_id")] = QStringLiteral("write-action");
        intent[QStringLiteral("account_scope")] = scope;
        intent[QStringLiteral("random_id_hex")] = QStringLiteral("a1234567ff");
        intent[QStringLiteral("native_method")] = QStringLiteral("messages.sendMessage");
        intent[QStringLiteral("new_phase")] = QStringLiteral("PREPARING_DISPATCH");
        if (!ledger.recordEvent(apo::EventType::Execution,
                QStringLiteral("write-action"), QStringLiteral("apo.executor.native"),
                intent)) return 164;
        QJsonObject sent;
        sent[QStringLiteral("action_id")] = QStringLiteral("write-action");
        sent[QStringLiteral("request_id")] = 807;
        sent[QStringLiteral("random_id_hex")] = QStringLiteral("a1234567ff");
        sent[QStringLiteral("new_phase")] = QStringLiteral("DISPATCHED");
        if (!ledger.recordEvent(apo::EventType::Execution,
                QStringLiteral("write-action"), QStringLiteral("apo.executor.native"),
                sent)) return 165;
        QJsonObject unknown;
        unknown[QStringLiteral("action_id")] = QStringLiteral("write-action");
        unknown[QStringLiteral("request_id")] = 807;
        unknown[QStringLiteral("random_id_hex")] = QStringLiteral("a1234567ff");
        unknown[QStringLiteral("new_phase")] = QStringLiteral("UNKNOWN_OUTCOME");
        unknown[QStringLiteral("result")] = QStringLiteral("NATIVE_RPC_ERROR");
        if (!ledger.recordEvent(apo::EventType::ObservedResult,
                QStringLiteral("write-action"), QStringLiteral("apo.executor.native"),
                unknown)) return 166;
        const auto pending = ledger.unresolvedNativeWrites(scope);
        return pending.size() == 1
            && pending[0].randomIdHex == QStringLiteral("a1234567ff")
            && pending[0].requestId == QStringLiteral("807")
            && pending[0].phase == QStringLiteral("UNKNOWN_OUTCOME")
            && !pending[0].serverAcknowledged && !pending[0].postconditionVerified
            && ledger.unresolvedNativeWrites(QStringLiteral("other-account")).isEmpty()
            ? 0 : 167;
    }
    if (mode == QStringLiteral("send-unknown-reopen")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 3) return 168;
        const auto pending = ledger.unresolvedNativeWrites(scope);
        if (pending.size() != 1
            || pending[0].phase != QStringLiteral("UNKNOWN_OUTCOME")
            || pending[0].requestId != QStringLiteral("807")) return 169;
        QJsonObject fakeClose;
        fakeClose[QStringLiteral("action_id")] = QStringLiteral("write-action");
        fakeClose[QStringLiteral("new_phase")] = QStringLiteral("CLOSED");
        fakeClose[QStringLiteral("result")] =
            QStringLiteral("SUCCESS_NATIVE_SEND_OBSERVED");
        if (!ledger.recordEvent(apo::EventType::StateReduce,
                QStringLiteral("write-action"), QStringLiteral("apo.executor.native"),
                fakeClose)) return 170;
        const auto still = ledger.unresolvedNativeWrites(scope);
        return still.size() == 1
            && still[0].phase == QStringLiteral("CORRELATION_MISSING")
            && !still[0].serverAcknowledged && !still[0].postconditionVerified
            ? 0 : 171;
    }
    // A queued write can receive a real ACK after DISPATCHED logging fails.
    // The durable intent must still correlate ACK + observed state, and close
    // without manufacturing a second send or inventing an intermediate event.
    if (mode == QStringLiteral("send-ack-without-dispatched-write")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 201;
        const auto actionId = QStringLiteral("write-ack-gap");
        const auto randomHex = QStringLiteral("f00d0000abcd");
        QJsonObject intent;
        intent[QStringLiteral("action_id")] = actionId;
        intent[QStringLiteral("account_scope")] = scope;
        intent[QStringLiteral("random_id_hex")] = randomHex;
        intent[QStringLiteral("native_method")] = QStringLiteral("messages.sendMessage");
        intent[QStringLiteral("new_phase")] = QStringLiteral("PREPARING_DISPATCH");
        if (!ledger.recordEvent(apo::EventType::Execution, actionId,
                QStringLiteral("apo.executor.native"), intent)) return 202;
        // A plausible ACK without the persisted random_id is not proof.
        QJsonObject uncorrelatedAck;
        uncorrelatedAck[QStringLiteral("action_id")] = actionId;
        uncorrelatedAck[QStringLiteral("request_id")] = 903;
        uncorrelatedAck[QStringLiteral("new_phase")] = QStringLiteral("ACKNOWLEDGED");
        uncorrelatedAck[QStringLiteral("result")] = QStringLiteral("SERVER_RESPONSE_RECEIVED");
        if (!ledger.recordEvent(apo::EventType::ObservedResult, actionId,
                QStringLiteral("apo.executor.native"), uncorrelatedAck)) return 209;
        const auto uncorrelated = ledger.unresolvedNativeWrites(scope);
        if (uncorrelated.size() != 1 || uncorrelated[0].serverAcknowledged
            || uncorrelated[0].phase != QStringLiteral("CORRELATION_MISSING")) return 210;
        QJsonObject noRequestAck;
        noRequestAck[QStringLiteral("action_id")] = actionId;
        noRequestAck[QStringLiteral("random_id_hex")] = randomHex;
        noRequestAck[QStringLiteral("new_phase")] = QStringLiteral("ACKNOWLEDGED");
        noRequestAck[QStringLiteral("result")] = QStringLiteral("SERVER_RESPONSE_RECEIVED");
        if (!ledger.recordEvent(apo::EventType::ObservedResult, actionId,
                QStringLiteral("apo.executor.native"), noRequestAck)) return 216;
        const auto missingRequest = ledger.unresolvedNativeWrites(scope);
        if (missingRequest.size() != 1 || missingRequest[0].serverAcknowledged
            || missingRequest[0].phase != QStringLiteral("ACK_REQUEST_ID_MISSING"))
            return 217;
        QJsonObject ack;
        ack[QStringLiteral("action_id")] = actionId;
        ack[QStringLiteral("random_id_hex")] = randomHex;
        ack[QStringLiteral("request_id")] = 903;
        ack[QStringLiteral("new_phase")] = QStringLiteral("ACKNOWLEDGED");
        ack[QStringLiteral("result")] = QStringLiteral("SERVER_RESPONSE_RECEIVED");
        if (!ledger.recordEvent(apo::EventType::ObservedResult, actionId,
                QStringLiteral("apo.executor.native"), ack)) return 203;
        const auto afterAck = ledger.unresolvedNativeWrites(scope);
        if (afterAck.size() != 1 || !afterAck[0].serverAcknowledged
            || afterAck[0].postconditionVerified
            || afterAck[0].requestId != QStringLiteral("903")) return 204;
        QJsonObject observed;
        observed[QStringLiteral("action_id")] = actionId;
        observed[QStringLiteral("random_id_hex")] = randomHex;
        observed[QStringLiteral("new_phase")] = QStringLiteral("STATE_CONFIRMED");
        observed[QStringLiteral("result")] = QStringLiteral("NATIVE_SEND_POSTCONDITION_MATCHED");
        // A forged success label with no actual native projection must not
        // close the write, even when the ACK and random_id are valid.
        if (!ledger.recordEvent(apo::EventType::ObservedResult, actionId,
                QStringLiteral("apo.executor.native"), observed)) return 205;
        const auto unprojected = ledger.unresolvedNativeWrites(scope);
        if (unprojected.size() != 1 || unprojected[0].postconditionVerified
            || unprojected[0].phase != QStringLiteral("POSTCONDITION_REQUEST_ID_MISSING"))
            return 211;
        // Even a plausible native projection from another RPC must not
        // complete this action's server-acknowledged request.
        observed[QStringLiteral("request_id")] = 904;
        observed[QStringLiteral("server_message_id")] = 1903;
        observed[QStringLiteral("native_projection_present")] = true;
        if (!ledger.recordEvent(apo::EventType::ObservedResult, actionId,
                QStringLiteral("apo.executor.native"), observed)) return 218;
        const auto mismatched = ledger.unresolvedNativeWrites(scope);
        if (mismatched.size() != 1 || mismatched[0].postconditionVerified
            || mismatched[0].requestId != QStringLiteral("903")
            || mismatched[0].phase != QStringLiteral("REQUEST_ID_MISMATCH"))
            return 219;
        QJsonObject closed;
        closed[QStringLiteral("action_id")] = actionId;
        closed[QStringLiteral("random_id_hex")] = randomHex;
        closed[QStringLiteral("new_phase")] = QStringLiteral("CLOSED");
        closed[QStringLiteral("result")] = QStringLiteral("SUCCESS_NATIVE_SEND_OBSERVED");
        closed[QStringLiteral("request_id")] = 904;
        if (!ledger.recordEvent(apo::EventType::StateReduce, actionId,
                QStringLiteral("apo.executor.native"), closed)) return 220;
        const auto wrongClose = ledger.unresolvedNativeWrites(scope);
        if (wrongClose.size() != 1 || wrongClose[0].postconditionVerified
            || wrongClose[0].phase != QStringLiteral("REQUEST_ID_MISMATCH"))
            return 221;
        closed[QStringLiteral("request_id")] = 903;
        if (!ledger.recordEvent(apo::EventType::StateReduce, actionId,
                QStringLiteral("apo.executor.native"), closed)) return 213;
        const auto premature = ledger.unresolvedNativeWrites(scope);
        if (premature.size() != 1 || premature[0].postconditionVerified
            || premature[0].phase != QStringLiteral("CLOSED_WITHOUT_COMPLETE_EVIDENCE"))
            return 214;
        observed[QStringLiteral("request_id")] = 903;
        observed[QStringLiteral("server_message_id")] = 1903;
        observed[QStringLiteral("native_projection_present")] = true;
        if (!ledger.recordEvent(apo::EventType::ObservedResult, actionId,
                QStringLiteral("apo.executor.native"), observed)) return 212;
        if (!ledger.recordEvent(apo::EventType::StateReduce, actionId,
                QStringLiteral("apo.executor.native"), closed)) return 206;
        return ledger.unresolvedNativeWrites(scope).empty() ? 0 : 207;
    }
    if (mode == QStringLiteral("send-ack-without-dispatched-reopen")) {
        return ledger.isInitialized() && ledger.totalEvents() == 10
            && ledger.unresolvedNativeWrites(scope).empty()
            && ledger.unresolvedNativeWrites(QStringLiteral("foreign-scope")).empty()
            ? 0 : 208;
    }
    if (mode == QStringLiteral("send-observed-reconcile")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 4) return 172;
        if (ledger.unresolvedNativeWrites(scope).size() != 1) return 173;
        QJsonObject ack;
        ack[QStringLiteral("action_id")] = QStringLiteral("write-action");
        ack[QStringLiteral("request_id")] = 807;
        ack[QStringLiteral("random_id_hex")] = QStringLiteral("a1234567ff");
        ack[QStringLiteral("new_phase")] = QStringLiteral("ACKNOWLEDGED");
        ack[QStringLiteral("result")] = QStringLiteral("SERVER_RESPONSE_RECEIVED");
        if (!ledger.recordEvent(apo::EventType::ObservedResult,
                QStringLiteral("write-action"), QStringLiteral("apo.executor.native"),
                ack)) return 174;
        QJsonObject observed;
        observed[QStringLiteral("action_id")] = QStringLiteral("write-action");
        observed[QStringLiteral("request_id")] = 807;
        observed[QStringLiteral("random_id_hex")] = QStringLiteral("a1234567ff");
        observed[QStringLiteral("new_phase")] = QStringLiteral("STATE_CONFIRMED");
        observed[QStringLiteral("result")] =
            QStringLiteral("NATIVE_SEND_POSTCONDITION_MATCHED");
        observed[QStringLiteral("server_message_id")] = 1807;
        observed[QStringLiteral("native_projection_present")] = true;
        if (!ledger.recordEvent(apo::EventType::ObservedResult,
                QStringLiteral("write-action"), QStringLiteral("apo.executor.native"),
                observed)) return 175;
        QJsonObject closed;
        closed[QStringLiteral("action_id")] = QStringLiteral("write-action");
        closed[QStringLiteral("random_id_hex")] = QStringLiteral("a1234567ff");
        closed[QStringLiteral("request_id")] = 807;
        closed[QStringLiteral("new_phase")] = QStringLiteral("CLOSED");
        closed[QStringLiteral("result")] =
            QStringLiteral("SUCCESS_NATIVE_SEND_OBSERVED");
        if (!ledger.recordEvent(apo::EventType::StateReduce,
                QStringLiteral("write-action"), QStringLiteral("apo.executor.native"),
                closed)) return 176;
        return ledger.unresolvedNativeWrites(scope).isEmpty() ? 0 : 177;
    }
    if (mode == QStringLiteral("send-closed-reopen")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 7) return 178;
        return ledger.unresolvedNativeWrites(scope).isEmpty()
            && ledger.unresolvedNativeWrites(QStringLiteral("other-account")).isEmpty()
            ? 0 : 179;
    }
    if (mode == QStringLiteral("recovery-write")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 61;
        QJsonObject planned;
        planned[QStringLiteral("action_id")] = QStringLiteral("read-action-1");
        planned[QStringLiteral("action_type")] = QStringLiteral("dialogs.inspect_server");
        planned[QStringLiteral("account_scope")] = scope;
        planned[QStringLiteral("source_trace_id")] = QStringLiteral("observed-cycle");
        planned[QStringLiteral("phase")] = QStringLiteral("PLANNED");
        if (!ledger.recordEvent(apo::EventType::Decision, QStringLiteral("read-action-1"),
            QStringLiteral("apo.agent_core"), planned)) return 62;
        QJsonObject dispatched;
        dispatched[QStringLiteral("action_id")] = QStringLiteral("read-action-1");
        dispatched[QStringLiteral("request_id")] = 583;
        dispatched[QStringLiteral("new_phase")] = QStringLiteral("DISPATCHED");
        if (!ledger.recordEvent(apo::EventType::Execution, QStringLiteral("read-action-1"),
            QStringLiteral("apo.executor.native"), dispatched)) return 63;
        QJsonObject unknown;
        unknown[QStringLiteral("action_id")] = QStringLiteral("read-action-1");
        unknown[QStringLiteral("account_scope")] = scope;
        unknown[QStringLiteral("new_phase")] = QStringLiteral("UNKNOWN_OUTCOME");
        unknown[QStringLiteral("result")] = QStringLiteral("UNKNOWN_OUTCOME");
        if (!ledger.recordEvent(apo::EventType::Computation, QStringLiteral("read-action-1"),
            QStringLiteral("apo.cognitive.remote_inspection"), unknown)) return 64;
        const auto open = ledger.unresolvedNativeReads(scope);
        return open.size() == 1 && open[0].requestId == QStringLiteral("583")
            && open[0].phase == QStringLiteral("UNKNOWN_OUTCOME") ? 0 : 65;
    }
    if (mode == QStringLiteral("recovery-reopen")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 3) return 66;
        const auto open = ledger.unresolvedNativeReads(scope);
        if (open.size() != 1 || open[0].actionId != QStringLiteral("read-action-1")
            || open[0].requestId != QStringLiteral("583")
            || open[0].phase != QStringLiteral("UNKNOWN_OUTCOME")
            || !ledger.unresolvedNativeReads(QStringLiteral("other-account")).isEmpty()) return 67;
        QJsonObject late;
        late[QStringLiteral("action_id")] = QStringLiteral("read-action-1");
        late[QStringLiteral("new_phase")] = QStringLiteral("RECONCILING");
        late[QStringLiteral("result")] = QStringLiteral("LATE_CALLBACK_IGNORED");
        if (!ledger.recordEvent(apo::EventType::ObservedResult, QStringLiteral("read-action-1"),
            QStringLiteral("apo.cognitive.remote_inspection"), late)) return 68;
        if (ledger.unresolvedNativeReads(scope).size() != 1) return 69;
        QJsonObject closed;
        closed[QStringLiteral("action_id")] = QStringLiteral("read-action-1");
        closed[QStringLiteral("new_phase")] = QStringLiteral("CLOSED");
        closed[QStringLiteral("result")] = QStringLiteral("SUCCESS_READ_ONLY");
        if (!ledger.recordEvent(apo::EventType::StateReduce, QStringLiteral("read-action-1"),
            QStringLiteral("apo.executor.native"), closed)) return 74;
        if (ledger.unresolvedNativeReads(scope).size() != 1) return 75;
        QJsonObject confirmed;
        confirmed[QStringLiteral("action_id")] = QStringLiteral("read-action-1");
        confirmed[QStringLiteral("new_phase")] = QStringLiteral("STATE_CONFIRMED");
        confirmed[QStringLiteral("result")] = QStringLiteral("NATIVE_READ_POSTCONDITION_MATCHED");
        if (!ledger.recordEvent(apo::EventType::ObservedResult, QStringLiteral("read-action-1"),
            QStringLiteral("apo.executor.native"), confirmed)) return 76;
        if (ledger.unresolvedNativeReads(scope).size() != 1) return 77;
        closed = QJsonObject();
        closed[QStringLiteral("action_id")] = QStringLiteral("read-action-1");
        closed[QStringLiteral("new_phase")] = QStringLiteral("CLOSED");
        closed[QStringLiteral("result")] = QStringLiteral("SUCCESS_READ_ONLY");
        if (!ledger.recordEvent(apo::EventType::StateReduce, QStringLiteral("read-action-1"),
            QStringLiteral("apo.executor.native"), closed)) return 70;
        return ledger.unresolvedNativeReads(scope).isEmpty() ? 0 : 71;
    }
    if (mode == QStringLiteral("recovery-closed")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 7) return 72;
        return ledger.unresolvedNativeReads(scope).isEmpty() ? 0 : 73;
    }

    QJsonObject state;
    state[QStringLiteral("C")] = 247.0 / 512.0;
    state[QStringLiteral("R")] = 1.0 / 247.0;
    state[QStringLiteral("E")] = 246.0 / 247.0;
    state[QStringLiteral("B")] = 1.0;
    const auto makePayload = [&](const QJsonObject &value) {
        QJsonObject payload;
        payload[QStringLiteral("kind")] = QStringLiteral("NATIVE_DIALOGS_STATE");
        payload[QStringLiteral("source")] = QStringLiteral("Data::Session.chatsList");
        payload[QStringLiteral("account_scope")] = scope;
        payload[QStringLiteral("measurement_complete")] = true;
        payload[QStringLiteral("measured_at_ms")] =
            static_cast<qint64>(QDateTime::currentMSecsSinceEpoch());
        payload[QStringLiteral("inspected_rows")] = 247;
        payload[QStringLiteral("total_loaded_rows")] = 247;
        payload[QStringLiteral("known_rows")] = 246;
        payload[QStringLiteral("source_loaded")] = true;
        payload[QStringLiteral("state")] = value;
        return payload;
    };
    if (mode == QStringLiteral("state-write")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 40;
        if (!ledger.recordEvent(apo::EventType::StateReduce, QStringLiteral("s0"),
            QStringLiteral("apo.cognitive.omega_measurement"), makePayload(state))) return 41;
        QThread::msleep(15);
        if (!ledger.recordEvent(apo::EventType::StateReduce, QStringLiteral("s1"),
            QStringLiteral("apo.cognitive.omega_measurement"), makePayload(state))) return 42;
        if (!ledger.verifyOmegaStatePair(scope, QStringLiteral("s0"), state,
            QStringLiteral("s1"), state)) return 43;
        if (ledger.verifyOmegaStatePair(scope, QStringLiteral("s1"), state,
            QStringLiteral("s0"), state)) return 44;
        return 0;
    }
    if (mode == QStringLiteral("state-reopen")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 2) return 45;
        if (!ledger.verifyOmegaStatePair(scope, QStringLiteral("s0"), state,
            QStringLiteral("s1"), state)) return 46;
        const auto latest = ledger.latestOmegaState(scope);
        if (!latest || latest->traceId != QStringLiteral("s1")) return 47;
        auto forged = state;
        forged[QStringLiteral("R")] = 0.07;
        if (ledger.verifyOmegaStatePair(scope, QStringLiteral("s0"), forged,
            QStringLiteral("s1"), state)) return 48;
        if (ledger.verifyOmegaStatePair(QStringLiteral("wrong-account"),
            QStringLiteral("s0"), state, QStringLiteral("s1"), state)) return 49;
        return 0;
    }
    if (mode == QStringLiteral("state-invalid")) {
        if (!ledger.isInitialized() || ledger.totalEvents() != 0) return 50;
        auto forged = state;
        forged[QStringLiteral("R")] = 0.07;
        forged[QStringLiteral("B")] = 0.985;
        if (!ledger.recordEvent(apo::EventType::StateReduce, QStringLiteral("fake"),
            QStringLiteral("apo.cognitive.omega_measurement"), makePayload(forged))) return 51;
        return ledger.latestOmegaState(scope) ? 52 : 0;
    }
    return 39;
}
