/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "apo/apo_behavior_ledger.h"
#include <QtCore/QStandardPaths>
#include <QtCore/QDir>
#include <QtCore/QTextStream>

namespace apo {

BehaviorLedger &BehaviorLedger::Instance() {
	static BehaviorLedger instance;
	return instance;
}

BehaviorLedger::BehaviorLedger()
	: _lastHash(QStringLiteral("0000000000000000000000000000000000000000000000000000000000000000")) {
}

BehaviorLedger::~BehaviorLedger() {
	flush();
}

void BehaviorLedger::init(const QString &storagePath) {
	QMutexLocker locker(&_mutex);
	if (_initialized) {
		return;
	}

	if (storagePath.isEmpty()) {
		const auto appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
		const auto apoDir = QDir(appData + QStringLiteral("/apo"));
		if (!apoDir.exists()) {
			apoDir.mkpath(QStringLiteral("."));
		}
		_filePath = apoDir.filePath(QStringLiteral("raw_behavior.jsonl"));
	} else {
		_filePath = storagePath;
	}

	_file = std::make_unique<QFile>(_filePath);
	if (_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
		_initialized = true;
	}
}

void BehaviorLedger::recordEvent(
	EventType type,
	const QString &traceId,
	const QString &actor,
	const QJsonObject &payload,
	const QString &parentTraceId) {
	QMutexLocker locker(&_mutex);
	if (!_initialized) {
		init();
	}

	++_sequence;
	const auto nowMs = QDateTime::currentMSecsSinceEpoch();

	RawEvent event;
	event.sequence = _sequence;
	event.traceId = traceId.isEmpty() ? QStringLiteral("trace-%1").arg(nowMs) : traceId;
	event.parentTraceId = parentTraceId;
	event.type = type;
	event.actor = actor.isEmpty() ? QStringLiteral("tdesktop.core") : actor;
	event.payload = payload;
	event.prevHash = _lastHash;
	event.timestampMs = nowMs;

	// Compute canonical SHA-256 hash over event payload + prevHash
	const auto canonicalData = QStringLiteral("%1:%2:%3:%4:%5:%6")
		.arg(event.sequence)
		.arg(event.traceId)
		.arg(EventTypeToString(event.type))
		.arg(event.actor)
		.arg(event.prevHash)
		.arg(QString::fromUtf8(QJsonDocument(event.payload).toJson(QJsonDocument::Compact)));

	const auto hashBytes = QCryptographicHash::hash(
		canonicalData.toUtf8(),
		QCryptographicHash::Sha256);
	event.currentHash = QString::fromUtf8(hashBytes.toHex());
	_lastHash = event.currentHash;

	if (_file && _file->isOpen()) {
		const auto jsonDoc = QJsonDocument(event.toJson());
		const auto line = jsonDoc.toJson(QJsonDocument::Compact) + '\n';
		_file->write(line);
		_file->flush();
	}
}

QString BehaviorLedger::lastHash() const {
	QMutexLocker locker(&_mutex);
	return _lastHash;
}

int64 BehaviorLedger::totalEvents() const {
	QMutexLocker locker(&_mutex);
	return _sequence;
}

void BehaviorLedger::flush() {
	QMutexLocker locker(&_mutex);
	if (_file && _file->isOpen()) {
		_file->flush();
	}
}

} // namespace apo
