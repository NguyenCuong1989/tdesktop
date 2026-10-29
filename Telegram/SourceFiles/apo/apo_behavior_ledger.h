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
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace apo {

// A hash-chain-verified native read with no evidenced terminal outcome.
struct NativeReadRecoveryEvidence {
    QString actionId;
    QString accountScope;
    QString sourceTraceId;
    QString requestId;
    QString phase;
    int64 timestampMs = 0;
    bool postconditionVerified = false;
};

// A persisted, account-scoped observation, never a model prior.
struct NativeWriteRecoveryEvidence {
    QString actionId;
    QString accountScope;
    QString requestId;
    QString randomIdHex;
    QString phase;
    int64 timestampMs = 0;
    bool serverAcknowledged = false;
    bool postconditionVerified = false;
};

struct OmegaStateEvidence {
	QString traceId;
	QString accountScope;
	QJsonObject state;
	int64 timestampMs = 0;
};

struct LedgerLimits {
	int64 maxLedgerBytes = int64(512) * 1024 * 1024;
	int64 reservedStorageBytes = int64(1024) * 1024 * 1024;
	size_t maxAsyncEvents = 8192;
};

[[nodiscard]] bool LedgerStorageAllowsAppend(
	int64 currentSize,
	int64 appendSize,
	int64 availableBytes,
	const LedgerLimits &limits = LedgerLimits());

class BehaviorLedger final {
public:
	static BehaviorLedger &Instance();

	void init(
		const QString &storagePath = QString(),
		const LedgerLimits &limits = LedgerLimits());
	bool recordEvent(
		EventType type,
		const QString &traceId,
		const QString &actor,
		const QJsonObject &payload,
		const QString &parentTraceId = QString());
	bool seal(
		EventType type,
		const QString &traceId,
		const QString &actor,
		const QJsonObject &payload,
		const QString &parentTraceId = QString());
	bool recordEventAsync(
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
	[[nodiscard]] QVector<NativeReadRecoveryEvidence> unresolvedNativeReads(
		const QString &accountScope) const;
	[[nodiscard]] QVector<NativeWriteRecoveryEvidence> unresolvedNativeWrites(
		const QString &accountScope) const;
	[[nodiscard]] bool verifyOmegaStatePair(
		const QString &accountScope,
		const QString &previousTrace,
		const QJsonObject &previousState,
		const QString &currentTrace,
		const QJsonObject &currentState) const;

	void flush();

private:
	struct PendingEvent {
		EventType type = EventType::UiEvent;
		QString traceId;
		QString actor;
		QJsonObject payload;
		QString parentTraceId;
		int64 timestampMs = 0;
		bool asynchronous = false;
		std::shared_ptr<std::promise<bool>> completion;
	};

	BehaviorLedger();
	~BehaviorLedger();

	// Caller must already hold _mutex.
	void initUnlocked(const QString &storagePath);
	void indexNativeReadUnlocked(
		const QString &actor,
		const QString &traceId,
		const QJsonObject &payload,
		int64 timestampMs);
	void indexOmegaStateUnlocked(
		const QString &eventType,
		const QString &actor,
		const QString &traceId,
		const QJsonObject &payload,
		int64 timestampMs);
	void startAsyncWriter();
	void stopAsyncWriter();
	void drainAsyncWriter();
	void asyncWriterLoop();
	[[nodiscard]] bool prepareForAdmission();
	[[nodiscard]] bool enqueueSynchronous(
		PendingEvent pending,
		bool sealAdmission);
	[[nodiscard]] bool persistEvent(const PendingEvent &pending);

	BehaviorLedger(const BehaviorLedger &) = delete;
	BehaviorLedger &operator=(const BehaviorLedger &) = delete;

	mutable QMutex _mutex;
	QString _filePath;
	std::unique_ptr<QFile> _file;
	QString _lastHash;
	int64 _sequence = 0;
	bool _initialized = false;
	LedgerLimits _limits;
	QHash<QString, QVector<OmegaStateEvidence>> _omegaHistory;
	QHash<QString, NativeReadRecoveryEvidence> _nativeReadRecovery;
	QHash<QString, NativeWriteRecoveryEvidence> _nativeWriteRecovery;
	mutable std::mutex _asyncMutex;
	std::condition_variable _asyncWake;
	std::condition_variable _asyncDrained;
	std::deque<PendingEvent> _asyncQueue;
	std::thread _asyncWriter;
	size_t _pendingAsyncEvents = 0;
	bool _asyncWriting = false;
	bool _writerStopping = false;
	bool _admissionSealed = false;
	bool _admissionFailed = false;
	bool _gapQueued = false;

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

inline bool RecordBehaviorAsync(
	EventType type,
	const QString &traceId,
	const QString &actor,
	const QJsonObject &payload,
	const QString &parentTraceId = QString()) {
	return BehaviorLedger::Instance().recordEventAsync(
		type, traceId, actor, payload, parentTraceId);
}

} // namespace apo
