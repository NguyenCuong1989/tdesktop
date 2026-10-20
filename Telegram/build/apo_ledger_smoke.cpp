#include "apo/apo_behavior_ledger.h"
#include "apo/apo_executor.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QThread>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 3) return 30;
    auto mode = QString::fromLocal8Bit(argv[1]);
    if (mode == QStringLiteral("dialogs-postcondition")) {
        if (apo::NativeDialogsPostconditionConfirmed(true, 16, 1)) return 53;
        if (apo::NativeDialogsPostconditionConfirmed(true, 16, 15)) return 54;
        if (apo::NativeDialogsPostconditionConfirmed(false, 16, 16)) return 55;
        if (apo::NativeDialogsPostconditionConfirmed(true, 0, 0)) return 56;
        if (apo::NativeDialogsPostconditionConfirmed(true, 16, 17)) return 57;
        if (!apo::NativeDialogsPostconditionConfirmed(true, 16, 16)) return 58;
        return 0;
    }
    auto path = QString::fromLocal8Bit(argv[2]);
    auto &ledger = apo::BehaviorLedger::Instance();
    ledger.init(path);
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
