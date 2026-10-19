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

// Contract for an APΩ operation in the native Telegram process.
// Transition evidence is issued by the owner of each native operation;
// stringifying an enum must never itself constitute an ACK or success.
enum class ActionPhase {
	Observed,
	Planned,
	Verified,
	Authorized,
	Dispatched,
	Acknowledged,
	StateConfirmed,
	Closed,
	Rejected,
	Denied,
	PendingAuthority,
	Failed,
	TimedOut,
	UnknownOutcome,
	Reconciling
};

// Close a read-only inspection only if every decoded server dialog
// is present in the loaded projection for the same authenticated session.
[[nodiscard]] constexpr bool NativeDialogsPostconditionConfirmed(
    bool projectionLoaded, int serverDialogs, int matchedLocalDialogs) {
    return projectionLoaded && serverDialogs > 0
        && matchedLocalDialogs == serverDialogs;
}

[[nodiscard]] inline QString ActionPhaseToString(ActionPhase phase) {
	switch (phase) {
	case ActionPhase::Observed: return QStringLiteral("OBSERVED");
	case ActionPhase::Planned: return QStringLiteral("PLANNED");
	case ActionPhase::Verified: return QStringLiteral("VERIFIED");
	case ActionPhase::Authorized: return QStringLiteral("AUTHORIZED");
	case ActionPhase::Dispatched: return QStringLiteral("DISPATCHED");
	case ActionPhase::Acknowledged: return QStringLiteral("ACKNOWLEDGED");
	case ActionPhase::StateConfirmed: return QStringLiteral("STATE_CONFIRMED");
	case ActionPhase::Closed: return QStringLiteral("CLOSED");
	case ActionPhase::Rejected: return QStringLiteral("REJECTED");
	case ActionPhase::Denied: return QStringLiteral("DENIED");
	case ActionPhase::PendingAuthority: return QStringLiteral("PENDING_AUTHORITY");
	case ActionPhase::Failed: return QStringLiteral("FAILED");
	case ActionPhase::TimedOut: return QStringLiteral("TIMED_OUT");
	case ActionPhase::UnknownOutcome: return QStringLiteral("UNKNOWN_OUTCOME");
	case ActionPhase::Reconciling: return QStringLiteral("RECONCILING");
	}
	return QStringLiteral("UNKNOWN");
}

struct PendingAction {
	QString actionId;
	QString actionType;
	QString target;
	QJsonObject parameters;
	QString rationale;
	int64 initiatingAccountUserId = 0; // Native account binding; never logged.
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

	mutable QMutex _mutex;
	QMap<QString, PendingAction> _pendingActions;
};

} // namespace apo
