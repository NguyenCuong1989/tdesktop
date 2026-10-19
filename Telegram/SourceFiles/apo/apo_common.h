/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonDocument>
#include <QtCore/QDateTime>
#include <QtCore/QCryptographicHash>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <memory>

namespace apo {

// Define the APΩ integer type explicitly so native modules compile
// independently of Telegram Desktop precompiled headers.
using int64 = qint64;

// Universal System Behavior Event Types
enum class EventType {
	UiEvent,          // Real mouse/key/UI interaction
	ExternalInput,    // Telegram server MTProto update / message
	StateReduce,      // Telegram state mutation / session update
	Computation,      // Local SLM / heuristic inference
	Decision,         // Agent strategy / candidate action
	ApoVerify,        // Verification & constraint check
	Execution,        // Outgoing MTProto request / local operation
	ObservedResult,   // Server response / receipt observation
	UiRender,         // View re-render / widget notification
};

[[nodiscard]] inline QString EventTypeToString(EventType type) {
	switch (type) {
	case EventType::UiEvent: return QStringLiteral("RAW.UI_EVENT");
	case EventType::ExternalInput: return QStringLiteral("RAW.EXTERNAL_INPUT");
	case EventType::StateReduce: return QStringLiteral("RAW.STATE_REDUCE");
	case EventType::Computation: return QStringLiteral("RAW.COMPUTATION");
	case EventType::Decision: return QStringLiteral("RAW.DECISION");
	case EventType::ApoVerify: return QStringLiteral("RAW.APO_VERIFY");
	case EventType::Execution: return QStringLiteral("RAW.EXECUTION");
	case EventType::ObservedResult: return QStringLiteral("RAW.OBSERVED_RESULT");
	case EventType::UiRender: return QStringLiteral("RAW.UI_RENDER");
	}
	return QStringLiteral("RAW.UNKNOWN");
}

// Capability != Authority Gate
// Capability is what the system CAN do; Authority is what the user PERMITS.
enum class AuthorityLevel {
	Observe,              // System can observe passive telemetry
	Read,                 // System can read channel/chat content
	Search,               // System can search global index / opportunities
	Compute,              // System can run local SLM / ranking algorithms
	Suggest,              // System can propose actions to user via UI
	AutoExecuteAllowed,   // System can execute without user prompt (whitelisted)
	RequireApproval,      // System MUST show approval modal to user before dispatch
	Deny,                 // Action is strictly prohibited by policy
};

[[nodiscard]] inline QString AuthorityLevelToString(AuthorityLevel level) {
	switch (level) {
	case AuthorityLevel::Observe: return QStringLiteral("OBSERVE");
	case AuthorityLevel::Read: return QStringLiteral("READ");
	case AuthorityLevel::Search: return QStringLiteral("SEARCH");
	case AuthorityLevel::Compute: return QStringLiteral("COMPUTE");
	case AuthorityLevel::Suggest: return QStringLiteral("SUGGEST");
	case AuthorityLevel::AutoExecuteAllowed: return QStringLiteral("AUTO_EXECUTE_ALLOWED");
	case AuthorityLevel::RequireApproval: return QStringLiteral("REQUIRE_APPROVAL");
	case AuthorityLevel::Deny: return QStringLiteral("DENY");
	}
	return QStringLiteral("DENY");
}

struct RawEvent {
	int64 sequence = 0;
	QString traceId;
	QString parentTraceId;
	EventType type = EventType::UiEvent;
	QString actor;
	QJsonObject payload;
	QString prevHash;
	QString currentHash;
	int64 timestampMs = 0;

	[[nodiscard]] QJsonObject toJson() const {
		QJsonObject obj;
		obj[QStringLiteral("sequence")] = static_cast<qint64>(sequence);
		obj[QStringLiteral("trace_id")] = traceId;
		if (!parentTraceId.isEmpty()) {
			obj[QStringLiteral("parent_trace_id")] = parentTraceId;
		}
		obj[QStringLiteral("event_type")] = EventTypeToString(type);
		obj[QStringLiteral("actor")] = actor;
		obj[QStringLiteral("payload")] = payload;
		obj[QStringLiteral("prev_hash")] = prevHash;
		obj[QStringLiteral("current_hash")] = currentHash;
		obj[QStringLiteral("timestamp_ms")] = static_cast<qint64>(timestampMs);
		return obj;
	}
};

} // namespace apo
