#include "apo/apo_behavior_ledger.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 3) return 30;
    auto mode = QString::fromLocal8Bit(argv[1]);
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
    return 39;
}
