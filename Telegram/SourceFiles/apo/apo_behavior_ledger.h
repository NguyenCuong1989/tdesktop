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
#include <memory>

namespace apo {

class BehaviorLedger final {
public:
	static BehaviorLedger &Instance();

	void init(const QString &storagePath = QString());
	void recordEvent(
		EventType type,
		const QString &traceId,
		const QString &actor,
		const QJsonObject &payload,
		const QString &parentTraceId = QString());

	[[nodiscard]] QString lastHash() const;
	[[nodiscard]] int64 totalEvents() const;

	void flush();

private:
	BehaviorLedger();
	~BehaviorLedger();

	BehaviorLedger(const BehaviorLedger &) = delete;
	BehaviorLedger &operator=(const BehaviorLedger &) = delete;

	mutable QMutex _mutex;
	QString _filePath;
	std::unique_ptr<QFile> _file;
	QString _lastHash;
	int64 _sequence = 0;
	bool _initialized = false;
};

// Global convenience recording function
inline void RecordBehavior(
	EventType type,
	const QString &traceId,
	const QString &actor,
	const QJsonObject &payload,
	const QString &parentTraceId = QString()) {
	BehaviorLedger::Instance().recordEvent(type, traceId, actor, payload, parentTraceId);
}

} // namespace apo
