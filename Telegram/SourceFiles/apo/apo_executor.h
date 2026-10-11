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
#include <functional>

namespace apo {

struct PendingAction {
	QString actionId;
	QString actionType;
	QString target;
	QJsonObject parameters;
	QString rationale;
	int64 proposedTimeMs = 0;
	std::function<void(bool approved)> callback;
};

class ActionExecutor final {
public:
	static ActionExecutor &Instance();

	// Submit an action for evaluation and possible execution
	void submitAction(
		const QString &actionType,
		const QString &target,
		const QJsonObject &parameters,
		const QString &rationale,
		std::function<void(bool success)> onComplete);

	// User approved a pending action via UI
	void approveAction(const QString &actionId);

	// User denied a pending action via UI
	void rejectAction(const QString &actionId, const QString &reason);

	[[nodiscard]] std::vector<PendingAction> listPendingActions() const;

private:
	ActionExecutor() = default;
	~ActionExecutor() = default;

	ActionExecutor(const ActionExecutor &) = delete;
	ActionExecutor &operator=(const ActionExecutor &) = delete;

	void executeInternal(const PendingAction &action);

	mutable QMutex _mutex;
	QMap<QString, PendingAction> _pendingActions;
};

} // namespace apo
