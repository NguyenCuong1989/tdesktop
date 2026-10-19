/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "apo/apo_executor.h"
#include "apo/apo_authority.h"
#include "apo/apo_verifier.h"
#include "apo/apo_behavior_ledger.h"
#include "core/application.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "data/data_session.h"
#include "dialogs/dialogs_main_list.h"
#include "history/history.h"
#include "window/window_controller.h"
#include "ui/boxes/confirm_box.h"
#include <QtCore/QUuid>
#include <memory>

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
	const ApoStateQuad &candidateState,
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
	if (Core::App().someSessionExists()) {
		if (const auto session = Core::App().activeAccount().maybeSession()) {
			action.initiatingAccountUserId = session->userId().bare;
		}
	}

	// Phase 1: PLANNED - Log decision in Behavior Ledger
	QJsonObject plannedPayload;
	plannedPayload[QStringLiteral("action_id")] = actionId;
	plannedPayload[QStringLiteral("action_type")] = actionType;
	plannedPayload[QStringLiteral("target_present")] = !target.isEmpty();
	plannedPayload[QStringLiteral("parameter_count")] = parameters.size();
	plannedPayload[QStringLiteral("parameters_redacted")] = true;
	plannedPayload[QStringLiteral("has_rationale")] = !rationale.isEmpty();
	plannedPayload[QStringLiteral("phase")] = ActionPhaseToString(ActionPhase::Planned);
	const auto sourceTraceId =
		parameters.value(QStringLiteral("source_trace_id")).toString();
	if (!sourceTraceId.isEmpty()) {
		plannedPayload[QStringLiteral("source_trace_id")] = sourceTraceId;
	}
	plannedPayload[QStringLiteral("previous_state_source")] =
		parameters.value(QStringLiteral("apo_previous_state_source")).toString();
	plannedPayload[QStringLiteral("current_state_source")] =
		parameters.value(QStringLiteral("apo_current_state_source")).toString();
	plannedPayload[QStringLiteral("account_scope")] =
		parameters.value(QStringLiteral("apo_account_scope")).toString();
	// Values in the gate must be inspectable, and measurement provenance
	// must be distinguished from model assumptions.
	plannedPayload[QStringLiteral("candidate_C")] = candidateState.C;
	plannedPayload[QStringLiteral("candidate_R")] = candidateState.R;
	plannedPayload[QStringLiteral("candidate_E")] = candidateState.E;
	plannedPayload[QStringLiteral("candidate_B")] = candidateState.B;
	plannedPayload[QStringLiteral("state_model_provenance")] =
		parameters.value(QStringLiteral("state_model_provenance")).toString();
	plannedPayload[QStringLiteral("expected_result")] =
		parameters.value(QStringLiteral("expected_result")).toString();

	if (!RecordBehavior(
		EventType::Decision,
		actionId,
		QStringLiteral("apo.agent_core"),
		plannedPayload,
		sourceTraceId)) {
		if (onComplete) {
			onComplete(false);
		}
		return;
	}

	// Mandatory verifier at the execution boundary: callers cannot bypass
	// Omega(P) simply by submitting an action directly to the executor.
	const auto verification = ApoVerifier::Instance().verifyAction(
		actionType,
		target,
		parameters,
		candidateState);
	if (!verification.passed) {
		QJsonObject rejected;
		rejected[QStringLiteral("action_id")] = actionId;
		rejected[QStringLiteral("result")] = QStringLiteral("REJECTED_BY_APO_VERIFIER");
		rejected[QStringLiteral("new_phase")] = ActionPhaseToString(ActionPhase::Rejected);
		rejected[QStringLiteral("reason")] = verification.rationale;
		RecordBehavior(EventType::ApoVerify, actionId, QStringLiteral("apo.executor.gate"), rejected);
		if (onComplete) {
			onComplete(false);
		}
		return;
	}

	// A listed policy is not an implemented native capability. Fail closed
	// before prompting the Creator for an action without a real dispatcher.
	if (actionType != u"dialogs.inspect_server"_q) {
		QJsonObject unsupported;
		unsupported[QStringLiteral("action_id")] = actionId;
		unsupported[QStringLiteral("action_type")] = actionType;
		unsupported[QStringLiteral("result")] = QStringLiteral("BLOCKED_NO_NATIVE_DISPATCH");
		unsupported[QStringLiteral("new_phase")] = QStringLiteral("BLOCKED");
		RecordBehavior(EventType::Execution, actionId,
			QStringLiteral("apo.executor.capability"), unsupported);
		if (onComplete) onComplete(false);
		return;
	}

	// Re-check authority after verification; a policy may change at any time.
	const auto authority = AuthorityManager::Instance().getActionAuthority(actionType);

	// Phase 2: VERIFIED
	QJsonObject verifyPayload;
	verifyPayload[QStringLiteral("action_id")] = actionId;
	verifyPayload[QStringLiteral("action_type")] = actionType;
	verifyPayload[QStringLiteral("authority_level")] = AuthorityLevelToString(authority);
	verifyPayload[QStringLiteral("proof_hash")] = verification.proofHash;

	if (authority == AuthorityLevel::AutoExecuteAllowed) {
		verifyPayload[QStringLiteral("result")] = QStringLiteral("ALLOWED_AUTO");
		verifyPayload[QStringLiteral("phase")] = ActionPhaseToString(ActionPhase::Verified);
		if (!RecordBehavior(EventType::ApoVerify, actionId, QStringLiteral("apo.authority.gate"), verifyPayload)) {
			if (onComplete) {
				onComplete(false);
			}
			return;
		}

		QJsonObject authorized;
		authorized[QStringLiteral("action_id")] = actionId;
		authorized[QStringLiteral("new_phase")] =
			ActionPhaseToString(ActionPhase::Authorized);
		authorized[QStringLiteral("result")] = QStringLiteral("AUTHORIZED_WITHIN_CREATOR_POLICY");
		if (!RecordBehavior(EventType::ApoVerify, actionId,
			QStringLiteral("apo.authority.gate"), authorized)) {
			if (onComplete) onComplete(false);
			return;
		}
		action.phase = ActionPhase::Authorized;
		executeInternal(action);
	} else if (authority == AuthorityLevel::RequireApproval) {
		verifyPayload[QStringLiteral("result")] = QStringLiteral("HELD_FOR_USER_APPROVAL");
		verifyPayload[QStringLiteral("phase")] = ActionPhaseToString(ActionPhase::PendingAuthority);
		if (!RecordBehavior(EventType::ApoVerify, actionId, QStringLiteral("apo.authority.gate"), verifyPayload)) {
			if (onComplete) {
				onComplete(false);
			}
			return;
		}

		// The Creator approval must be visible in the same authenticated
		// Telegram account. An unpresentable prompt is not approval.
		const auto window = Core::App().activeWindow();
		const auto windowSession = window ? window->maybeSession() : nullptr;
		if (!windowSession || !action.initiatingAccountUserId
			|| windowSession->userId().bare != action.initiatingAccountUserId) {
			QJsonObject unavailable;
			unavailable[QStringLiteral("action_id")] = actionId;
			unavailable[QStringLiteral("result")] = QStringLiteral("BLOCKED_NO_NATIVE_APPROVAL_UI");
			unavailable[QStringLiteral("new_phase")] = QStringLiteral("BLOCKED");
			RecordBehavior(EventType::ApoVerify, actionId,
				QStringLiteral("apo.authority.ui"), unavailable);
			if (onComplete) onComplete(false);
			return;
		}
		action.phase = ActionPhase::PendingAuthority;
		{
			QMutexLocker locker(&_mutex);
			_pendingActions[actionId] = action;
		}
		const auto displayTarget = target.isEmpty()
			? QStringLiteral("(not specified)") : target.left(120);
		const auto message = QStringLiteral(
			"Ω-Telegram requires Creator approval.\n\nAction: %1\nTarget: %2")
			.arg(actionType, displayTarget);
		window->show(Ui::MakeConfirmBox({
			.text = message,
			.confirmed = [actionId] {
				ActionExecutor::Instance().approveAction(actionId);
			},
			.cancelled = [actionId] {
				ActionExecutor::Instance().rejectAction(
					actionId, QStringLiteral("CREATOR_DECLINED"));
			},
			.confirmText = QStringLiteral("Approve"),
			.cancelText = QStringLiteral("Reject"),
		}));
	} else {
		verifyPayload[QStringLiteral("result")] = QStringLiteral("DENIED_BY_POLICY");
		verifyPayload[QStringLiteral("phase")] = ActionPhaseToString(ActionPhase::Denied);
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
	const auto currentSession = Core::App().someSessionExists()
		? Core::App().activeAccount().maybeSession() : nullptr;
	if (!currentSession || !action.initiatingAccountUserId
		|| currentSession->userId().bare != action.initiatingAccountUserId) {
		verifyPayload[QStringLiteral("result")] = QStringLiteral("APPROVAL_REJECTED_ACCOUNT_CHANGED");
		verifyPayload[QStringLiteral("new_phase")] = QStringLiteral("REJECTED");
		RecordBehavior(EventType::ApoVerify, actionId,
			QStringLiteral("apo.authority.ui"), verifyPayload);
		if (action.callback) action.callback(false);
		return;
	}
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
	if (!RecordBehavior(EventType::ApoVerify, actionId, QStringLiteral("ui.approval_dialog"), verifyPayload)) {
		if (action.callback) {
			action.callback(false);
		}
		return;
	}

	QJsonObject authorized;
	authorized[QStringLiteral("action_id")] = actionId;
	authorized[QStringLiteral("new_phase")] =
		ActionPhaseToString(ActionPhase::Authorized);
	authorized[QStringLiteral("result")] = QStringLiteral("AUTHORIZED_BY_CREATOR_APPROVAL");
	if (!RecordBehavior(EventType::ApoVerify, actionId,
		QStringLiteral("apo.authority.gate"), authorized)) {
		if (action.callback) action.callback(false);
		return;
	}
	action.phase = ActionPhase::Authorized;
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

void ActionExecutor::executeInternal(const PendingAction &action) {
	if (action.actionType == u"dialogs.inspect_server"_q) {
		// First bounded, non-mutating APΩ operation. Reuse the same native
		// Telegram API sender that drives the client, not a new transport.
		const auto session = Core::App().someSessionExists()
			? Core::App().activeAccount().maybeSession()
			: nullptr;
		if (!session || !session->userId()) {
			QJsonObject failure;
			failure[QStringLiteral("action_id")] = action.actionId;
			failure[QStringLiteral("result")] = QStringLiteral("FAILED_NO_AUTHENTICATED_SESSION");
			failure[QStringLiteral("phase")] = QStringLiteral("FAILED");
			RecordBehavior(EventType::Execution, action.actionId,
				QStringLiteral("apo.executor.native"), failure);
			if (action.callback) action.callback(false);
			return;
		}
		const auto requestId = std::make_shared<mtpRequestId>(0);
		const auto dispatchPersisted = std::make_shared<bool>(false);
		const auto requestLimit = 16;
		// MTProto messages.getDialogs has no server-side side effects.
		// Its response still must be reconciled with native Data::Session
		// before a read-only operation can be marked CLOSED.
		*requestId = session->api().request(MTPmessages_GetDialogs(
			MTP_flags(
				MTPmessages_GetDialogs::Flag::f_exclude_pinned
				| MTPmessages_GetDialogs::Flag::f_folder_id),
			MTP_int(0),
			MTP_int(0),
			MTP_int(0),
			MTP_inputPeerEmpty(),
			MTP_int(requestLimit),
			MTP_long(0)
		)).done([action, requestId, dispatchPersisted, session](const MTPmessages_Dialogs &result) {
			if (!*dispatchPersisted) {
				if (action.callback) action.callback(false);
				return;
			}
			QJsonObject ack;
			ack[QStringLiteral("action_id")] = action.actionId;
			ack[QStringLiteral("request_id")] = static_cast<qint64>(*requestId);
			ack[QStringLiteral("native_method")] = QStringLiteral("messages.getDialogs");
			ack[QStringLiteral("new_phase")] = QStringLiteral("ACKNOWLEDGED");
			ack[QStringLiteral("result")] = QStringLiteral("SERVER_RESPONSE_RECEIVED");
			if (!RecordBehavior(EventType::ObservedResult, action.actionId,
				QStringLiteral("apo.executor.native"), ack)) {
				if (action.callback) action.callback(false);
				return;
			}
			// Check both sides: decoded server dialogs and the separate native
			// in-memory chat-list projection. A response alone is not a state
			// confirmation, nor is a nonempty local list alone.
			int serverDialogs = 0;
			int matchedLocalDialogs = 0;
			const auto current = Core::App().someSessionExists()
				? Core::App().activeAccount().maybeSession()
				: nullptr;
			const auto sameSession = (current == session);
			const auto mainList = sameSession ? current->data().chatsList().get() : nullptr;
			const auto inspect = [&](const auto &data) {
				for (const auto &item : data.vdialogs().v) {
					item.match([&](const MTPDdialog &dialog) {
						++serverDialogs;
						if (!mainList) return;
						const auto peer = peerFromMTP(dialog.vpeer());
						if (!peer) return;
						for (const auto row : mainList->indexed()->all()) {
							const auto history = row->history();
							if (history && history->peer->id == peer) {
								++matchedLocalDialogs;
								break;
							}
						}
					}, [](const MTPDdialogFolder &) {
					}, [](const MTPDdialogCommunity &) {
					});
				}
			};
			result.match([](const MTPDmessages_dialogsNotModified &) {
			}, inspect, inspect);
			const auto stateConfirmed = mainList && mainList->loaded()
				&& serverDialogs > 0 && matchedLocalDialogs > 0;
			QJsonObject observed;
			observed[QStringLiteral("action_id")] = action.actionId;
			observed[QStringLiteral("request_id")] = static_cast<qint64>(*requestId);
			observed[QStringLiteral("server_dialogs")] = serverDialogs;
			observed[QStringLiteral("native_projection_matches")] = matchedLocalDialogs;
			observed[QStringLiteral("projection_loaded")] = mainList && mainList->loaded();
			observed[QStringLiteral("loaded_rows_before")] =
				action.parameters.value(QStringLiteral("loaded_rows")).toInt(-1);
			observed[QStringLiteral("loaded_rows_after")] =
				mainList ? mainList->indexed()->all().size() : -1;
			observed[QStringLiteral("new_phase")] =
				stateConfirmed ? QStringLiteral("STATE_CONFIRMED")
					: QStringLiteral("RECONCILING");
			observed[QStringLiteral("result")] = stateConfirmed
				? QStringLiteral("NATIVE_READ_POSTCONDITION_MATCHED")
				: QStringLiteral("NATIVE_READ_POSTCONDITION_UNCONFIRMED");
			if (!RecordBehavior(EventType::ObservedResult, action.actionId,
				QStringLiteral("apo.executor.native"), observed)) {
				if (action.callback) action.callback(false);
				return;
			}
			if (!stateConfirmed) {
				// Never call success or retry if the postcondition is unknown.
				if (action.callback) action.callback(false);
				return;
			}
			QJsonObject completed;
			completed[QStringLiteral("action_id")] = action.actionId;
			completed[QStringLiteral("request_id")] = static_cast<qint64>(*requestId);
			completed[QStringLiteral("new_phase")] = QStringLiteral("CLOSED");
			completed[QStringLiteral("result")] = QStringLiteral("SUCCESS_READ_ONLY");
			const auto persisted = RecordBehavior(
				EventType::StateReduce, action.actionId,
				QStringLiteral("apo.executor.native"), completed);
			if (action.callback) action.callback(persisted);
		}).fail([action, requestId, dispatchPersisted](const MTP::Error &) {
			if (!*dispatchPersisted) {
				if (action.callback) action.callback(false);
				return;
			}
			QJsonObject failure;
			failure[QStringLiteral("action_id")] = action.actionId;
			failure[QStringLiteral("request_id")] = static_cast<qint64>(*requestId);
			failure[QStringLiteral("native_method")] = QStringLiteral("messages.getDialogs");
			failure[QStringLiteral("new_phase")] = QStringLiteral("FAILED");
			failure[QStringLiteral("result")] = QStringLiteral("NATIVE_RPC_ERROR");
			RecordBehavior(EventType::ObservedResult, action.actionId,
				QStringLiteral("apo.executor.native"), failure);
			if (action.callback) action.callback(false);
		}).send();
		QJsonObject dispatched;
		dispatched[QStringLiteral("action_id")] = action.actionId;
		dispatched[QStringLiteral("request_id")] = static_cast<qint64>(*requestId);
		dispatched[QStringLiteral("native_method")] = QStringLiteral("messages.getDialogs");
		dispatched[QStringLiteral("new_phase")] =
			*requestId ? QStringLiteral("DISPATCHED") : QStringLiteral("FAILED");
		dispatched[QStringLiteral("result")] =
			*requestId ? QStringLiteral("NATIVE_REQUEST_QUEUED")
				: QStringLiteral("NATIVE_REQUEST_NOT_ACCEPTED");
		*dispatchPersisted = RecordBehavior(
			EventType::Execution, action.actionId,
			QStringLiteral("apo.executor.native"), dispatched);
		if (!*requestId && action.callback) action.callback(false);
		return;
	}
	// A VERIFIED intent is not a SENT operation. Until an actual native
	// Telegram dispatch adapter is connected, fail closed and never claim
	// ACKNOWLEDGED / OBSERVED / CLOSED or invoke a success callback.
	QJsonObject execPayload;
	execPayload[QStringLiteral("action_id")] = action.actionId;
	execPayload[QStringLiteral("action_type")] = action.actionType;
	execPayload[QStringLiteral("target_present")] = !action.target.isEmpty();
	execPayload[QStringLiteral("parameter_count")] = action.parameters.size();
	execPayload[QStringLiteral("parameters_redacted")] = true;
	execPayload[QStringLiteral("result")] = QStringLiteral("BLOCKED_NO_NATIVE_DISPATCH");
	execPayload[QStringLiteral("phase")] = ActionPhaseToString(action.phase);
	execPayload[QStringLiteral("new_phase")] = ActionPhaseToString(ActionPhase::Failed);

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
