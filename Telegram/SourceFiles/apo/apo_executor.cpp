/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "apo/apo_executor.h"
#include "apo/apo_authority.h"
#include "apo/apo_behavior_ledger.h"
#include <QtCore/QUuid>

namespace apo {

ActionExecutor &ActionExecutor::Instance() {
	static ActionExecutor instance;
	return instance;
}

void ActionExecutor::submitAction(
	const QString &actionType,
	const QString &target,
	const QJsonObject &parameters,
	const QString &rationale,
	std::function<void(bool success)> onComplete) {
	const auto actionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	const auto nowMs = QDateTime::currentMSecsSinceEpoch();

	PendingAction action;
	action.actionId = actionId;
	action.actionType = actionType;
	action.target = target;
	action.parameters = parameters;
	action.rationale = rationale;
	action.phase = ActionPhase::Planned;
	action.proposedTimeMs = nowMs;
	action.callback = onComplete;

	// Phase 1: PLANNED - Log decision in Behavior Ledger
	QJsonObject plannedPayload;
	plannedPayload[QStringLiteral("action_id")] = actionId;
	plannedPayload[QStringLiteral("action_type")] = actionType;
	plannedPayload[QStringLiteral("target")] = target;
	plannedPayload[QStringLiteral("parameter_count")] = parameters.size();
	plannedPayload[QStringLiteral("parameters_redacted")] = true;
	plannedPayload[QStringLiteral("has_rationale")] = !rationale.isEmpty();
	plannedPayload[QStringLiteral("phase")] = ActionPhaseToString(ActionPhase::Planned);

	if (!RecordBehavior(
		EventType::Decision,
		actionId,
		QStringLiteral("apo.agent_core"),
		plannedPayload)) {
		if (onComplete) {
			onComplete(false);
		}
		return;
	}

	// Authority check: Capability != Authority
	const auto authority = AuthorityManager::Instance().getActionAuthority(actionType);

	// Phase 2: VERIFIED
	QJsonObject verifyPayload;
	verifyPayload[QStringLiteral("action_id")] = actionId;
	verifyPayload[QStringLiteral("action_type")] = actionType;
	verifyPayload[QStringLiteral("authority_level")] = AuthorityLevelToString(authority);

	if (authority == AuthorityLevel::AutoExecuteAllowed) {
		verifyPayload[QStringLiteral("result")] = QStringLiteral("ALLOWED_AUTO");
		verifyPayload[QStringLiteral("phase")] = ActionPhaseToString(ActionPhase::Verified);
		RecordBehavior(EventType::ApoVerify, actionId, QStringLiteral("apo.authority.gate"), verifyPayload);

		action.phase = ActionPhase::Verified;
		executeInternal(action);
	} else if (authority == AuthorityLevel::RequireApproval) {
		verifyPayload[QStringLiteral("result")] = QStringLiteral("HELD_FOR_USER_APPROVAL");
		verifyPayload[QStringLiteral("phase")] = QStringLiteral("PENDING_USER_APPROVAL");
		RecordBehavior(EventType::ApoVerify, actionId, QStringLiteral("apo.authority.gate"), verifyPayload);

		QMutexLocker locker(&_mutex);
		_pendingActions[actionId] = action;
	} else {
		verifyPayload[QStringLiteral("result")] = QStringLiteral("DENIED_BY_POLICY");
		verifyPayload[QStringLiteral("phase")] = QStringLiteral("REJECTED");
		RecordBehavior(EventType::ApoVerify, actionId, QStringLiteral("apo.authority.gate"), verifyPayload);
		if (onComplete) {
			onComplete(false);
		}
	}
}

void ActionExecutor::approveAction(const QString &actionId) {
	PendingAction action;
	{
		QMutexLocker locker(&_mutex);
		const auto it = _pendingActions.find(actionId);
		if (it == _pendingActions.end()) {
			return;
		}
		action = it.value();
		_pendingActions.erase(it);
	}

	QJsonObject verifyPayload;
	verifyPayload[QStringLiteral("action_id")] = actionId;
	// Authorization may have changed while this action awaited approval.
	const auto currentAuthority = AuthorityManager::Instance().getActionAuthority(action.actionType);
	if (currentAuthority != AuthorityLevel::RequireApproval
		&& currentAuthority != AuthorityLevel::AutoExecuteAllowed) {
		verifyPayload[QStringLiteral("result")] = QStringLiteral("APPROVAL_REVOKED_BY_POLICY");
		RecordBehavior(EventType::ApoVerify, actionId, QStringLiteral("apo.authority.gate"), verifyPayload);
		if (action.callback) {
			action.callback(false);
		}
		return;
	}
	verifyPayload[QStringLiteral("result")] = QStringLiteral("USER_EXPLICIT_APPROVAL");
	verifyPayload[QStringLiteral("phase")] = ActionPhaseToString(ActionPhase::Verified);
	RecordBehavior(EventType::ApoVerify, actionId, QStringLiteral("ui.approval_dialog"), verifyPayload);

	action.phase = ActionPhase::Verified;
	executeInternal(action);
}

void ActionExecutor::rejectAction(const QString &actionId, const QString &reason) {
	PendingAction action;
	{
		QMutexLocker locker(&_mutex);
		const auto it = _pendingActions.find(actionId);
		if (it == _pendingActions.end()) {
			return;
		}
		action = it.value();
		_pendingActions.erase(it);
	}

	QJsonObject verifyPayload;
	verifyPayload[QStringLiteral("action_id")] = actionId;
	verifyPayload[QStringLiteral("result")] = QStringLiteral("USER_EXPLICIT_REJECTION");
	verifyPayload[QStringLiteral("reason")] = reason;
	verifyPayload[QStringLiteral("phase")] = QStringLiteral("CLOSED_REJECTED");
	RecordBehavior(EventType::ApoVerify, actionId, QStringLiteral("ui.approval_dialog"), verifyPayload);

	if (action.callback) {
		action.callback(false);
	}
}

void ActionExecutor::advancePhase(const QString &actionId, ActionPhase newPhase) {
	QJsonObject payload;
	payload[QStringLiteral("action_id")] = actionId;
	payload[QStringLiteral("new_phase")] = ActionPhaseToString(newPhase);

	EventType evtType = EventType::Execution;
	if (newPhase == ActionPhase::Acknowledged || newPhase == ActionPhase::Observed) {
		evtType = EventType::ObservedResult;
	} else if (newPhase == ActionPhase::Closed) {
		evtType = EventType::StateReduce;
	}

	RecordBehavior(
		evtType,
		actionId,
		QStringLiteral("apo.executor.lifecycle"),
		payload);
}

void ActionExecutor::executeInternal(const PendingAction &action) {
	// A VERIFIED intent is not a SENT operation. Until an actual native
	// Telegram dispatch adapter is connected, fail closed and never claim
	// ACKNOWLEDGED / OBSERVED / CLOSED or invoke a success callback.
	QJsonObject execPayload;
	execPayload[QStringLiteral("action_id")] = action.actionId;
	execPayload[QStringLiteral("action_type")] = action.actionType;
	execPayload[QStringLiteral("target")] = action.target;
	execPayload[QStringLiteral("parameter_count")] = action.parameters.size();
	execPayload[QStringLiteral("parameters_redacted")] = true;
	execPayload[QStringLiteral("result")] = QStringLiteral("BLOCKED_NO_NATIVE_DISPATCH");
	execPayload[QStringLiteral("phase")] = ActionPhaseToString(action.phase);

	RecordBehavior(
		EventType::Execution,
		action.actionId,
		QStringLiteral("apo.executor"),
		execPayload);

	if (action.callback) {
		action.callback(false);
	}
}

std::vector<PendingAction> ActionExecutor::listPendingActions() const {
	QMutexLocker locker(&_mutex);
	std::vector<PendingAction> result;
	result.reserve(_pendingActions.size());
	for (const auto &item : _pendingActions) {
		result.push_back(item);
	}
	return result;
}

} // namespace apo
