/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "apo/apo_common.h"
#include <QtCore/QMap>
#include <QtCore/QMutex>
#include <QtCore/QString>

namespace apo {

class AuthorityManager final {
public:
	static AuthorityManager &Instance();

	void setGlobalPolicy(AuthorityLevel level);
	[[nodiscard]] AuthorityLevel globalPolicy() const;

	void setActionAuthority(const QString &actionName, AuthorityLevel level);
	[[nodiscard]] AuthorityLevel getActionAuthority(const QString &actionName) const;

	// Validates whether an action can execute without user prompt.
	// Returns true if AutoExecuteAllowed.
	// Returns false if RequireApproval (must present UI modal) or Deny.
	[[nodiscard]] bool canAutoExecute(const QString &actionName) const;

	// Checks if system is allowed to even suggest or compute this action.
	[[nodiscard]] bool isActionPermitted(const QString &actionName) const;

private:
	AuthorityManager();
	~AuthorityManager() = default;

	AuthorityManager(const AuthorityManager &) = delete;
	AuthorityManager &operator=(const AuthorityManager &) = delete;

	mutable QMutex _mutex;
	AuthorityLevel _globalPolicy = AuthorityLevel::RequireApproval;
	QMap<QString, AuthorityLevel> _actionPolicies;
};

} // namespace apo
