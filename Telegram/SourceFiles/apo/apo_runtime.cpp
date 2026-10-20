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
#include "settings.h"
#include <QtCore/QDir>
#include <QtCore/QDebug>

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
	// Match Telegram Desktop's own -workdir boundary. All APΩ events of
	// one local client profile must be isolated from any other profile.
	// Do not import the old app-name-global ledger: it may contain
	// mixed test accounts or concurrent local sessions.
	if (cWorkingDir().isEmpty()) {
		qWarning("APO runtime startup blocked: Telegram working directory not initialized");
		return;
	}
	const auto profileLedgerDir = QDir(cWorkingDir()).filePath(u"apo"_q);
	if (!QDir().mkpath(profileLedgerDir)) {
		qWarning("APO runtime startup blocked: profile ledger directory is unavailable");
		return;
	}
	BehaviorLedger::Instance().init(
		QDir(profileLedgerDir).filePath(u"raw_behavior.jsonl"_q));
	if (!BehaviorLedger::Instance().isInitialized()) {
		qWarning("APO runtime startup blocked: ledger is unavailable or failed integrity verification");
		return;
	}
	_running = true;

	QJsonObject payload;
	payload[QStringLiteral("status")] = QStringLiteral("APO_RUNTIME_STARTED");
	payload[QStringLiteral("mode")] = QStringLiteral("AGENT_NATIVE_TDESKTOP");
	payload[QStringLiteral("version")] = QStringLiteral("0.1.0-alpha");

	if (!RecordBehavior(
		EventType::StateReduce,
		QStringLiteral("bootstrap"),
		QStringLiteral("apo.runtime"),
		payload)) {
		_running = false;
		qWarning("APO runtime startup blocked: initial ledger event could not be persisted");
		return;
	}

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
	if (!BehaviorLedger::Instance().isInitialized()) {
		// Keep the Telegram UI alive, but disable unverified agent actions.
		_running = false;
		CognitiveLoop::Instance().stop();
		if (_heartbeatTimer) {
			_heartbeatTimer->stop();
		}
		qWarning("APO agent stopped: ledger persistence lost");
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
	QJsonObject payload;
	payload[QStringLiteral("detail_count")] = details.size();
	payload[QStringLiteral("details_redacted")] = true;
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
	payload[QStringLiteral("parameter_count")] = params.size();
	payload[QStringLiteral("parameters_redacted")] = true;
	if (const auto constructor = params.value(
		QStringLiteral("request_constructor_id")); constructor.isString()) {
		payload[QStringLiteral("request_constructor_id")] = constructor;
	}

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
	payload[QStringLiteral("result_field_count")] = result.size();
	payload[QStringLiteral("result_redacted")] = true;
	if (const auto constructor = result.value(
		QStringLiteral("response_constructor_id")); constructor.isString()) {
		payload[QStringLiteral("response_constructor_id")] = constructor;
	}
	if (const auto words = result.value(
		QStringLiteral("response_word_count")); words.isDouble()) {
		payload[QStringLiteral("response_word_count")] = words;
	}
	if (const auto code = result.value(QStringLiteral("code")); code.isDouble()) {
		payload[QStringLiteral("error_code")] = code;
	}

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
	payload[QStringLiteral("data_field_count")] = updateData.size();
	payload[QStringLiteral("data_redacted")] = true;

	RecordBehavior(
		EventType::ExternalInput,
		QString(),
		QStringLiteral("apo.server.update"),
		payload);
}

} // namespace apo
