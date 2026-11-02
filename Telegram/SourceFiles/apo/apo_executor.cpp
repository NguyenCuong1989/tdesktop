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
#include "data/data_peer_id.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_chat_participant_status.h"
#include "data/data_premium_limits.h"
#include "data/components/ephemeral_messages.h"
#include "base/random.h"
#include "dialogs/dialogs_main_list.h"
#include "history/history.h"
#include "history/history_item.h"
#include "window/window_controller.h"
#include "ui/boxes/confirm_box.h"
#include "settings.h"
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
	std::function<void(bool success)> onComplete,
	QString *submittedActionId) {
	const auto actionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	if (submittedActionId) {
		*submittedActionId = actionId;
	}
	const auto nowMs = QDateTime::currentMSecsSinceEpoch();

	PendingAction action;
	action.actionId = actionId;
	action.actionType = actionType;
	action.target = target;
	action.parameters = parameters;
	action.candidateState = candidateState;
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

	// The ledger can contain valid observations for several accounts/profiles.
	// Reject a foreign state pair before it reaches the mathematical verifier.
	if ((actionType == u"dialogs.inspect_server"_q
			|| actionType == u"messages.send_text"_q)
		&& !NativeAccountScopeConfirmed(
			action.initiatingAccountUserId, cWorkingDir(),
			parameters.value(QStringLiteral("apo_account_scope")).toString())) {
		QJsonObject blocked;
		blocked[QStringLiteral("action_id")] = actionId;
		blocked[QStringLiteral("result")] = QStringLiteral("FAILED_NATIVE_ACCOUNT_SCOPE_BINDING");
		blocked[QStringLiteral("new_phase")] = QStringLiteral("BLOCKED");
		RecordBehavior(EventType::ApoVerify, actionId,
			QStringLiteral("apo.executor.account_scope"), blocked);
		if (onComplete) onComplete(false);
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
	if (actionType != u"dialogs.inspect_server"_q
		&& actionType != u"messages.send_text"_q) {
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
			? QStringLiteral("(not specified)") : target;
		const auto message = (actionType == u"messages.send_text"_q)
			? NativeSendApprovalPreview(
				displayTarget,
				parameters.value(QStringLiteral("text")).toString())
			: QStringLiteral(
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
	const auto accountBound = NativeAccountBindingConfirmed(
		action.initiatingAccountUserId,
		currentSession ? currentSession->userId().bare : 0);
	// Authorization may have changed while this action awaited approval.
	const auto currentAuthority = AuthorityManager::Instance().getActionAuthority(action.actionType);
	const auto authorityPermitted =
		currentAuthority == AuthorityLevel::RequireApproval
		|| currentAuthority == AuthorityLevel::AutoExecuteAllowed;
	// Re-run the exact verifier against the state pair captured with this
	// pending action. A prior VERIFIED event is not a reusable approval token.
	VerificationResult revalidation;
	if (accountBound && authorityPermitted) {
		revalidation = ApoVerifier::Instance().verifyAction(
			action.actionType,
			action.target,
			action.parameters,
			action.candidateState);
	}
	if (!NativeApprovalMayProceed(
		accountBound, authorityPermitted, revalidation.passed)) {
		if (!accountBound) {
			verifyPayload[QStringLiteral("result")] =
				QStringLiteral("APPROVAL_REJECTED_ACCOUNT_CHANGED");
			verifyPayload[QStringLiteral("new_phase")] = QStringLiteral("REJECTED");
			RecordBehavior(EventType::ApoVerify, actionId,
				QStringLiteral("apo.authority.ui"), verifyPayload);
		} else if (!authorityPermitted) {
			verifyPayload[QStringLiteral("result")] =
				QStringLiteral("APPROVAL_REVOKED_BY_POLICY");
			verifyPayload[QStringLiteral("new_phase")] = QStringLiteral("REJECTED");
			RecordBehavior(EventType::ApoVerify, actionId,
				QStringLiteral("apo.authority.gate"), verifyPayload);
		} else {
			verifyPayload[QStringLiteral("result")] =
				QStringLiteral("APPROVAL_REJECTED_APO_REVALIDATION_FAILED");
			verifyPayload[QStringLiteral("reason")] = revalidation.rationale;
			verifyPayload[QStringLiteral("new_phase")] = QStringLiteral("REJECTED");
			RecordBehavior(EventType::ApoVerify, actionId,
				QStringLiteral("apo.authority.revalidation"), verifyPayload);
		}
		if (action.callback) action.callback(false);
		return;
	}
	verifyPayload[QStringLiteral("proof_hash")] = revalidation.proofHash;
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

void ActionExecutor::reconcilePendingNativeWrites(const QString &accountScope) {
	if (accountScope.isEmpty() || !Core::App().someSessionExists()) {
		return;
	}
	const auto session = Core::App().activeAccount().maybeSession();
	if (!session || !session->userId()
		|| !NativeAccountScopeConfirmed(
			session->userId().bare, cWorkingDir(), accountScope)) {
		return;
	}

	const auto validSha256 = [](const QString &value) {
		const auto encoded = value.toLatin1();
		return value.size() == 64
			&& QString::fromLatin1(encoded) == value
			&& QByteArray::fromHex(encoded).size() == 32
			&& QByteArray::fromHex(encoded).toHex() == encoded;
	};
	const auto nowMs = QDateTime::currentMSecsSinceEpoch();
	constexpr auto kMaxConcurrentNativeWriteRecoveries = 4;
	const auto pending = BehaviorLedger::Instance().unresolvedNativeWrites(
		accountScope);
	for (const auto &write : pending) {
		// Recovery is read-only and only resolves writes with a durable,
		// method-correlated server ACK and server message ID. A missing ACK or
		// ID remains UNKNOWN_OUTCOME; it is never converted into a resend.
		if (write.accountScope != accountScope
			|| !write.serverAcknowledged
			|| write.serverMessageId.isEmpty()
			|| write.requestId.isEmpty()
			|| write.randomIdHex.isEmpty()
			|| write.peerId.isEmpty()
			|| !validSha256(write.textSha256)
			|| !validSha256(write.targetSha256)
			|| write.phase == QStringLiteral("CONFLICTING_DISPATCH_INTENT")
			|| write.phase == QStringLiteral("SERVER_MESSAGE_ID_MISMATCH")
			|| write.phase == QStringLiteral("CORRELATION_MISMATCH")
			|| write.phase == QStringLiteral("REQUEST_ID_MISMATCH")
			|| write.phase == QStringLiteral("ACCOUNT_SCOPE_MISMATCH")
			|| write.phase == QStringLiteral("PEER_ID_MISMATCH")
			|| write.phase == QStringLiteral("ACK_NATIVE_METHOD_MISMATCH")) {
			continue;
		}

		bool requestValid = false;
		const auto originalRequestId = write.requestId.toLongLong(&requestValid);
		if (!requestValid || originalRequestId <= 0
			|| originalRequestId > 9007199254740991LL) {
			continue;
		}
		bool randomValid = false;
		const auto randomId = write.randomIdHex.toULongLong(&randomValid, 16);
		if (!randomValid || !randomId
			|| QString::number(randomId, 16) != write.randomIdHex) {
			continue;
		}
		bool peerIdValid = false;
		const auto serializedPeer = write.peerId.toULongLong(&peerIdValid);
		if (!peerIdValid || !serializedPeer
			|| QString::number(serializedPeer) != write.peerId) {
			continue;
		}
		bool messageIdValid = false;
		const auto messageIdValue = write.serverMessageId.toInt(&messageIdValid);
		if (!messageIdValid || messageIdValue <= 0) {
			continue;
		}

		const auto peerId = DeserializePeerId(serializedPeer);
		if ((!peerIsUser(peerId) && !peerIsChat(peerId) && !peerIsChannel(peerId))
			|| peerIsChannel(peerId) != write.peerIsChannel) {
			continue;
		}
		// Resolve through Telegram's native peer factory. After restart the
		// peer may not be loaded in the dialogs list yet; the read-only server
		// response below hydrates it through processExistingMessages().
		auto peer = session->data().peer(peerId);
		if (peer->id != peerId || peer->isChannel() != write.peerIsChannel) {
			continue;
		}
		const auto channel = peer->asChannel();
		if (write.peerIsChannel != (channel != nullptr)) {
			continue;
		}
		const auto messageId = static_cast<MsgId>(messageIdValue);
		const auto recoveryMethod = write.peerIsChannel
			? QStringLiteral("channels.getMessages")
			: QStringLiteral("messages.getMessages");
		const auto actionId = write.actionId;
		{
			QMutexLocker locker(&_mutex);
			if (_nativeWriteRecoveryInFlight.contains(actionId)
				|| _nativeWriteRecoveryInFlight.size()
					>= kMaxConcurrentNativeWriteRecoveries) {
				continue;
			}
			const auto lastAttempt = _nativeWriteRecoveryLastAttemptMs.value(
				actionId, 0);
			if (lastAttempt > 0
				&& (nowMs < lastAttempt || nowMs - lastAttempt < 60000)) {
				continue;
			}
			_nativeWriteRecoveryInFlight.insert(actionId);
			_nativeWriteRecoveryLastAttemptMs.insert(actionId, nowMs);
		}
		const auto finish = [this, actionId] {
			QMutexLocker locker(&_mutex);
			_nativeWriteRecoveryInFlight.remove(actionId);
		};

		QJsonObject intent;
		intent[QStringLiteral("action_id")] = actionId;
		intent[QStringLiteral("account_scope")] = accountScope;
		intent[QStringLiteral("request_id")] = originalRequestId;
		intent[QStringLiteral("random_id_hex")] = write.randomIdHex;
		intent[QStringLiteral("peer_id")] = write.peerId;
		intent[QStringLiteral("peer_is_channel")] = write.peerIsChannel;
		intent[QStringLiteral("native_method")] = recoveryMethod;
		intent[QStringLiteral("recovery_rpc_method")] = recoveryMethod;
		intent[QStringLiteral("server_message_id")] = messageIdValue;
		intent[QStringLiteral("text_sha256")] = write.textSha256;
		intent[QStringLiteral("target_sha256")] = write.targetSha256;
		intent[QStringLiteral("new_phase")] =
			QStringLiteral("RECOVERY_DISPATCH_INTENT");
		intent[QStringLiteral("result")] =
			QStringLiteral("NATIVE_RECOVERY_READBACK_INTENT_PERSISTED");
		if (!RecordBehavior(EventType::Execution, actionId,
			QStringLiteral("apo.executor.native"), intent)) {
			finish();
			continue;
		}

		const auto dispatchPersisted = std::make_shared<bool>(false);
		const auto queuedRequestId = std::make_shared<mtpRequestId>(0);
		const auto onDone = [
				session, peer, channel, write, accountScope, originalRequestId,
				messageId, messageIdValue, recoveryMethod, dispatchPersisted,
				queuedRequestId, finish](
				const MTPmessages_Messages &result,
				mtpRequestId recoveryRequestId) {
			if (!*dispatchPersisted) {
				finish();
				return;
			}
			if (recoveryRequestId <= 0
				|| recoveryRequestId != *queuedRequestId
				|| recoveryRequestId > 9007199254740991ULL) {
				QJsonObject unresolved;
				unresolved[QStringLiteral("action_id")] = write.actionId;
				unresolved[QStringLiteral("account_scope")] = accountScope;
				unresolved[QStringLiteral("request_id")] = originalRequestId;
				unresolved[QStringLiteral("recovery_request_id")] =
					static_cast<qint64>(recoveryRequestId);
				unresolved[QStringLiteral("recovery_rpc_method")] = recoveryMethod;
				unresolved[QStringLiteral("random_id_hex")] = write.randomIdHex;
				unresolved[QStringLiteral("new_phase")] = QStringLiteral("RECONCILING");
				unresolved[QStringLiteral("result")] =
					QStringLiteral("NATIVE_RECOVERY_RPC_CORRELATION_MISMATCH");
				RecordBehavior(EventType::ObservedResult, write.actionId,
					QStringLiteral("apo.executor.native"), unresolved);
				finish();
				return;
			}

			// Require the server readback itself to contain the requested
			// non-empty message. A stale cached HistoryItem must never turn an
			// empty/deleted server response into a recovered send.
			const auto responseContainsMessage = result.match(
				[](const MTPDmessages_messagesNotModified &) {
					return false;
				},
				[&](const auto &data) {
					for (const auto &entry : data.vmessages().v) {
						if (entry.type() == mtpc_message
							&& entry.c_message().vid().v == messageId.bare
							&& peerFromMTP(entry.c_message().vpeer_id()) == peer->id) {
							return true;
						}
					}
					return false;
				});
			// Reuse Telegram's native decoder/projection path. The RPC is
			// read-only and addresses only the persisted peer + server message ID.
			session->data().processExistingMessages(channel, result);
			const auto sameSession = Core::App().someSessionExists()
				&& Core::App().activeAccount().maybeSession() == session
				&& session->userId()
				&& NativeAccountScopeConfirmed(
					session->userId().bare, cWorkingDir(), accountScope);
			const auto item = sameSession
				? session->data().message(peer->id, messageId)
				: nullptr;
			const auto samePeer = item && item->history()
				&& item->history()->peer->id == peer->id;
			const auto observedTextHash = samePeer
				? QString::fromLatin1(QCryptographicHash::hash(
					item->originalText().text.toUtf8(),
					QCryptographicHash::Sha256).toHex())
				: QString();
			const auto exactText = samePeer
				&& observedTextHash == write.textSha256;
			const auto observed = responseContainsMessage
				&& NativeSendProjectionConfirmed(
					sameSession,
					samePeer && item->id == messageId,
					samePeer && item->out(),
					exactText);

			QJsonObject observation;
			observation[QStringLiteral("action_id")] = write.actionId;
			observation[QStringLiteral("account_scope")] = accountScope;
			observation[QStringLiteral("request_id")] = originalRequestId;
			observation[QStringLiteral("recovery_request_id")] =
				static_cast<qint64>(recoveryRequestId);
			observation[QStringLiteral("recovery_rpc_method")] = recoveryMethod;
			observation[QStringLiteral("native_method")] =
				QStringLiteral("messages.sendMessage");
			observation[QStringLiteral("evidence_source")] =
				QStringLiteral("NATIVE_RECOVERY_READBACK");
			observation[QStringLiteral("random_id_hex")] = write.randomIdHex;
			observation[QStringLiteral("peer_id")] = write.peerId;
			observation[QStringLiteral("peer_is_channel")] = write.peerIsChannel;
			observation[QStringLiteral("text_sha256")] = write.textSha256;
			observation[QStringLiteral("target_sha256")] = write.targetSha256;
			observation[QStringLiteral("server_message_id")] = messageIdValue;
			observation[QStringLiteral("recovery_response_contains_message")] =
				responseContainsMessage;
			observation[QStringLiteral("native_projection_present")] = observed;
			observation[QStringLiteral("new_phase")] = observed
				? QStringLiteral("STATE_CONFIRMED")
				: QStringLiteral("RECONCILING");
			observation[QStringLiteral("result")] = observed
				? QStringLiteral("NATIVE_SEND_POSTCONDITION_MATCHED")
				: QStringLiteral("NATIVE_RECOVERY_READBACK_UNCONFIRMED");
		if (!RecordBehavior(EventType::ObservedResult, write.actionId,
			QStringLiteral("apo.executor.native"), observation)) {
			finish();
			return;
		}
		if (observed) {
			QJsonObject closed;
			closed[QStringLiteral("action_id")] = write.actionId;
			closed[QStringLiteral("account_scope")] = accountScope;
			closed[QStringLiteral("request_id")] = originalRequestId;
			closed[QStringLiteral("recovery_request_id")] =
				static_cast<qint64>(recoveryRequestId);
			closed[QStringLiteral("recovery_rpc_method")] = recoveryMethod;
			closed[QStringLiteral("native_method")] =
				QStringLiteral("messages.sendMessage");
			closed[QStringLiteral("evidence_source")] =
				QStringLiteral("NATIVE_RECOVERY_READBACK");
			closed[QStringLiteral("random_id_hex")] = write.randomIdHex;
			closed[QStringLiteral("peer_id")] = write.peerId;
			closed[QStringLiteral("peer_is_channel")] = write.peerIsChannel;
			closed[QStringLiteral("text_sha256")] = write.textSha256;
			closed[QStringLiteral("target_sha256")] = write.targetSha256;
			closed[QStringLiteral("server_message_id")] = messageIdValue;
			closed[QStringLiteral("recovery_response_contains_message")] = true;
			closed[QStringLiteral("native_projection_present")] = true;
			closed[QStringLiteral("new_phase")] = QStringLiteral("CLOSED");
			closed[QStringLiteral("result")] =
				QStringLiteral("SUCCESS_NATIVE_SEND_OBSERVED");
			RecordBehavior(EventType::StateReduce, write.actionId,
				QStringLiteral("apo.executor.native"), closed);
		}
		finish();
	};
	const auto onFail = [
			write, accountScope, originalRequestId, recoveryMethod,
			dispatchPersisted, queuedRequestId, finish](
			const MTP::Error &, mtpRequestId recoveryRequestId) {
		if (!*dispatchPersisted) {
			finish();
			return;
		}
		QJsonObject unresolved;
		unresolved[QStringLiteral("action_id")] = write.actionId;
		unresolved[QStringLiteral("account_scope")] = accountScope;
		unresolved[QStringLiteral("request_id")] = originalRequestId;
		unresolved[QStringLiteral("recovery_request_id")] =
			static_cast<qint64>(recoveryRequestId);
		unresolved[QStringLiteral("recovery_rpc_method")] = recoveryMethod;
		unresolved[QStringLiteral("random_id_hex")] = write.randomIdHex;
		unresolved[QStringLiteral("new_phase")] = QStringLiteral("RECONCILING");
		unresolved[QStringLiteral("result")] =
			(recoveryRequestId == *queuedRequestId)
			? QStringLiteral("NATIVE_RECOVERY_READBACK_RPC_ERROR")
			: QStringLiteral("NATIVE_RECOVERY_RPC_CORRELATION_MISMATCH");
		RecordBehavior(EventType::ObservedResult, write.actionId,
			QStringLiteral("apo.executor.native"), unresolved);
		finish();
	};

	if (channel) {
		*queuedRequestId = session->api().request(MTPchannels_GetMessages(
			channel->inputChannel(),
			MTP_vector<MTPInputMessage>(
				1, MTP_inputMessageID(MTP_int(messageId))
		))).done(onDone).fail(onFail).send();
	} else {
		*queuedRequestId = session->api().request(MTPmessages_GetMessages(
			MTP_vector<MTPInputMessage>(
				1, MTP_inputMessageID(MTP_int(messageId))
		))).done(onDone).fail(onFail).send();
	}

	QJsonObject dispatched;
	dispatched[QStringLiteral("action_id")] = actionId;
	dispatched[QStringLiteral("account_scope")] = accountScope;
	dispatched[QStringLiteral("request_id")] = originalRequestId;
	dispatched[QStringLiteral("recovery_request_id")] =
		static_cast<qint64>(*queuedRequestId);
	dispatched[QStringLiteral("recovery_rpc_method")] = recoveryMethod;
	dispatched[QStringLiteral("random_id_hex")] = write.randomIdHex;
	dispatched[QStringLiteral("peer_id")] = write.peerId;
	dispatched[QStringLiteral("peer_is_channel")] = write.peerIsChannel;
	dispatched[QStringLiteral("native_method")] = recoveryMethod;
	dispatched[QStringLiteral("new_phase")] = *queuedRequestId
		? QStringLiteral("RECOVERY_DISPATCHED")
		: QStringLiteral("RECONCILING");
	dispatched[QStringLiteral("result")] = *queuedRequestId
		? QStringLiteral("NATIVE_RECOVERY_READBACK_QUEUED")
		: QStringLiteral("NATIVE_RECOVERY_READBACK_NOT_QUEUED");
	*dispatchPersisted = RecordBehavior(EventType::Execution, actionId,
		QStringLiteral("apo.executor.native"), dispatched);
	if (!*dispatchPersisted || !*queuedRequestId) {
		finish();
	}
	}
}

void ActionExecutor::executeInternal(const PendingAction &action) {
	if (action.actionType == u"messages.send_text"_q) {
		// Use the existing Telegram session and MTProto sender. The fixed
		// APΩ verifier and existing Creator authority gate ran in submitAction.
		const auto session = Core::App().someSessionExists()
			? Core::App().activeAccount().maybeSession() : nullptr;
		if (!session || !session->userId()
			|| !NativeAccountBindingConfirmed(
				action.initiatingAccountUserId, session->userId().bare)
			|| !NativeAccountScopeConfirmed(
				session->userId().bare, cWorkingDir(),
				action.parameters.value(QStringLiteral("apo_account_scope")).toString())) {
			QJsonObject failure;
			failure[QStringLiteral("action_id")] = action.actionId;
			failure[QStringLiteral("result")] = QStringLiteral("NO_MATCHING_NATIVE_SESSION_OR_SCOPE");
			failure[QStringLiteral("new_phase")] = QStringLiteral("BLOCKED");
			RecordBehavior(EventType::Execution, action.actionId,
				QStringLiteral("apo.executor.native"), failure);
			if (action.callback) action.callback(false);
			return;
		}
		const auto text = action.parameters.value(QStringLiteral("text")).toString();
		PeerData *peer = nullptr;
		if (action.target == u"self"_q) {
			peer = session->user().get();
		} else if (action.target.startsWith(u"peer:"_q)) {
			bool valid = false;
			const auto serialized = action.target.mid(5).toULongLong(&valid);
			if (valid) {
				peer = session->data().peerLoaded(DeserializePeerId(serialized));
			}
		}
		// One verified APΩ action must map to one ordinary native send.
		// Refuse unauthorized, paid or split payloads before persisting intent.
		if (!peer || text.trimmed().isEmpty() || text != text.trimmed()
			|| text.size() > Data::PremiumLimits(session).messageLengthCurrent()
			|| !Data::CanSendTexts(peer)
			|| peer->starsPerMessageChecked() > 0) {
			QJsonObject blocked;
			blocked[QStringLiteral("action_id")] = action.actionId;
			blocked[QStringLiteral("result")] = QStringLiteral("NATIVE_TARGET_OR_TEXT_UNAVAILABLE");
			blocked[QStringLiteral("new_phase")] = QStringLiteral("BLOCKED");
			RecordBehavior(EventType::Execution, action.actionId,
				QStringLiteral("apo.executor.native"), blocked);
			if (action.callback) action.callback(false);
			return;
		}
		// Keep Telegram's native send preparation, including the loaded
		// History, message policy and local-item registration. Reject special
		// ephemeral sends: they do not satisfy this text-action contract.
		auto outgoing = Api::MessageToSend(
			Api::SendAction(session->data().history(peer->id)));
		outgoing.action.clearDraft = false;
		outgoing.textWithTags.text = text;
		outgoing.webPage.removed = true;
		if (session->ephemeralMessages().wouldSend(outgoing)) {
			QJsonObject blocked;
			blocked[QStringLiteral("action_id")] = action.actionId;
			blocked[QStringLiteral("new_phase")] = QStringLiteral("BLOCKED");
			blocked[QStringLiteral("result")] =
				QStringLiteral("NATIVE_EPHEMERAL_SEND_UNSUPPORTED");
			RecordBehavior(EventType::Execution, action.actionId,
				QStringLiteral("apo.executor.native"), blocked);
			if (action.callback) action.callback(false);
			return;
		}
		// Telegram's random_id is an idempotency key, not the MTProto request
		// ID. Persist it before dispatch; never silently retry a write.
		const auto randomId = base::RandomValue<uint64>();
		const auto randomHex = QString::number(randomId, 16);
		const auto textHash = QString::fromUtf8(QCryptographicHash::hash(
			text.toUtf8(), QCryptographicHash::Sha256).toHex());
		const auto targetHash = QString::fromUtf8(QCryptographicHash::hash(
			action.target.toUtf8(), QCryptographicHash::Sha256).toHex());
		const auto peerId = QString::number(SerializePeerId(peer->id));
		const auto peerIsChannel = peer->isChannel();
		const auto accountScope =
			action.parameters.value(QStringLiteral("apo_account_scope")).toString();
		QJsonObject intent;
		intent[QStringLiteral("action_id")] = action.actionId;
		intent[QStringLiteral("action_type")] = action.actionType;
		intent[QStringLiteral("native_method")] = QStringLiteral("messages.sendMessage");
		intent[QStringLiteral("account_scope")] = accountScope;
		intent[QStringLiteral("random_id_hex")] = randomHex;
		intent[QStringLiteral("text_sha256")] = textHash;
		intent[QStringLiteral("text_length")] = text.size();
		intent[QStringLiteral("target_sha256")] = targetHash;
		intent[QStringLiteral("peer_id")] = peerId;
		intent[QStringLiteral("peer_is_channel")] = peerIsChannel;
		intent[QStringLiteral("new_phase")] = QStringLiteral("PREPARING_DISPATCH");
		intent[QStringLiteral("result")] = QStringLiteral("NATIVE_DISPATCH_INTENT_PERSISTED");
		if (!RecordBehavior(EventType::Execution, action.actionId,
				QStringLiteral("apo.executor.native"), intent)) {
			if (action.callback) action.callback(false);
			return;
		}
		// ApiWrap is Telegram's established send pipeline: it registers a
		// local HistoryItem and random_id, and Data::Histories applies
		// Api::Updates before calling this completion observer.
		auto tracking = std::make_shared<ApiWrap::TrackedTextSend>();
		tracking->randomId = randomId;
		tracking->queued = [
				action, randomId, randomHex, accountScope,
				textHash, targetHash, peerId, peerIsChannel](
				uint64 queuedRandomId, int nativeQueueId) {
			QJsonObject queued;
			queued[QStringLiteral("action_id")] = action.actionId;
			queued[QStringLiteral("account_scope")] = accountScope;
			queued[QStringLiteral("native_method")] =
				QStringLiteral("messages.sendMessage");
			queued[QStringLiteral("random_id_hex")] = randomHex;
			queued[QStringLiteral("text_sha256")] = textHash;
			queued[QStringLiteral("target_sha256")] = targetHash;
			queued[QStringLiteral("peer_id")] = peerId;
			queued[QStringLiteral("peer_is_channel")] = peerIsChannel;
			queued[QStringLiteral("native_queue_id")] = nativeQueueId;
			const auto correlated = queuedRandomId == randomId
				&& nativeQueueId > 0;
			queued[QStringLiteral("new_phase")] = correlated
				? QStringLiteral("QUEUED") : QStringLiteral("RECONCILING");
			queued[QStringLiteral("result")] = correlated
				? QStringLiteral("NATIVE_APIWRAP_SEND_SUBMITTED")
				: QStringLiteral("NATIVE_APIWRAP_QUEUE_CORRELATION_INVALID");
			RecordBehavior(EventType::Execution, action.actionId,
				QStringLiteral("apo.executor.native"), queued);
		};
		tracking->rejected = [
				action, randomId, randomHex, accountScope,
				textHash, targetHash, peerId, peerIsChannel](
				uint64 rejectedRandomId, const QString &reason) {
			QJsonObject rejected;
			rejected[QStringLiteral("action_id")] = action.actionId;
			rejected[QStringLiteral("account_scope")] = accountScope;
			rejected[QStringLiteral("native_method")] =
				QStringLiteral("messages.sendMessage");
			rejected[QStringLiteral("random_id_hex")] = randomHex;
			rejected[QStringLiteral("text_sha256")] = textHash;
			rejected[QStringLiteral("target_sha256")] = targetHash;
			rejected[QStringLiteral("peer_id")] = peerId;
			rejected[QStringLiteral("peer_is_channel")] = peerIsChannel;
			const auto correlated = rejectedRandomId == randomId;
			rejected[QStringLiteral("new_phase")] = correlated
				? QStringLiteral("REJECTED_BEFORE_DISPATCH")
				: QStringLiteral("RECONCILING");
			rejected[QStringLiteral("result")] = correlated
				? reason
				: QStringLiteral("NATIVE_APIWRAP_REJECTION_CORRELATION_INVALID");
			RecordBehavior(EventType::Execution, action.actionId,
				QStringLiteral("apo.executor.native"), rejected);
			if (action.callback) action.callback(false);
		};
		tracking->done = [action, session, peer, text, randomHex, randomId, accountScope, textHash, targetHash, peerId, peerIsChannel](
				uint64 completedRandomId,
				const MTPUpdates &result,
				const MTP::Response &response) {
			const auto requestId = response.requestId;
			const auto correlationValid = (completedRandomId != 0)
				&& completedRandomId == randomId
				&& requestId > 0;
			if (!correlationValid) {
				QJsonObject unresolved;
				unresolved[QStringLiteral("action_id")] = action.actionId;
				unresolved[QStringLiteral("random_id_hex")] = randomHex;
				unresolved[QStringLiteral("new_phase")] = QStringLiteral("RECONCILING");
				unresolved[QStringLiteral("result")] =
					QStringLiteral("NATIVE_SEND_CALLBACK_CORRELATION_INVALID");
				RecordBehavior(EventType::ObservedResult, action.actionId,
					QStringLiteral("apo.executor.native"), unresolved);
				if (action.callback) action.callback(false);
				return;
			}
			// The MTProto send response may contain a shortSentMessage or an
			// updateMessageID linked to the exact random_id. Telegram's native
			// update pipeline already processed the response before this hook.
			MsgId messageId = 0;
			if (result.type() == mtpc_updateShortSentMessage) {
				messageId = result.c_updateShortSentMessage().vid().v;
			} else {
				const auto matchRandomId = [&](const auto &updates) {
					for (const auto &update : updates.vupdates().v) {
						if (update.type() == mtpc_updateMessageID) {
							const auto &mapping = update.c_updateMessageID();
							if (mapping.vrandom_id().v == completedRandomId) {
								messageId = mapping.vid().v;
							}
						}
					}
				};
				if (result.type() == mtpc_updates) {
					matchRandomId(result.c_updates());
				} else if (result.type() == mtpc_updatesCombined) {
					matchRandomId(result.c_updatesCombined());
				}
			}
			QJsonObject ack;
			ack[QStringLiteral("action_id")] = action.actionId;
			ack[QStringLiteral("request_id")] = static_cast<qint64>(requestId);
			ack[QStringLiteral("random_id_hex")] = randomHex;
			ack[QStringLiteral("native_method")] = QStringLiteral("messages.sendMessage");
            ack[QStringLiteral("account_scope")] = accountScope;
            ack[QStringLiteral("text_sha256")] = textHash;
            ack[QStringLiteral("target_sha256")] = targetHash;
            ack[QStringLiteral("peer_id")] = peerId;
            ack[QStringLiteral("peer_is_channel")] = peerIsChannel;
			if (messageId > 0) {
				ack[QStringLiteral("server_message_id")] = messageId.bare;
			}
			ack[QStringLiteral("new_phase")] = QStringLiteral("ACKNOWLEDGED");
			ack[QStringLiteral("result")] = QStringLiteral("SERVER_RESPONSE_RECEIVED");
			if (!RecordBehavior(EventType::ObservedResult, action.actionId,
					QStringLiteral("apo.executor.native"), ack)) {
				if (action.callback) action.callback(false);
				return;
			}
			const auto sameSession = Core::App().someSessionExists()
				&& Core::App().activeAccount().maybeSession() == session
				&& NativeAccountBindingConfirmed(
					action.initiatingAccountUserId, session->userId().bare);
			const auto projectedItem = (sameSession && messageId > 0)
				? session->data().message(peer->id, messageId) : nullptr;
			const auto observed = NativeSendProjectionConfirmed(
				sameSession, messageId > 0, projectedItem && projectedItem->out(),
				projectedItem && projectedItem->originalText().text == text);
			QJsonObject observation;
			observation[QStringLiteral("action_id")] = action.actionId;
            observation[QStringLiteral("account_scope")] = accountScope;
            observation[QStringLiteral("native_method")] = QStringLiteral("messages.sendMessage");
            observation[QStringLiteral("text_sha256")] = textHash;
            observation[QStringLiteral("target_sha256")] = targetHash;
            observation[QStringLiteral("peer_id")] = peerId;
            observation[QStringLiteral("peer_is_channel")] = peerIsChannel;
            observation[QStringLiteral("evidence_source")] = QStringLiteral("NATIVE_SEND_CALLBACK");
			observation[QStringLiteral("request_id")] = static_cast<qint64>(requestId);
			observation[QStringLiteral("random_id_hex")] = randomHex;
			observation[QStringLiteral("server_message_id")] = messageId.bare;
			observation[QStringLiteral("native_projection_present")] = observed;
			observation[QStringLiteral("new_phase")] = observed
				? QStringLiteral("STATE_CONFIRMED") : QStringLiteral("RECONCILING");
			observation[QStringLiteral("result")] = observed
				? QStringLiteral("NATIVE_SEND_POSTCONDITION_MATCHED")
				: QStringLiteral("NATIVE_SEND_POSTCONDITION_UNCONFIRMED");
			if (!RecordBehavior(EventType::ObservedResult, action.actionId,
					QStringLiteral("apo.executor.native"), observation) || !observed) {
				// Delayed PTS / getDifference can still resolve the same local
				// message. Never retry this write or infer success from its ACK.
				if (action.callback) action.callback(false);
				return;
			}
			QJsonObject closed;
			closed[QStringLiteral("action_id")] = action.actionId;
            closed[QStringLiteral("account_scope")] = accountScope;
            closed[QStringLiteral("native_method")] = QStringLiteral("messages.sendMessage");
            closed[QStringLiteral("text_sha256")] = textHash;
            closed[QStringLiteral("target_sha256")] = targetHash;
            closed[QStringLiteral("peer_id")] = peerId;
            closed[QStringLiteral("peer_is_channel")] = peerIsChannel;
            closed[QStringLiteral("evidence_source")] = QStringLiteral("NATIVE_SEND_CALLBACK");
            closed[QStringLiteral("server_message_id")] = messageId.bare;
			closed[QStringLiteral("request_id")] = static_cast<qint64>(requestId);
			closed[QStringLiteral("random_id_hex")] = randomHex;
			closed[QStringLiteral("new_phase")] = QStringLiteral("CLOSED");
			closed[QStringLiteral("result")] =
				QStringLiteral("SUCCESS_NATIVE_SEND_OBSERVED");
			const auto persisted = RecordBehavior(EventType::StateReduce,
				action.actionId, QStringLiteral("apo.executor.native"), closed);
			if (action.callback) action.callback(persisted);
		};
		tracking->fail = [action, randomId, randomHex, peerId, peerIsChannel](
				uint64 failedRandomId,
				const MTP::Error &,
				const MTP::Response &response) {
			QJsonObject uncertain;
			uncertain[QStringLiteral("action_id")] = action.actionId;
			uncertain[QStringLiteral("request_id")] =
				static_cast<qint64>(response.requestId);
			uncertain[QStringLiteral("random_id_hex")] = randomHex;
			uncertain[QStringLiteral("peer_id")] = peerId;
			uncertain[QStringLiteral("peer_is_channel")] = peerIsChannel;
			uncertain[QStringLiteral("native_method")] =
				QStringLiteral("messages.sendMessage");
			uncertain[QStringLiteral("native_random_id_correlated")] =
				(failedRandomId == randomId);
			uncertain[QStringLiteral("new_phase")] =
				QStringLiteral("UNKNOWN_OUTCOME");
			uncertain[QStringLiteral("result")] =
				QStringLiteral("NATIVE_RPC_ERROR");
			RecordBehavior(EventType::ObservedResult, action.actionId,
				QStringLiteral("apo.executor.native"), uncertain);
			if (action.callback) action.callback(false);
		};
		// Telegram itself creates the optimistic local item and enqueues the
		// request through Data::Histories::sendPreparedMessage. Only one
		// message is allowed per APΩ action to keep its random_id unambiguous.
		session->api().sendMessage(std::move(outgoing), std::nullopt, tracking);
		return;
	}

	if (action.actionType == u"dialogs.inspect_server"_q) {
		// First bounded, non-mutating APΩ operation. Reuse the same native
		// Telegram API sender that drives the client, not a new transport.
		const auto session = Core::App().someSessionExists()
			? Core::App().activeAccount().maybeSession()
			: nullptr;
		if (!session || !session->userId()
			|| !NativeAccountBindingConfirmed(
				action.initiatingAccountUserId, session->userId().bare)) {
			QJsonObject failure;
			failure[QStringLiteral("action_id")] = action.actionId;
			failure[QStringLiteral("result")] = (!session || !session->userId())
				? QStringLiteral("FAILED_NO_AUTHENTICATED_SESSION")
				: QStringLiteral("FAILED_NATIVE_ACCOUNT_BINDING");
			failure[QStringLiteral("phase")] = QStringLiteral("FAILED");
			RecordBehavior(EventType::Execution, action.actionId,
				QStringLiteral("apo.executor.native"), failure);
			if (action.callback) action.callback(false);
			return;
		}
		// Recheck after a delayed Creator approval, immediately before MTProto.
		if (!NativeAccountScopeConfirmed(
			session->userId().bare, cWorkingDir(),
			action.parameters.value(QStringLiteral("apo_account_scope")).toString())) {
			QJsonObject failure;
			failure[QStringLiteral("action_id")] = action.actionId;
			failure[QStringLiteral("result")] = QStringLiteral("FAILED_NATIVE_ACCOUNT_SCOPE_BINDING");
			failure[QStringLiteral("new_phase")] = QStringLiteral("FAILED");
			RecordBehavior(EventType::Execution, action.actionId,
				QStringLiteral("apo.executor.native"), failure);
			if (action.callback) action.callback(false);
			return;
		}
		// Persist an account-scoped intent BEFORE MTProto can enqueue a request.
		// A crash between this event and DISPATCHED remains recoverable as
		// an unknown outcome; a persistence failure must not dispatch.
		QJsonObject dispatchIntent;
		dispatchIntent[QStringLiteral("action_id")] = action.actionId;
		dispatchIntent[QStringLiteral("native_method")] = QStringLiteral("messages.getDialogs");
		dispatchIntent[QStringLiteral("account_scope")] =
			action.parameters.value(QStringLiteral("apo_account_scope")).toString();
		dispatchIntent[QStringLiteral("new_phase")] = QStringLiteral("PREPARING_DISPATCH");
		dispatchIntent[QStringLiteral("result")] = QStringLiteral("NATIVE_DISPATCH_INTENT_PERSISTED");
		if (!RecordBehavior(EventType::Execution, action.actionId,
			QStringLiteral("apo.executor.native"), dispatchIntent)) {
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
						for (const auto &row : mainList->indexed()->all()) {
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
			const auto stateConfirmed = NativeDialogsPostconditionConfirmed(
				mainList && mainList->loaded(), serverDialogs, matchedLocalDialogs);
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
			// A post-dispatch RPC failure alone cannot prove the server
			// never processed the request. Preserve an unknown outcome for
			// hash-chain-backed recovery rather than claiming terminal failure.
			failure[QStringLiteral("new_phase")] = QStringLiteral("UNKNOWN_OUTCOME");
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
