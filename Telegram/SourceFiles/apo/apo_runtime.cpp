/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "apo/apo_runtime.h"
#include "apo/apo_behavior_ledger.h"
#include "apo/apo_authority.h"
#include "apo/apo_state_bridge.h"
#include "apo/apo_cognitive_loop.h"

namespace apo {

Runtime &Runtime::Instance() {
	static Runtime instance;
	return instance;
}

Runtime::Runtime() {
}

Runtime::~Runtime() {
	stop();
}

void Runtime::start() {
	if (_running) {
		return;
	}
	_running = true;

	// Initialize the Ledger
	BehaviorLedger::Instance().init();

	QJsonObject payload;
	payload[QStringLiteral("status")] = QStringLiteral("APO_RUNTIME_STARTED");
	payload[QStringLiteral("mode")] = QStringLiteral("AGENT_NATIVE_TDESKTOP");
	payload[QStringLiteral("version")] = QStringLiteral("0.1.0-alpha");

	RecordBehavior(
		EventType::StateReduce,
		QStringLiteral("bootstrap"),
		QStringLiteral("apo.runtime"),
		payload);

	// Start 5-second background heartbeat
	_heartbeatTimer = std::make_unique<QTimer>(this);
	connect(_heartbeatTimer.get(), &QTimer::timeout, this, [this] {
		onHeartbeatTick();
	});
	_heartbeatTimer->start(5000);

	// Start native cognitive agent loop
	CognitiveLoop::Instance().start(10000);
}

void Runtime::stop() {
	if (!_running) {
		return;
	}
	_running = false;

	CognitiveLoop::Instance().stop();

	if (_heartbeatTimer) {
		_heartbeatTimer->stop();
		_heartbeatTimer.reset();
	}

	QJsonObject payload;
	payload[QStringLiteral("status")] = QStringLiteral("APO_RUNTIME_STOPPED");
	RecordBehavior(
		EventType::StateReduce,
		QStringLiteral("shutdown"),
		QStringLiteral("apo.runtime"),
		payload);

	BehaviorLedger::Instance().flush();
}

bool Runtime::isRunning() const {
	return _running;
}

void Runtime::onHeartbeatTick() {
	if (!_running) {
		return;
	}

	QJsonObject payload;
	payload[QStringLiteral("tick_time")] = static_cast<qint64>(QDateTime::currentMSecsSinceEpoch());
	payload[QStringLiteral("total_events")] = static_cast<qint64>(BehaviorLedger::Instance().totalEvents());

	RecordBehavior(
		EventType::Computation,
		QStringLiteral("heartbeat"),
		QStringLiteral("apo.runtime.heartbeat"),
		payload);
}

void Runtime::onUiEvent(
	const QString &widgetName,
	const QString &actionName,
	const QJsonObject &details) {
	QJsonObject payload = details;
	payload[QStringLiteral("widget")] = widgetName;
	payload[QStringLiteral("action")] = actionName;

	RecordBehavior(
		EventType::UiEvent,
		QString(),
		widgetName,
		payload);
}

void Runtime::onMtprotoRequest(
	int64 reqId,
	const QString &method,
	const QJsonObject &params) {
	QJsonObject payload;
	payload[QStringLiteral("req_id")] = static_cast<qint64>(reqId);
	payload[QStringLiteral("method")] = method;
	payload[QStringLiteral("params")] = params;

	RecordBehavior(
		EventType::Execution,
		QStringLiteral("mtproto-%1").arg(reqId),
		QStringLiteral("apo.mtproto.dispatcher"),
		payload);
}

void Runtime::onMtprotoResponse(
	int64 reqId,
	const QString &method,
	bool success,
	const QJsonObject &result) {
	QJsonObject payload;
	payload[QStringLiteral("req_id")] = static_cast<qint64>(reqId);
	payload[QStringLiteral("method")] = method;
	payload[QStringLiteral("success")] = success;
	payload[QStringLiteral("result")] = result;

	RecordBehavior(
		EventType::ObservedResult,
		QStringLiteral("mtproto-%1").arg(reqId),
		QStringLiteral("apo.mtproto.receiver"),
		payload);
}

void Runtime::onServerUpdate(
	const QString &updateType,
	const QJsonObject &updateData) {
	QJsonObject payload;
	payload[QStringLiteral("update_type")] = updateType;
	payload[QStringLiteral("data")] = updateData;

	RecordBehavior(
		EventType::ExternalInput,
		QString(),
		QStringLiteral("apo.server.update"),
		payload);
}

} // namespace apo
