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
	action.proposedTimeMs = nowMs;
	action.callback = onComplete;

	// Decision logging
	QJsonObject decisionPayload;
	decisionPayload[QStringLiteral("action_id")] = actionId;
	decisionPayload[QStringLiteral("action_type")] = actionType;
	decisionPayload[QStringLiteral("target")] = target;
	decisionPayload[QStringLiteral("parameters")] = parameters;
	decisionPayload[QStringLiteral("rationale")] = rationale;

	RecordBehavior(
		EventType::Decision,
		actionId,
		QStringLiteral("apo.agent_core"),
		decisionPayload);

	// Authority check: Capability != Authority
	const auto authority = AuthorityManager::Instance().getActionAuthority(actionType);

	QJsonObject verifyPayload;
	verifyPayload[QStringLiteral("action_id")] = actionId;
	verifyPayload[QStringLiteral("action_type")] = actionType;
	verifyPayload[QStringLiteral("authority_level")] = AuthorityLevelToString(authority);

	if (authority == AuthorityLevel::AutoExecuteAllowed) {
		verifyPayload[QStringLiteral("result")] = QStringLiteral("ALLOWED_AUTO");
		RecordBehavior(EventType::ApoVerify, actionId, QStringLiteral("apo.authority.gate"), verifyPayload);
		executeInternal(action);
	} else if (authority == AuthorityLevel::RequireApproval) {
		verifyPayload[QStringLiteral("result")] = QStringLiteral("HELD_FOR_USER_APPROVAL");
		RecordBehavior(EventType::ApoVerify, actionId, QStringLiteral("apo.authority.gate"), verifyPayload);

		QMutexLocker locker(&_mutex);
		_pendingActions[actionId] = action;
	} else {
		verifyPayload[QStringLiteral("result")] = QStringLiteral("DENIED_BY_POLICY");
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
	verifyPayload[QStringLiteral("result")] = QStringLiteral("USER_EXPLICIT_APPROVAL");
	RecordBehavior(EventType::ApoVerify, actionId, QStringLiteral("ui.approval_dialog"), verifyPayload);

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
	RecordBehavior(EventType::ApoVerify, actionId, QStringLiteral("ui.approval_dialog"), verifyPayload);

	if (action.callback) {
		action.callback(false);
	}
}

void ActionExecutor::executeInternal(const PendingAction &action) {
	QJsonObject execPayload;
	execPayload[QStringLiteral("action_id")] = action.actionId;
	execPayload[QStringLiteral("action_type")] = action.actionType;
	execPayload[QStringLiteral("target")] = action.target;
	execPayload[QStringLiteral("parameters")] = action.parameters;

	RecordBehavior(
		EventType::Execution,
		action.actionId,
		QStringLiteral("apo.executor"),
		execPayload);

	// Execution complete callback
	if (action.callback) {
		action.callback(true);
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
