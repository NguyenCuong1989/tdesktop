/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "apo/apo_cognitive_loop.h"
#include "apo/apo_state_bridge.h"
#include "apo/apo_verifier.h"
#include "apo/apo_executor.h"
#include "apo/apo_behavior_ledger.h"
#include "core/application.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "history/history.h"
#include "dialogs/dialogs_main_list.h"
#include "settings.h"
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <algorithm>
#include <utility>
#include <vector>

namespace apo {

CognitiveLoop &CognitiveLoop::Instance() {
	static CognitiveLoop instance;
	return instance;
}

CognitiveLoop::CognitiveLoop() {
}

CognitiveLoop::~CognitiveLoop() {
	stop();
}

void CognitiveLoop::start(int intervalMs) {
	if (_running) {
		return;
	}
	_running = true;

	_loopTimer = std::make_unique<QTimer>(this);
	connect(_loopTimer.get(), &QTimer::timeout, this, [this] {
		step();
	});
	_loopTimer->start(intervalMs);
	_wakeTimer = std::make_unique<QTimer>(this);
	_wakeTimer->setSingleShot(true);
	connect(_wakeTimer.get(), &QTimer::timeout, this, [this] {
		if (!_running || !_pendingWakeCount) {
			return;
		}
		if (_stepActive) {
			_wakeTimer->start(250);
			return;
		}
		_activeWakeCount = _pendingWakeCount;
		_activeWakeAdmittedAtMs = _pendingWakeAdmittedAtMs;
		_activeWakeStartedAtMs = QDateTime::currentMSecsSinceEpoch();
		_activeWakeAccountUserId = _pendingWakeAccountUserId;
		_activeWakeSources = std::move(_pendingWakeSources);
		_activeWakeSourceTraceId = std::move(_pendingWakeSourceTraceId);
		_pendingWakeCount = 0;
		_pendingWakeAdmittedAtMs = 0;
		_pendingWakeAccountUserId = 0;
		step();
		_activeWakeCount = 0;
		_activeWakeAdmittedAtMs = 0;
		_activeWakeStartedAtMs = 0;
		_activeWakeAccountUserId = 0;
		_activeWakeSources.clear();
		_activeWakeSourceTraceId.clear();
	});

	QJsonObject payload;
	payload[QStringLiteral("status")] = QStringLiteral("COGNITIVE_LOOP_STARTED");
	payload[QStringLiteral("interval_ms")] = intervalMs;
	RecordBehavior(
		EventType::StateReduce,
		QStringLiteral("cognitive.start"),
		QStringLiteral("apo.cognitive_loop"),
		payload);
}

void CognitiveLoop::stop(bool recordPendingWake) {
	if (!_running) {
		return;
	}
	// The ingress may already be durable while its 250 ms wake timer
	// has not fired. Classify it honestly before orderly ledger seal.
	// Never replay historical network activity or claim a reflex occurred.
	if (recordPendingWake && _pendingWakeCount > 0) {
		QJsonObject pending;
		pending[QStringLiteral("kind")] =
			QStringLiteral("NATIVE_SENSORY_WAKE_UNRESOLVED");
		pending[QStringLiteral("phase")] = QStringLiteral("PENDING");
		pending[QStringLiteral("result")] =
			QStringLiteral("NOT_REPLAYED_NO_ACTION");
		pending[QStringLiteral("sources")] =
			QJsonArray::fromStringList(_pendingWakeSources);
		pending[QStringLiteral("coalesced_events")] = _pendingWakeCount;
		pending[QStringLiteral("first_source_trace_id")] =
			_pendingWakeSourceTraceId;
		pending[QStringLiteral("wait_age_ms")] = static_cast<qint64>(
			std::max<int64>(0,
				QDateTime::currentMSecsSinceEpoch() - _pendingWakeAdmittedAtMs));
		if (_pendingWakeAccountUserId == 0) {
			pending[QStringLiteral("scope_kind")] =
				QStringLiteral("anonymous_profile");
			pending[QStringLiteral("profile_scope")] = QString::fromUtf8(
				QCryptographicHash::hash(
					(QStringLiteral("anonymous|") + cWorkingDir()).toUtf8(),
					QCryptographicHash::Sha256).toHex());
		} else {
			// The source account is historical provenance, NOT proof that
			// this is still the active authenticated Telegram session.
			pending[QStringLiteral("scope_kind")] =
				QStringLiteral("source_account_at_admission");
			pending[QStringLiteral("account_scope")] = QString::fromUtf8(
				QCryptographicHash::hash(
					(QString::number(_pendingWakeAccountUserId)
						+ QStringLiteral("|") + cWorkingDir()).toUtf8(),
					QCryptographicHash::Sha256).toHex());
		}
		const auto traceId = QStringLiteral("wake-stop-%1-%2")
			.arg(_cycleCount)
			.arg(QDateTime::currentMSecsSinceEpoch());
		if (!RecordBehavior(EventType::Computation, traceId,
					QStringLiteral("apo.cognitive.sensory_wake"), pending,
					_pendingWakeSourceTraceId)) {
			qWarning("APO pending wake could not be persisted on orderly stop");
		}
	}
	_running = false;
	_lastRankingAccount = 0;
	_lastRemoteInspectionAccount = 0;
	_lastRemoteInspectionAtMs = 0;
	_lastRemoteInspectionSucceeded = false;
	_remoteInspectionInFlight = false;
	_lastRemoteInspectionActionId.clear();
	_lastRemoteInspectionSourceTrace.clear();
	_lastRemoteInspectionAccountScope.clear();
	_lastRecoveryAccountScope.clear();
	_stepActive = false;
	_pendingWakeCount = 0;
	_pendingWakeAdmittedAtMs = 0;
	_pendingWakeAccountUserId = 0;
	_pendingWakeSources.clear();
	_pendingWakeSourceTraceId.clear();
	_activeWakeCount = 0;
	_activeWakeAdmittedAtMs = 0;
	_activeWakeStartedAtMs = 0;
	_activeWakeAccountUserId = 0;
	_activeWakeSources.clear();
	_activeWakeSourceTraceId.clear();
	++_remoteInspectionGeneration; // Invalidate callbacks from a stopped loop.
	_lastRankingEvidence.clear();
	if (_loopTimer) {
		_loopTimer->stop();
		_loopTimer.reset();
	}
	if (_wakeTimer) {
		_wakeTimer->stop();
		_wakeTimer.reset();
	}
}

bool CognitiveLoop::isRunning() const {
	return _running;
}

void CognitiveLoop::requestWake(
		const QString &source,
		const QString &sourceTraceId,
		int64 admittedAtMs,
		int64 accountUserId) {
	if (!_running || !_wakeTimer || source.isEmpty() || admittedAtMs <= 0) {
		return;
	}
	if (_pendingWakeCount && _pendingWakeAccountUserId != accountUserId) {
		_pendingWakeCount = 0;
		_pendingWakeSources.clear();
		_wakeTimer->start(250);
	}
	if (!_pendingWakeCount) {
		_pendingWakeAdmittedAtMs = admittedAtMs;
		_pendingWakeAccountUserId = accountUserId;
		_pendingWakeSourceTraceId = sourceTraceId;
	}
	if (!_pendingWakeSources.contains(source)) {
		_pendingWakeSources.push_back(source);
	}
	++_pendingWakeCount;
	if (!_wakeTimer->isActive()) {
		_wakeTimer->start(250);
	}
}

void CognitiveLoop::step() {
	if (!_running || _stepActive) {
		return;
	}
	_stepActive = true;
	stepNow();
	_stepActive = false;
}

void CognitiveLoop::stepNow() {
	++_cycleCount;
	const auto traceId = QStringLiteral("cycle-%1-%2")
		.arg(_cycleCount)
		.arg(QDateTime::currentMSecsSinceEpoch());

	// 1. OBSERVE: Fetch native session state
	const auto self = StateBridge::Instance().currentSelf();

	// 2. COMPUTE: Record only observed Telegram state. The optional economy
	// engine has no live producer for its full vector and cannot supply priors.
	const int unreadCount = self.unreadTotal;

	QJsonObject computePayload;
	computePayload[QStringLiteral("cycle")] = static_cast<qint64>(_cycleCount);
	computePayload[QStringLiteral("has_user_identity")] = (self.userId != 0);
	computePayload[QStringLiteral("unread")] = unreadCount;

	if (!RecordBehavior(
		EventType::Computation,
		traceId,
		QStringLiteral("apo.cognitive.evaluator"),
		computePayload)) {
		return;
	}

	// 4. OBSERVE native dialogue context rather than interpreting the
	// whole-account unread badge as an addressable chat or a customer request.
	// A nonzero badge alone cannot authorize "compute.draft_reply": there
	// is no message context, recipient, actual draft or measurable cost.
	if (self.userId == 0) {
		// Even an anonymous native body must retain evidence of sensory
		// wakeups. It must NOT invent an account or authorize an action.
		// Keep the authenticated wake path below unchanged.
		if (_activeWakeCount > 0
				&& _activeWakeAccountUserId == 0
				&& _activeWakeAdmittedAtMs > 0
				&& _activeWakeStartedAtMs >= _activeWakeAdmittedAtMs) {
			QJsonObject wake;
			wake[QStringLiteral("kind")] = QStringLiteral("NATIVE_SENSORY_WAKE");
			wake[QStringLiteral("sources")] = QJsonArray::fromStringList(
				_activeWakeSources);
			wake[QStringLiteral("scope_kind")] = QStringLiteral("anonymous_profile");
			wake[QStringLiteral("profile_scope")] = QString::fromUtf8(
				QCryptographicHash::hash(
					(QStringLiteral("anonymous|") + cWorkingDir()).toUtf8(),
					QCryptographicHash::Sha256).toHex());
			wake[QStringLiteral("coalesced_events")] = _activeWakeCount;
			wake[QStringLiteral("admission_to_wake_ms")] = static_cast<qint64>(
				_activeWakeStartedAtMs - _activeWakeAdmittedAtMs);
			if (!_activeWakeSourceTraceId.isEmpty()) {
				wake[QStringLiteral("first_source_trace_id")] =
					_activeWakeSourceTraceId;
			}
			if (!RecordBehavior(EventType::Computation, traceId,
					QStringLiteral("apo.cognitive.sensory_wake"), wake,
					_activeWakeSourceTraceId)) {
				return;
			}
		}
		return; // No authenticated native session: no reflex dispatch.
	}
	const auto session = Core::App().activeAccount().maybeSession();
	if (!session || session->userId().bare != self.userId) {
		return;
	}
	const auto list = session->data().chatsList();
	constexpr auto kMaxInspected = 512;
	constexpr auto kTop = 5;
	struct RankedUnread {
		int listPosition = 0;
		int unread = 0;
		int unmuted = 0;
		int mentions = 0;
	};
	std::vector<RankedUnread> ranked;
	const auto &rows = list->indexed()->all();
	const auto limit = std::min(int(rows.size()), kMaxInspected);
	int knownRows = 0;
	int historyRows = 0;
	int userDialogs = 0;
	int basicGroups = 0;
	int supergroups = 0;
	int broadcastChannels = 0;
	int unclassifiedPeers = 0;
	int unreadDialogs = 0;
	int mentionedDialogs = 0;
	auto rowIterator = rows.cbegin();
	for (auto i = 0; i != limit; ++i, ++rowIterator) {
		const auto row = *rowIterator;
		if (!row->history()) {
			continue; // A folder is not an independent conversation.
		}
		++historyRows;
		const auto peer = row->history()->peer;
		if (peer->isUser()) {
			++userDialogs;
		} else if (peer->isChat()) {
			++basicGroups;
		} else if (peer->isChannel()) {
			if (peer->isMegagroup()) ++supergroups;
			else ++broadcastChannels;
		} else {
			++unclassifiedPeers;
		}
		const auto state = row->entry()->chatListUnreadState();
		if (!state.known) {
			continue; // Unknown is not zero unread.
		}
		++knownRows;
		const auto unread = std::max(state.messages, 0);
		const auto unmuted = std::max(unread - state.messagesMuted, 0);
		const auto mentions = std::max(state.mentions, 0);
		if (unread > 0) ++unreadDialogs;
		if (mentions > 0) ++mentionedDialogs;
		if (unread > 0 || mentions > 0) {
			ranked.push_back({ i, unread, unmuted, mentions });
		}
	}
	// Lexicographic priority uses measured Telegram values, not invented
	// business value: mentions, nonmuted unread, total unread, recency.
	std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
		if (a.mentions != b.mentions) return a.mentions > b.mentions;
		if (a.unmuted != b.unmuted) return a.unmuted > b.unmuted;
		if (a.unread != b.unread) return a.unread > b.unread;
		return a.listPosition < b.listPosition;
	});
	QJsonArray top;
	for (auto i = 0, count = std::min(int(ranked.size()), kTop);
			i != count; ++i) {
		const auto &item = ranked[i];
		QJsonObject one;
		one[QStringLiteral("priority_rank")] = i + 1;
		one[QStringLiteral("loaded_list_position")] = item.listPosition;
		one[QStringLiteral("unread_messages")] = item.unread;
		one[QStringLiteral("unmuted_messages")] = item.unmuted;
		one[QStringLiteral("mentions")] = item.mentions;
		top.append(one);
	}
	QJsonObject observation;
	observation[QStringLiteral("kind")] = QStringLiteral("NATIVE_UNREAD_PRIORITY_OBSERVATION");
	observation[QStringLiteral("source")] = QStringLiteral("Data::Session.chatsList");
	observation[QStringLiteral("source_loaded")] = list->loaded();
	observation[QStringLiteral("total_loaded_rows")] = int(rows.size());
	observation[QStringLiteral("inspected_rows")] = limit;
	observation[QStringLiteral("history_rows")] = historyRows;
	observation[QStringLiteral("known_unread_rows")] = knownRows;
	observation[QStringLiteral("unread_ranked_rows")] = int(ranked.size());
	observation[QStringLiteral("full_loaded_list_covered")] =
		list->loaded() && (limit == int(rows.size()));
	observation[QStringLiteral("top")] = top;
	observation[QStringLiteral("result")] = QStringLiteral("OBSERVED_ONLY");
	observation[QStringLiteral("native_dispatch")] = false;
	// No peer IDs, chat names or message text leave the native process.
	const auto snapshot = QJsonDocument(observation).toJson(QJsonDocument::Compact);
	auto rankingPersisted = true;
	if (_lastRankingAccount != self.userId
		|| _lastRankingEvidence != snapshot) {
		rankingPersisted = RecordBehavior(EventType::Computation,
			traceId,
			QStringLiteral("apo.cognitive.unread_ranking"),
			observation);
		if (rankingPersisted) {
			_lastRankingAccount = self.userId;
			_lastRankingEvidence = snapshot;
		}
	}
	// Bounded native read-only inspection can refresh and recover from a
	// rejected request, timeout or failed postcondition. Never send an
	// unrestricted message or treat a dispatch as observed success.
	const auto nowMs = QDateTime::currentMSecsSinceEpoch();
	if (_lastRemoteInspectionAccount != self.userId) {
		++_remoteInspectionGeneration; // Invalidate prior account callbacks.
		_lastRemoteInspectionAccount = self.userId;
		_lastRemoteInspectionAtMs = 0;
		_remoteInspectionInFlight = false;
		_lastRemoteInspectionSucceeded = false;
		_lastRemoteInspectionActionId.clear();
		_lastRemoteInspectionSourceTrace.clear();
		_lastRemoteInspectionAccountScope.clear();
	}
	constexpr auto kRetryAfterFailureMs = int64(60) * 1000;
	constexpr auto kRefreshAfterSuccessMs = int64(15) * 60 * 1000;
	constexpr auto kInFlightExpiryMs = int64(2) * 60 * 1000;
	const auto elapsedMs = _lastRemoteInspectionAtMs > 0
		? nowMs - _lastRemoteInspectionAtMs : int64(0);
	if (_remoteInspectionInFlight && elapsedMs >= kInFlightExpiryMs) {
		QJsonObject unknown;
		unknown[QStringLiteral("kind")] = QStringLiteral("REMOTE_INSPECTION_TIMEOUT");
		unknown[QStringLiteral("result")] = QStringLiteral("UNKNOWN_OUTCOME");
		unknown[QStringLiteral("elapsed_ms")] = static_cast<qint64>(elapsedMs);
		unknown[QStringLiteral("action_id")] = _lastRemoteInspectionActionId;
		unknown[QStringLiteral("source_trace_id")] = _lastRemoteInspectionSourceTrace;
		unknown[QStringLiteral("account_scope")] = _lastRemoteInspectionAccountScope;
		unknown[QStringLiteral("new_phase")] = QStringLiteral("UNKNOWN_OUTCOME");
		if (!RecordBehavior(EventType::Computation,
			_lastRemoteInspectionActionId.isEmpty() ? traceId : _lastRemoteInspectionActionId,
			QStringLiteral("apo.cognitive.remote_inspection"), unknown,
			_lastRemoteInspectionSourceTrace)) {
			return; // No fresh dispatch without durable timeout evidence.
		}
		_remoteInspectionInFlight = false;
		_lastRemoteInspectionSucceeded = false;
		_lastRemoteInspectionActionId.clear();
		_lastRemoteInspectionSourceTrace.clear();
		_lastRemoteInspectionAccountScope.clear();
		++_remoteInspectionGeneration;
	}
	const auto revisitMs = _lastRemoteInspectionSucceeded
		? kRefreshAfterSuccessMs : kRetryAfterFailureMs;
	const auto inspectionDue = unreadCount > 0 && !_remoteInspectionInFlight
		&& (_lastRemoteInspectionAtMs == 0
			|| (elapsedMs >= 0 && elapsedMs >= revisitMs));

	// Native observations define the Ω state for THIS read-only domain:
	// C = observed inspected-row capacity fraction (limit / 512);
	// R = observed unknown-unread fraction (measurement uncertainty);
	// E = observed known-unread fraction; B = fully loaded/covered boundary.
	// These are measured Telegram projections, not economic risk estimates
	// or the sealed baseline priors. Never substitute missing measurements.
	const auto accountScope = QString::fromUtf8(QCryptographicHash::hash(
		(QString::number(self.userId) + QStringLiteral("|")
			+ cWorkingDir()).toUtf8(),
		QCryptographicHash::Sha256).toHex());
	if (_activeWakeCount > 0
			&& _activeWakeAccountUserId == self.userId
			&& _activeWakeAdmittedAtMs > 0
			&& _activeWakeStartedAtMs >= _activeWakeAdmittedAtMs) {
		QJsonObject wake;
		wake[QStringLiteral("kind")] = QStringLiteral("NATIVE_SENSORY_WAKE");
		wake[QStringLiteral("sources")] = QJsonArray::fromStringList(
			_activeWakeSources);
		wake[QStringLiteral("account_scope")] = accountScope;
		wake[QStringLiteral("coalesced_events")] = _activeWakeCount;
		wake[QStringLiteral("admission_to_wake_ms")] = static_cast<qint64>(
			_activeWakeStartedAtMs - _activeWakeAdmittedAtMs);
		if (!_activeWakeSourceTraceId.isEmpty()) {
			wake[QStringLiteral("first_source_trace_id")] =
				_activeWakeSourceTraceId;
		}
		if (!RecordBehavior(EventType::Computation, traceId,
				QStringLiteral("apo.cognitive.sensory_wake"), wake,
				_activeWakeSourceTraceId)) {
			return;
		}
	}
	// Feed the existing StateBridge with distinct, measured native body types.
	// This is observation only: no new action policy or convergence assumption.
	NativeBodyProjection body;
	body.accountUserId = self.userId;
	body.accountScope = accountScope;
	body.sourceTraceId = traceId;
	body.measuredAtMs = nowMs;
	body.totalLoadedRows = int(rows.size());
	body.inspectedRows = limit;
	body.historyRows = historyRows;
	body.userDialogs = userDialogs;
	body.basicGroups = basicGroups;
	body.supergroups = supergroups;
	body.broadcastChannels = broadcastChannels;
	body.unclassifiedPeers = unclassifiedPeers;
	body.knownUnreadRows = knownRows;
	body.unreadDialogs = unreadDialogs;
	body.mentionedDialogs = mentionedDialogs;
	body.sourceLoaded = list->loaded();
	body.complete = list->loaded() && limit == int(rows.size())
		&& unclassifiedPeers == 0;
	if (!StateBridge::Instance().updateNativeBodyProjection(body)) {
		return; // Never compute an action from an unpersisted body state.
	}
	// Restore unresolved native dispatches from the already verified RAW chain.
	// These are recovery observations, NOT inferred RPC failures or successes.
	if (_lastRecoveryAccountScope != accountScope) {
		const auto unresolved = BehaviorLedger::Instance().unresolvedNativeReads(accountScope);
		for (const auto &pending : unresolved) {
			QJsonObject recovery;
			recovery[QStringLiteral("kind")] = QStringLiteral("NATIVE_READ_RECOVERY_PENDING");
			recovery[QStringLiteral("action_id")] = pending.actionId;
			recovery[QStringLiteral("account_scope")] = accountScope;
			recovery[QStringLiteral("source_trace_id")] = pending.sourceTraceId;
			recovery[QStringLiteral("request_id")] = pending.requestId;
			recovery[QStringLiteral("previous_phase")] = pending.phase;
			recovery[QStringLiteral("result")] = QStringLiteral("OUTCOME_UNRESOLVED");
			recovery[QStringLiteral("new_phase")] = QStringLiteral("RECONCILING");
			if (!RecordBehavior(EventType::Computation, pending.actionId,
				QStringLiteral("apo.cognitive.recovery"), recovery,
				pending.sourceTraceId)) {
				return; // Unverifiable recovery cannot authorize a new transition.
			}
		}
		// Native writes must not be retried based on a missing RPC callback.
		// Surface persisted random_id and request identity for reconciliation.
		for (const auto &pending :
				BehaviorLedger::Instance().unresolvedNativeWrites(accountScope)) {
			QJsonObject recovery;
			recovery[QStringLiteral("kind")] =
				QStringLiteral("NATIVE_WRITE_RECOVERY_PENDING");
			recovery[QStringLiteral("action_id")] = pending.actionId;
			recovery[QStringLiteral("account_scope")] = accountScope;
			recovery[QStringLiteral("request_id")] = pending.requestId;
			recovery[QStringLiteral("random_id_hex")] = pending.randomIdHex;
			recovery[QStringLiteral("previous_phase")] = pending.phase;
			recovery[QStringLiteral("server_acknowledged")] =
				pending.serverAcknowledged;
			recovery[QStringLiteral("native_postcondition_verified")] =
				pending.postconditionVerified;
			recovery[QStringLiteral("result")] =
				QStringLiteral("OUTCOME_UNRESOLVED_NO_AUTOMATIC_RETRY");
			recovery[QStringLiteral("new_phase")] =
				QStringLiteral("RECONCILING");
			if (!RecordBehavior(EventType::Computation, pending.actionId,
				QStringLiteral("apo.cognitive.recovery"), recovery)) {
				return;
			}
		}
		_lastRecoveryAccountScope = accountScope;
	}
	const auto measurementComplete = list->loaded() && limit > 0
		&& knownRows > 0 && limit == int(rows.size());
	const auto previous = measurementComplete
		? BehaviorLedger::Instance().latestOmegaState(accountScope)
		: std::nullopt;
	ApoStateQuad measured;
	if (measurementComplete) {
		measured = {
			double(limit) / double(kMaxInspected),
			double(limit - knownRows) / double(limit),
			double(knownRows) / double(limit),
			1.0
		};
	}
	QJsonObject measuredPayload;
	measuredPayload[QStringLiteral("kind")] = QStringLiteral("NATIVE_DIALOGS_STATE");
	measuredPayload[QStringLiteral("source")] = QStringLiteral("Data::Session.chatsList");
	measuredPayload[QStringLiteral("account_scope")] = accountScope;
	measuredPayload[QStringLiteral("measurement_complete")] = measurementComplete;
	measuredPayload[QStringLiteral("measured_at_ms")] = static_cast<qint64>(nowMs);
	measuredPayload[QStringLiteral("inspected_rows")] = limit;
	measuredPayload[QStringLiteral("total_loaded_rows")] = int(rows.size());
	measuredPayload[QStringLiteral("known_rows")] = knownRows;
	measuredPayload[QStringLiteral("source_loaded")] = list->loaded();
	measuredPayload[QStringLiteral("state")] = measurementComplete
		? measured.toJson() : QJsonObject();
	if (!RecordBehavior(EventType::StateReduce, traceId,
		QStringLiteral("apo.cognitive.omega_measurement"), measuredPayload)) {
		return; // No state transition without a durable observation.
	}
	if (inspectionDue && (!measurementComplete || !previous
		|| previous->timestampMs >= nowMs)) {
		QJsonObject wait;
		wait[QStringLiteral("decision")] = QStringLiteral("WAIT_FOR_DATA");
		wait[QStringLiteral("reason")] = !measurementComplete
			? QStringLiteral("NATIVE_STATE_INCOMPLETE")
			: (!previous ? QStringLiteral("PREVIOUS_OBSERVATION_MISSING")
				: QStringLiteral("PREVIOUS_OBSERVATION_NOT_OLDER"));
		wait[QStringLiteral("account_scope")] = accountScope;
		wait[QStringLiteral("current_state_source")] = traceId;
		RecordBehavior(EventType::Decision, traceId,
			QStringLiteral("apo.cognitive.omega_gate"), wait);
	}
	if (rankingPersisted && measurementComplete && previous
		&& previous->timestampMs < nowMs
		&& !ranked.empty() && inspectionDue) {
		// S0 and S1 came from distinct native observation cycles.
		// The ledger verifies their hash-chain-backed account scope and order.
		const auto candidate = measured;
		QJsonObject params;
		params[QStringLiteral("source_trace_id")] = traceId;
		params[QStringLiteral("apo_account_scope")] = accountScope;
		params[QStringLiteral("apo_state_domain")] =
			QStringLiteral("dialogs.inspect_server");
		params[QStringLiteral("apo_previous_state")] = previous->state;
		params[QStringLiteral("apo_previous_state_source")] = previous->traceId;
		params[QStringLiteral("apo_current_state_source")] = traceId;
		params[QStringLiteral("loaded_rows")] = int(rows.size());
		params[QStringLiteral("known_rows")] = knownRows;
		params[QStringLiteral("expected_result")] =
			QStringLiteral("native getDialogs response with local chat projection match");
		params[QStringLiteral("state_model_provenance")] =
			QStringLiteral("C:inspected_rows_over_512;"
				"R:unknown_unread_rows_over_inspected;"
				"E:known_unread_rows_over_inspected;"
				"B:loaded_and_full_coverage;"
				"source:Data::Session.chatsList");
		QJsonObject decision;
		decision[QStringLiteral("decision")] =
			QStringLiteral("SUBMIT_NATIVE_READ_INSPECTION");
		decision[QStringLiteral("account_scope")] = accountScope;
		decision[QStringLiteral("source_trace_id")] = traceId;
		decision[QStringLiteral("state_domain")] =
			QStringLiteral("dialogs.inspect_server");
		if (!RecordBehavior(EventType::Decision, traceId,
			QStringLiteral("apo.cognitive.omega_gate"), decision)) {
			return;
		}
		_lastRemoteInspectionAtMs = nowMs;
		_lastRemoteInspectionSucceeded = false;
		_remoteInspectionInFlight = true;
		_lastRemoteInspectionSourceTrace = traceId;
		_lastRemoteInspectionAccountScope = accountScope;
		_lastRemoteInspectionActionId.clear();
		const auto attemptGeneration = ++_remoteInspectionGeneration;
		const auto accountUserId = self.userId;
		const auto attemptActionId = std::make_shared<QString>();
		ActionExecutor::Instance().submitAction(
			QStringLiteral("dialogs.inspect_server"),
			QStringLiteral("native_main_dialogs"),
			params,
			QStringLiteral("Verify local unread context against native Telegram server"),
			candidate,
			[this, accountUserId, accountScope, attemptGeneration, traceId, attemptActionId](bool success) {
				// Never promote an obsolete attempt to success for the current account/cycle.
				// Still preserve the arrival as a correlated, non-terminal observation.
				if (!_running || _lastRemoteInspectionAccount != accountUserId
					|| _remoteInspectionGeneration != attemptGeneration) {
					QJsonObject late;
					late[QStringLiteral("action_id")] = *attemptActionId;
					late[QStringLiteral("source_trace_id")] = traceId;
					late[QStringLiteral("account_scope")] = accountScope;
					late[QStringLiteral("result")] = QStringLiteral("LATE_CALLBACK_IGNORED");
					late[QStringLiteral("verified")] = success;
					late[QStringLiteral("new_phase")] = QStringLiteral("RECONCILING");
					RecordBehavior(EventType::ObservedResult,
						attemptActionId->isEmpty() ? traceId : *attemptActionId,
						QStringLiteral("apo.cognitive.remote_inspection"), late, traceId);
					return;
				}
				_remoteInspectionInFlight = false;
				_lastRemoteInspectionSucceeded = success;
				QJsonObject result;
				result[QStringLiteral("kind")] = QStringLiteral("NATIVE_INSPECTION_CALLBACK");
				result[QStringLiteral("action_id")] = *attemptActionId;
				result[QStringLiteral("source_trace_id")] = traceId;
				result[QStringLiteral("account_scope")] = accountScope;
				result[QStringLiteral("verified")] = success;
				result[QStringLiteral("result")] = success
					? QStringLiteral("VERIFIED_LOCAL_POSTCONDITION")
					: QStringLiteral("NOT_VERIFIED");
				RecordBehavior(EventType::Computation,
					attemptActionId->isEmpty() ? traceId : *attemptActionId,
					QStringLiteral("apo.cognitive.remote_inspection"), result, traceId);
				_lastRemoteInspectionActionId.clear();
				_lastRemoteInspectionSourceTrace.clear();
				_lastRemoteInspectionAccountScope.clear();
			},
			attemptActionId.get());
		if (_remoteInspectionInFlight && _remoteInspectionGeneration == attemptGeneration) {
			_lastRemoteInspectionActionId = *attemptActionId;
		}
	}
}

} // namespace apo
