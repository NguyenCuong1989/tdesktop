/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "apo/apo_common.h"
#include "apo/apo_verifier.h"
#include <QtCore/QMap>
#include <QtCore/QMutex>
#include <QtCore/QString>
#include <functional>

namespace apo {

// Standardized 6-Phase Action Lifecycle:
// PLANNED -> VERIFIED -> SENT -> ACKNOWLEDGED -> OBSERVED -> CLOSED
enum class ActionPhase {
	Planned,
	Verified,
	Sent,
	Acknowledged,
	Observed,
	Closed
};

[[nodiscard]] inline QString ActionPhaseToString(ActionPhase phase) {
	switch (phase) {
	case ActionPhase::Planned: return QStringLiteral("PLANNED");
	case ActionPhase::Verified: return QStringLiteral("VERIFIED");
	case ActionPhase::Sent: return QStringLiteral("SENT");
	case ActionPhase::Acknowledged: return QStringLiteral("ACKNOWLEDGED");
	case ActionPhase::Observed: return QStringLiteral("OBSERVED");
	case ActionPhase::Closed: return QStringLiteral("CLOSED");
	}
	return QStringLiteral("UNKNOWN");
}

struct PendingAction {
	QString actionId;
	QString actionType;
	QString target;
	QJsonObject parameters;
	QString rationale;
	ActionPhase phase = ActionPhase::Planned;
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
		const ApoStateQuad &candidateState,
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

	// Private until a native dispatch adapter can verify each transition against
	// a matching Telegram request/response/update, not an intent label.
	void advancePhase(const QString &actionId, ActionPhase newPhase);

	mutable QMutex _mutex;
	QMap<QString, PendingAction> _pendingActions;
};

} // namespace apo
