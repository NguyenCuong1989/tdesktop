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
		if (!peer || text.trimmed().isEmpty()) {
			QJsonObject blocked;
			blocked[QStringLiteral("action_id")] = action.actionId;
			blocked[QStringLiteral("result")] = QStringLiteral("NATIVE_TARGET_OR_TEXT_UNAVAILABLE");
			blocked[QStringLiteral("new_phase")] = QStringLiteral("BLOCKED");
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
		intent[QStringLiteral("target_sha256")] = QString::fromUtf8(
			QCryptographicHash::hash(action.target.toUtf8(),
				QCryptographicHash::Sha256).toHex());
		intent[QStringLiteral("new_phase")] = QStringLiteral("PREPARING_DISPATCH");
		intent[QStringLiteral("result")] = QStringLiteral("NATIVE_DISPATCH_INTENT_PERSISTED");
		if (!RecordBehavior(EventType::Execution, action.actionId,
				QStringLiteral("apo.executor.native"), intent)) {
			if (action.callback) action.callback(false);
			return;
		}
		const auto requestId = std::make_shared<mtpRequestId>(0);
		*requestId = session->api().request(MTPmessages_SendMessage(
			MTP_flags(MTPmessages_SendMessage::Flag::f_no_webpage),
			peer->input(),
			MTPInputReplyTo(),
			MTP_string(text),
			MTP_long(randomId),
			MTPReplyMarkup(),
			MTPvector<MTPMessageEntity>(),
			MTP_int(0),
			MTP_int(0),
			MTP_inputPeerEmpty(),
			MTPInputQuickReplyShortcut(),
			MTP_long(0),
			MTP_long(0),
			MTPSuggestedPost(),
			MTPInputRichMessage()
		)).done([action, requestId,
				session, peer, randomId, randomHex, text](const MTPUpdates &result) {
			// PREPARING_DISPATCH was durably recorded before the RPC was sent.
			// A failed DISPATCHED append must not suppress a real server ACK.
			QJsonObject ack;
			ack[QStringLiteral("action_id")] = action.actionId;
			ack[QStringLiteral("request_id")] = static_cast<qint64>(*requestId);
			ack[QStringLiteral("random_id_hex")] = randomHex;
			ack[QStringLiteral("native_method")] = QStringLiteral("messages.sendMessage");
			ack[QStringLiteral("new_phase")] = QStringLiteral("ACKNOWLEDGED");
			ack[QStringLiteral("result")] = QStringLiteral("SERVER_RESPONSE_RECEIVED");
			if (!RecordBehavior(EventType::ObservedResult, action.actionId,
				QStringLiteral("apo.executor.native"), ack)) {
				if (action.callback) action.callback(false);
				return;
			}
			// The server response is not an observed message. Correlate
			// Telegram's message_id to the persisted random_id first.
			MsgId messageId = 0;
			if (result.type() == mtpc_updateShortSentMessage) {
				messageId = result.c_updateShortSentMessage().vid().v;
			} else {
				const auto inspectUpdates = [&](const auto &updates) {
					for (const auto &update : updates.vupdates().v) {
						if (update.type() == mtpc_updateMessageID) {
							const auto &mapping = update.c_updateMessageID();
							if (mapping.vrandom_id().v == randomId) {
								messageId = mapping.vid().v;
							}
						}
					}
				};
				if (result.type() == mtpc_updates) {
					inspectUpdates(result.c_updates());
				} else if (result.type() == mtpc_updatesCombined) {
					inspectUpdates(result.c_updatesCombined());
				}
			}
			const auto sameSession = Core::App().someSessionExists()
				&& Core::App().activeAccount().maybeSession() == session
				&& NativeAccountBindingConfirmed(
					action.initiatingAccountUserId, session->userId().bare);
			if (sameSession) {
				session->api().applyUpdates(result, randomId);
			}
			// A matching server message ID alone is insufficient: the native
			// projection must also be the outgoing text requested by APΩ.
			const auto projectedItem = (sameSession && messageId > 0)
				? session->data().message(peer->id, messageId) : nullptr;
			const auto observed = projectedItem && projectedItem->out()
				&& projectedItem->originalText().text == text;
			QJsonObject observation;
			observation[QStringLiteral("action_id")] = action.actionId;
			observation[QStringLiteral("request_id")] = static_cast<qint64>(*requestId);
			observation[QStringLiteral("random_id_hex")] = randomHex;
			observation[QStringLiteral("server_message_id")] = messageId.bare;
			observation[QStringLiteral("native_projection_present")] = observed;
			observation[QStringLiteral("new_phase")] = observed
				? QStringLiteral("STATE_CONFIRMED") : QStringLiteral("RECONCILING");
			observation[QStringLiteral("result")] = observed
				? QStringLiteral("NATIVE_SEND_POSTCONDITION_MATCHED")
				: QStringLiteral("NATIVE_SEND_POSTCONDITION_UNCONFIRMED");
			if (!RecordBehavior(EventType::ObservedResult, action.actionId,
				QStringLiteral("apo.executor.native"), observation)) {
				if (action.callback) action.callback(false);
				return;
			}
			if (!observed) {
				// Direct MTProto sends do not register a pending HistoryItem.
				// updateShortSentMessage carries an ID but no message body,
				// so the native projection may legitimately be absent at ACK.
				// Read back from the same peer, verify ID + exact text, and
				// feed Telegram's existing message store before closing.
				if (!sameSession || messageId <= 0) {
					if (action.callback) action.callback(false);
					return;
				}
				session->api().request(MTPmessages_GetHistory(
					peer->input(),
					MTP_int(0), MTP_int(0), MTP_int(0),
					MTP_int(30), MTP_int(0), MTP_int(0), MTP_long(0)
				)).done([action, requestId, session, peer, messageId,
						randomHex, text](const MTPmessages_Messages &history) {
					const auto stillBound = Core::App().someSessionExists()
						&& Core::App().activeAccount().maybeSession() == session
						&& NativeAccountBindingConfirmed(
							action.initiatingAccountUserId, session->userId().bare);
					bool serverMessageMatched = false;
					if (stillBound) {
						history.match(
							[](const MTPDmessages_messagesNotModified &) {},
							[&](const auto &data) {
								for (const auto &item : data.vmessages().v) {
									item.match([&](const MTPDmessage &message) {
										if (message.vid().v == messageId
											&& message.is_out()
											&& qs(message.vmessage()) == text) {
											serverMessageMatched = true;
										}
									}, [](const auto &) {});
								}
							});
						if (serverMessageMatched) {
							session->data().processExistingMessages(
								peer->asChannel(), history);
						}
					}
					const auto projectedItem = serverMessageMatched
						? session->data().message(peer->id, messageId) : nullptr;
					const auto projected = projectedItem && projectedItem->out()
						&& projectedItem->originalText().text == text;
					QJsonObject checked;
					checked[QStringLiteral("action_id")] = action.actionId;
					checked[QStringLiteral("request_id")] = static_cast<qint64>(*requestId);
					checked[QStringLiteral("random_id_hex")] = randomHex;
					checked[QStringLiteral("server_message_id")] = messageId.bare;
					checked[QStringLiteral("server_readback_matched")] = serverMessageMatched;
					checked[QStringLiteral("native_projection_present")] = projected;
					checked[QStringLiteral("new_phase")] = projected
						? QStringLiteral("STATE_CONFIRMED")
						: QStringLiteral("RECONCILING");
					checked[QStringLiteral("result")] = projected
						? QStringLiteral("NATIVE_SEND_POSTCONDITION_MATCHED")
						: QStringLiteral("NATIVE_SEND_READBACK_UNCONFIRMED");
					if (!RecordBehavior(EventType::ObservedResult, action.actionId,
							QStringLiteral("apo.executor.native"), checked)
						|| !projected) {
						if (action.callback) action.callback(false);
						return;
					}
					QJsonObject closed;
					closed[QStringLiteral("action_id")] = action.actionId;
					closed[QStringLiteral("request_id")] = static_cast<qint64>(*requestId);
					closed[QStringLiteral("random_id_hex")] = randomHex;
					closed[QStringLiteral("new_phase")] = QStringLiteral("CLOSED");
					closed[QStringLiteral("result")] =
						QStringLiteral("SUCCESS_NATIVE_SEND_OBSERVED");
					const auto persisted = RecordBehavior(EventType::StateReduce,
						action.actionId, QStringLiteral("apo.executor.native"), closed);
					if (action.callback) action.callback(persisted);
				}).fail([action, requestId, randomHex](const MTP::Error &) {
					QJsonObject uncertain;
					uncertain[QStringLiteral("action_id")] = action.actionId;
					uncertain[QStringLiteral("request_id")] = static_cast<qint64>(*requestId);
					uncertain[QStringLiteral("random_id_hex")] = randomHex;
					uncertain[QStringLiteral("new_phase")] = QStringLiteral("RECONCILING");
					uncertain[QStringLiteral("result")] =
						QStringLiteral("NATIVE_SEND_READBACK_RPC_ERROR");
					RecordBehavior(EventType::ObservedResult, action.actionId,
						QStringLiteral("apo.executor.native"), uncertain);
					if (action.callback) action.callback(false);
				}).send();
				return;
			}
			QJsonObject completed;
			completed[QStringLiteral("action_id")] = action.actionId;
			completed[QStringLiteral("request_id")] = static_cast<qint64>(*requestId);
			completed[QStringLiteral("random_id_hex")] = randomHex;
			completed[QStringLiteral("new_phase")] = QStringLiteral("CLOSED");
			completed[QStringLiteral("result")] = QStringLiteral("SUCCESS_NATIVE_SEND_OBSERVED");
			const auto persisted = RecordBehavior(EventType::StateReduce,
				action.actionId, QStringLiteral("apo.executor.native"), completed);
			if (action.callback) action.callback(persisted);
		}).fail([action, requestId, randomHex](const MTP::Error &) {
			// Preserve the uncertain outcome even if DISPATCHED was not persisted.
			QJsonObject uncertain;
			uncertain[QStringLiteral("action_id")] = action.actionId;
			uncertain[QStringLiteral("request_id")] = static_cast<qint64>(*requestId);
			uncertain[QStringLiteral("random_id_hex")] = randomHex;
			uncertain[QStringLiteral("native_method")] = QStringLiteral("messages.sendMessage");
			uncertain[QStringLiteral("new_phase")] = QStringLiteral("UNKNOWN_OUTCOME");
			uncertain[QStringLiteral("result")] = QStringLiteral("NATIVE_RPC_ERROR");
			RecordBehavior(EventType::ObservedResult, action.actionId,
				QStringLiteral("apo.executor.native"), uncertain);
			if (action.callback) action.callback(false);
		}).send();
		QJsonObject dispatched;
		dispatched[QStringLiteral("action_id")] = action.actionId;
		dispatched[QStringLiteral("request_id")] = static_cast<qint64>(*requestId);
		dispatched[QStringLiteral("random_id_hex")] = randomHex;
		dispatched[QStringLiteral("new_phase")] = *requestId
			? QStringLiteral("DISPATCHED") : QStringLiteral("UNKNOWN_OUTCOME");
		dispatched[QStringLiteral("result")] = *requestId
			? QStringLiteral("NATIVE_REQUEST_QUEUED")
			: QStringLiteral("NATIVE_REQUEST_NOT_ACCEPTED");
		// The durable intent already marks this write as potentially sent.
		// Failure to append DISPATCHED cannot erase later ACK evidence.
		RecordBehavior(EventType::Execution, action.actionId,
			QStringLiteral("apo.executor.native"), dispatched);
		if (!*requestId && action.callback) action.callback(false);
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
