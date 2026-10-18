/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "apo/apo_behavior_ledger.h"
#include <QtCore/QStandardPaths>
#include <QtCore/QDir>
#include <QtCore/QTextStream>
#include <cmath>

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
	initUnlocked(storagePath);
}

void BehaviorLedger::initUnlocked(const QString &storagePath) {
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

	// Restore and verify the persisted chain before appending.
	int64 restoredSequence = 0;
	auto restoredHash = QStringLiteral("0000000000000000000000000000000000000000000000000000000000000000");
	if (QFile::exists(_filePath)) {
		QFile reader(_filePath);
		if (!reader.open(QIODevice::ReadOnly)) {
			return;
		}
		while (!reader.atEnd()) {
			const auto line = reader.readLine(1024 * 1024);
			if (!line.endsWith('\n')) {
				// Truncated crash tail or record over the bounded line size.
				return;
			}
			QJsonParseError parseError;
			const auto document = QJsonDocument::fromJson(line, &parseError);
			if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
				return;
			}
			const auto json = document.object();
			const auto sequenceValue = json.value(QStringLiteral("sequence"));
			if (!sequenceValue.isDouble() || !json.value(QStringLiteral("payload")).isObject()) {
				return;
			}
			// Qt 5 has no QJsonValue::toInteger (introduced in Qt 6).
			// Reject non-integral or imprecisely representable JSON sequence values.
			const auto sequenceNumber = sequenceValue.toDouble(-1);
			if (!std::isfinite(sequenceNumber)
				|| sequenceNumber < 1.0
				|| sequenceNumber > 9007199254740991.0
				|| std::floor(sequenceNumber) != sequenceNumber) {
				return;
			}
			const auto sequence = static_cast<int64>(sequenceNumber);
			const auto prevHash = json.value(QStringLiteral("prev_hash")).toString();
			const auto eventHash = json.value(QStringLiteral("current_hash")).toString();
			if (sequence != restoredSequence + 1 || prevHash != restoredHash || eventHash.size() != 64) {
				return;
			}
			const auto canonicalData = QStringLiteral("%1:%2:%3:%4:%5:%6")
				.arg(sequence)
				.arg(json.value(QStringLiteral("trace_id")).toString())
				.arg(json.value(QStringLiteral("event_type")).toString())
				.arg(json.value(QStringLiteral("actor")).toString())
				.arg(prevHash)
				.arg(QString::fromUtf8(QJsonDocument(
					json.value(QStringLiteral("payload")).toObject()).toJson(QJsonDocument::Compact)));
			const auto expectedHash = QString::fromUtf8(QCryptographicHash::hash(
				canonicalData.toUtf8(), QCryptographicHash::Sha256).toHex());
			if (eventHash != expectedHash) {
				// Preserve the corrupted ledger untouched for forensic audit.
				return;
			}
			restoredHash = eventHash;
			restoredSequence = sequence;
		}
	}

	_file = std::make_unique<QFile>(_filePath);
	if (_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
		_sequence = restoredSequence;
		_lastHash = restoredHash;
		_initialized = true;
	}
}

bool BehaviorLedger::recordEvent(
	EventType type,
	const QString &traceId,
	const QString &actor,
	const QJsonObject &payload,
	const QString &parentTraceId) {
	QMutexLocker locker(&_mutex);
	if (!_initialized) {
		initUnlocked(QString());
	}
	if (!_initialized) {
		// Do not create unverifiable in-memory events when persistence failed.
		return false;
	}

	const auto nowMs = QDateTime::currentMSecsSinceEpoch();

	RawEvent event;
	event.sequence = _sequence + 1;
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

	if (!_file || !_file->isOpen()) {
		_initialized = false;
		return false;
	}
	const auto previousSize = _file->size();
	const auto line = QJsonDocument(event.toJson()).toJson(QJsonDocument::Compact) + '\n';
	if (_file->write(line) != line.size() || !_file->flush()) {
		// Do not advance state when persistence fails.
		_file->resize(previousSize);
		_file->close();
		_initialized = false;
		return false;
	}
	_sequence = event.sequence;
	_lastHash = event.currentHash;
	return true;
}

bool BehaviorLedger::isInitialized() const {
	QMutexLocker locker(&_mutex);
	return _initialized;
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
