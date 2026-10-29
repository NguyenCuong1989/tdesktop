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
#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QDebug>
#include <QtCore/QCryptographicHash>
#include <QtCore/QEvent>
#include <QtCore/QMetaObject>
#include <QtCore/QMutexLocker>
#include <QtCore/QThread>

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
	{
		QMutexLocker lock(&_ingressMutex);
		_ingressOpen = true;
	}
}

void Runtime::stop() {
	{
		QMutexLocker lock(&_ingressMutex);
		_ingressOpen = false;
		if (!_running) {
			return;
		}
	}
	if (QThread::currentThread() != thread()) {
		const auto queued = QMetaObject::invokeMethod(this, [this] {
			stop();
		}, Qt::QueuedConnection);
		if (!queued) {
			qWarning("APO runtime shutdown enqueue failed");
		}
		return;
	}

	QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
	if (!_running) {
		return;
	}

	// Record an unresolved sensory wake before sealing this profile's ledger.
	// Ledger-failure stop paths continue to use stop() without this marker.
	CognitiveLoop::Instance().stop(true);
	_running = false;

	if (_heartbeatTimer) {
		_heartbeatTimer->stop();
		_heartbeatTimer.reset();
	}

	QJsonObject payload;
	payload[QStringLiteral("status")] = QStringLiteral("APO_RUNTIME_STOPPED");
	if (!BehaviorLedger::Instance().seal(
		EventType::StateReduce,
		QStringLiteral("shutdown"),
		QStringLiteral("apo.runtime"),
		payload)) {
		qWarning("APO runtime shutdown ledger seal failed");
	}

	BehaviorLedger::Instance().flush();
}

bool Runtime::isRunning() const {
	return _running;
}

void Runtime::onLedgerAdmissionFailure() {
	{
		QMutexLocker lock(&_ingressMutex);
		_ingressOpen = false;
		if (!_running) {
			return;
		}
	}
	_running = false;
	CognitiveLoop::Instance().stop();
	if (_heartbeatTimer) {
		_heartbeatTimer->stop();
	}
	qWarning("APO agent stopped: ledger queue admission failed");
}

void Runtime::onHeartbeatTick() {
	if (!_running) {
		return;
	}
	if (!BehaviorLedger::Instance().isInitialized()) {
		{
			QMutexLocker lock(&_ingressMutex);
			_ingressOpen = false;
		}
		_running = false;
		CognitiveLoop::Instance().stop();
		_heartbeatTimer->stop();
		qWarning("APO agent stopped: ledger is unavailable");
		return;
	}

	QJsonObject payload;
	payload[QStringLiteral("tick_time")] = static_cast<qint64>(QDateTime::currentMSecsSinceEpoch());
	payload[QStringLiteral("total_events")] = static_cast<qint64>(BehaviorLedger::Instance().totalEvents());

	if (!RecordBehaviorAsync(
		EventType::Computation,
		QStringLiteral("heartbeat"),
		QStringLiteral("apo.runtime.heartbeat"),
		payload)) {
		onLedgerAdmissionFailure();
	}
}

void Runtime::onUiEvent(
	const QString &widgetName,
	const QString &actionName,
	const QJsonObject &details) {
	auto accountUserId = int64(0);
	{
		QMutexLocker lock(&_ingressMutex);
		if (!_ingressOpen) {
			return;
		}
		accountUserId = StateBridge::Instance().currentSelf().userId;
		if (QThread::currentThread() != thread()) {
			const auto queued = QMetaObject::invokeMethod(this, [=] {
				recordUiEvent(widgetName, actionName, details, accountUserId);
			}, Qt::QueuedConnection);
			if (!queued) {
				qWarning("APO runtime ingress enqueue failed: UI event");
			}
			return;
		}
	}
	recordUiEvent(widgetName, actionName, details, accountUserId);
}

void Runtime::recordUiEvent(
		const QString &widgetName,
		const QString &actionName,
		const QJsonObject &details,
		int64 accountUserId) {
	if (!_running) {
		return;
	}
	const auto admittedAtMs = QDateTime::currentMSecsSinceEpoch();
	const auto traceId = QStringLiteral("sensory-%1-%2")
		.arg(admittedAtMs)
		.arg(++_sensorySequence);
	QJsonObject payload;
	payload[QStringLiteral("detail_count")] = details.size();
	payload[QStringLiteral("details_redacted")] = true;
	payload[QStringLiteral("widget")] = widgetName;
	payload[QStringLiteral("action")] = actionName;

	if (!RecordBehaviorAsync(
		EventType::UiEvent,
		traceId,
		widgetName,
		payload)) {
		onLedgerAdmissionFailure();
		return;
	}
	CognitiveLoop::Instance().requestWake(
		QStringLiteral("ui_event"), traceId, admittedAtMs,
		accountUserId);
}

void Runtime::onMtprotoRequest(
	int64 reqId,
	const QString &method,
	const QJsonObject &params) {
	auto accountUserId = int64(0);
	{
		QMutexLocker lock(&_ingressMutex);
		if (!_ingressOpen) {
			return;
		}
		accountUserId = StateBridge::Instance().currentSelf().userId;
		if (QThread::currentThread() != thread()) {
			const auto queued = QMetaObject::invokeMethod(this, [=] {
				recordMtprotoRequest(reqId, method, params, accountUserId);
			}, Qt::QueuedConnection);
			if (!queued) {
				qWarning("APO runtime ingress enqueue failed: MTProto request");
			}
			return;
		}
	}
	recordMtprotoRequest(reqId, method, params, accountUserId);
}

void Runtime::recordMtprotoRequest(
		int64 reqId,
		const QString &method,
		const QJsonObject &params,
		int64 accountUserId) {
	if (!_running) {
		return;
	}
	const auto admittedAtMs = QDateTime::currentMSecsSinceEpoch();
	const auto traceId = QStringLiteral("mtproto-%1").arg(reqId);
	QJsonObject payload;
	payload[QStringLiteral("req_id")] = static_cast<qint64>(reqId);
	payload[QStringLiteral("method")] = method;
	payload[QStringLiteral("parameter_count")] = params.size();
	payload[QStringLiteral("parameters_redacted")] = true;
	if (const auto constructor = params.value(
		QStringLiteral("request_constructor_id")); constructor.isString()) {
		payload[QStringLiteral("request_constructor_id")] = constructor;
	}

	if (!RecordBehaviorAsync(
		EventType::Execution,
		traceId,
		QStringLiteral("apo.mtproto.dispatcher"),
		payload)) {
		onLedgerAdmissionFailure();
		return;
	}
	CognitiveLoop::Instance().requestWake(
		QStringLiteral("mtproto_request"), traceId, admittedAtMs,
		accountUserId);
}

void Runtime::onMtprotoResponse(
	int64 reqId,
	const QString &method,
	bool success,
	const QJsonObject &result) {
	auto accountUserId = int64(0);
	{
		QMutexLocker lock(&_ingressMutex);
		if (!_ingressOpen) {
			return;
		}
		accountUserId = StateBridge::Instance().currentSelf().userId;
		if (QThread::currentThread() != thread()) {
			const auto queued = QMetaObject::invokeMethod(this, [=] {
				recordMtprotoResponse(
					reqId, method, success, result, accountUserId);
			}, Qt::QueuedConnection);
			if (!queued) {
				qWarning("APO runtime ingress enqueue failed: MTProto response");
			}
			return;
		}
	}
	recordMtprotoResponse(reqId, method, success, result, accountUserId);
}

void Runtime::recordMtprotoResponse(
		int64 reqId,
		const QString &method,
		bool success,
		const QJsonObject &result,
		int64 accountUserId) {
	if (!_running) {
		return;
	}
	const auto admittedAtMs = QDateTime::currentMSecsSinceEpoch();
	const auto traceId = QStringLiteral("mtproto-%1").arg(reqId);
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

	if (!RecordBehaviorAsync(
		EventType::ObservedResult,
		traceId,
		QStringLiteral("apo.mtproto.receiver"),
		payload)) {
		onLedgerAdmissionFailure();
		return;
	}
	CognitiveLoop::Instance().requestWake(
		QStringLiteral("mtproto_response"), traceId, admittedAtMs,
		accountUserId);
}

void Runtime::onServerUpdate(
	const QString &updateType,
	const QJsonObject &updateData,
	int64 accountUserId) {
	{
		QMutexLocker lock(&_ingressMutex);
		if (!_ingressOpen) {
			return;
		}
		if (QThread::currentThread() != thread()) {
			const auto queued = QMetaObject::invokeMethod(this, [=] {
				recordServerUpdate(updateType, updateData, accountUserId);
			}, Qt::QueuedConnection);
			if (!queued) {
				qWarning("APO runtime ingress enqueue failed: server update");
			}
			return;
		}
	}
	recordServerUpdate(updateType, updateData, accountUserId);
}

void Runtime::recordServerUpdate(
		const QString &updateType,
		const QJsonObject &updateData,
		int64 accountUserId) {
	if (!_running) {
		return;
	}
	const auto admittedAtMs = QDateTime::currentMSecsSinceEpoch();
	const auto traceId = QStringLiteral("sensory-%1-%2")
		.arg(admittedAtMs)
		.arg(++_sensorySequence);
	QJsonObject payload;
	payload[QStringLiteral("update_type")] = updateType;
	payload[QStringLiteral("data_redacted")] = true;
	const auto copyAllowed = [&](const auto &key) {
		const auto value = updateData.value(key);
		if (!value.isUndefined()) {
			payload[key] = value;
		}
	};
	copyAllowed(QStringLiteral("constructor_id"));
	copyAllowed(QStringLiteral("envelope_type"));
	copyAllowed(QStringLiteral("phase"));
	copyAllowed(QStringLiteral("dispatch_mode"));

	if (!RecordBehaviorAsync(
		EventType::ExternalInput,
		traceId,
		QStringLiteral("apo.server.update"),
		payload)) {
		onLedgerAdmissionFailure();
		return;
	}
	CognitiveLoop::Instance().requestWake(
		QStringLiteral("server_update"), traceId, admittedAtMs,
		accountUserId);
}

void Runtime::onNativeChangeNotificationsFlushed(
		int64 accountUserId,
		NativeChangeNotificationCounts counts) {
	{
		QMutexLocker lock(&_ingressMutex);
		if (!_ingressOpen) {
			return;
		}
		if (QThread::currentThread() != thread()) {
			const auto queued = QMetaObject::invokeMethod(
				this,
				[this, accountUserId, counts] {
					recordNativeChangeNotificationsFlushed(
						accountUserId,
						counts);
				},
				Qt::QueuedConnection);
			if (!queued) {
				qWarning("APO runtime ingress enqueue failed: model notification");
			}
			return;
		}
	}
	recordNativeChangeNotificationsFlushed(accountUserId, counts);
}

void Runtime::recordNativeChangeNotificationsFlushed(
		int64 accountUserId,
		NativeChangeNotificationCounts counts) {
	// Do not mix old account callbacks with a newly active body projection.
	if (!_running || !accountUserId
		|| StateBridge::Instance().currentSelf().userId != accountUserId) {
		return;
	}
	const auto &domain = Core::App().domain();
	if (!domain.started() || domain.accounts().empty()) {
		return;
	}
	const auto session = domain.active().maybeSession();
	if (!session || session->userId().bare != accountUserId) {
		return;
	}
	const auto admittedAtMs = QDateTime::currentMSecsSinceEpoch();
	const auto traceId = QStringLiteral("model-notify-%1-%2")
		.arg(admittedAtMs)
		.arg(++_sensorySequence);
	// Match CognitiveLoop's existing account/workdir projection exactly.
	// Scope is a digest, not a raw Telegram account identifier.
	const auto accountScope = QString::fromUtf8(QCryptographicHash::hash(
		(QString::number(accountUserId) + QStringLiteral("|")
			+ cWorkingDir()).toUtf8(),
		QCryptographicHash::Sha256).toHex());
	const auto payload = QJsonObject{
		{ QStringLiteral("phase"),
			QStringLiteral("MODEL_NOTIFICATIONS_FLUSHED") },
		{ QStringLiteral("account_scope"), accountScope },
		{ QStringLiteral("data_redacted"), true },
		{ QStringLiteral("peer_notification_count"), counts.peer },
		{ QStringLiteral("history_notification_count"), counts.history },
		{ QStringLiteral("message_notification_count"), counts.message },
		{ QStringLiteral("entry_notification_count"), counts.entry },
		{ QStringLiteral("topic_notification_count"), counts.topic },
		{ QStringLiteral("sublist_notification_count"), counts.sublist },
		{ QStringLiteral("story_notification_count"), counts.story },
		{ QStringLiteral("ledger_admission"),
			QStringLiteral("QUEUED_NOT_DURABLE") },
	};
	if (!RecordBehaviorAsync(
		EventType::StateReduce,
		traceId,
		QStringLiteral("apo.native.model_notifications"),
		payload)) {
		onLedgerAdmissionFailure();
		return;
	}
	// Model notification means re-evaluate the body, not GoalSatisfied.
	CognitiveLoop::Instance().requestWake(
		QStringLiteral("native_model_notifications"),
		traceId,
		admittedAtMs,
		accountUserId);
}

} // namespace apo
