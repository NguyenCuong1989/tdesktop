/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "apo/apo_common.h"
#include <QtCore/QFile>
#include <QtCore/QMutex>
#include <QtCore/QString>
#include <QtCore/QHash>
#include <QtCore/QVector>
#include <memory>
#include <optional>

namespace apo {

// A persisted, account-scoped observation, never a model prior.
struct OmegaStateEvidence {
	QString traceId;
	QString accountScope;
	QJsonObject state;
	int64 timestampMs = 0;
};

class BehaviorLedger final {
public:
	static BehaviorLedger &Instance();

	void init(const QString &storagePath = QString());
	bool recordEvent(
		EventType type,
		const QString &traceId,
		const QString &actor,
		const QJsonObject &payload,
		const QString &parentTraceId = QString());

	[[nodiscard]] bool isInitialized() const;
	[[nodiscard]] QString lastHash() const;
	[[nodiscard]] int64 totalEvents() const;

	// Only hash-chain-verified native observations are eligible as S0/S1.
	[[nodiscard]] std::optional<OmegaStateEvidence> latestOmegaState(
		const QString &accountScope) const;
	[[nodiscard]] bool verifyOmegaStatePair(
		const QString &accountScope,
		const QString &previousTrace,
		const QJsonObject &previousState,
		const QString &currentTrace,
		const QJsonObject &currentState) const;

	void flush();

private:
	BehaviorLedger();
	~BehaviorLedger();

	// Caller must already hold _mutex.
	void initUnlocked(const QString &storagePath);
	void indexOmegaStateUnlocked(
		const QString &eventType,
		const QString &actor,
		const QString &traceId,
		const QJsonObject &payload,
		int64 timestampMs);

	BehaviorLedger(const BehaviorLedger &) = delete;
	BehaviorLedger &operator=(const BehaviorLedger &) = delete;

	mutable QMutex _mutex;
	QString _filePath;
	std::unique_ptr<QFile> _file;
	QString _lastHash;
	int64 _sequence = 0;
	bool _initialized = false;
	QHash<QString, QVector<OmegaStateEvidence>> _omegaHistory;
};

// Global convenience recording function
inline bool RecordBehavior(
	EventType type,
	const QString &traceId,
	const QString &actor,
	const QJsonObject &payload,
	const QString &parentTraceId = QString()) {
	return BehaviorLedger::Instance().recordEvent(type, traceId, actor, payload, parentTraceId);
}

} // namespace apo
