/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "apo/apo_authority.h"
#include "apo/apo_behavior_ledger.h"

namespace apo {

AuthorityManager &AuthorityManager::Instance() {
	static AuthorityManager instance;
	return instance;
}

AuthorityManager::AuthorityManager() {
	// Whitelisted safe passive actions
	_actionPolicies[QStringLiteral("channel.scan_public")] = AuthorityLevel::AutoExecuteAllowed;
	_actionPolicies[QStringLiteral("messages.search_local")] = AuthorityLevel::AutoExecuteAllowed;
	_actionPolicies[QStringLiteral("compute.opportunity_ranking")] = AuthorityLevel::AutoExecuteAllowed;
	_actionPolicies[QStringLiteral("compute.draft_reply")] = AuthorityLevel::AutoExecuteAllowed;

	// High-stakes actions strictly require user approval by default
	_actionPolicies[QStringLiteral("messages.send_text")] = AuthorityLevel::RequireApproval;
	_actionPolicies[QStringLiteral("messages.delete")] = AuthorityLevel::RequireApproval;
	_actionPolicies[QStringLiteral("wallet.transfer_ton")] = AuthorityLevel::RequireApproval;
	_actionPolicies[QStringLiteral("payments.send_stars")] = AuthorityLevel::RequireApproval;
	_actionPolicies[QStringLiteral("channels.join")] = AuthorityLevel::RequireApproval;
	_actionPolicies[QStringLiteral("channels.leave")] = AuthorityLevel::RequireApproval;
}

void AuthorityManager::setGlobalPolicy(AuthorityLevel level) {
	QMutexLocker locker(&_mutex);
	_globalPolicy = level;

	QJsonObject payload;
	payload[QStringLiteral("new_global_policy")] = AuthorityLevelToString(level);
	RecordBehavior(EventType::StateReduce, QString(), QStringLiteral("apo.authority"), payload);
}

AuthorityLevel AuthorityManager::globalPolicy() const {
	QMutexLocker locker(&_mutex);
	return _globalPolicy;
}

void AuthorityManager::setActionAuthority(const QString &actionName, AuthorityLevel level) {
	QMutexLocker locker(&_mutex);
	_actionPolicies[actionName] = level;

	QJsonObject payload;
	payload[QStringLiteral("action")] = actionName;
	payload[QStringLiteral("level")] = AuthorityLevelToString(level);
	RecordBehavior(EventType::StateReduce, QString(), QStringLiteral("apo.authority"), payload);
}

AuthorityLevel AuthorityManager::getActionAuthority(const QString &actionName) const {
	QMutexLocker locker(&_mutex);
	if (_globalPolicy == AuthorityLevel::Deny) {
		return AuthorityLevel::Deny;
	}
	const auto it = _actionPolicies.constFind(actionName);
	if (it != _actionPolicies.constEnd()) {
		return it.value();
	}
	return _globalPolicy;
}

bool AuthorityManager::canAutoExecute(const QString &actionName) const {
	const auto level = getActionAuthority(actionName);
	return (level == AuthorityLevel::AutoExecuteAllowed);
}

bool AuthorityManager::isActionPermitted(const QString &actionName) const {
	const auto level = getActionAuthority(actionName);
	return (level != AuthorityLevel::Deny);
}

} // namespace apo
