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
#include <algorithm>

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

void BehaviorLedger::indexNativeReadUnlocked(
    const QString &actor,
    const QString &traceId,
    const QJsonObject &payload,
    int64 timestampMs) {
    const auto actionId = payload.value(QStringLiteral("action_id")).toString();
    if (actionId.isEmpty()) return;
    const auto declared = payload.value(QStringLiteral("phase")).toString();
    const auto transition = payload.value(QStringLiteral("new_phase")).toString();
    const auto phase = transition.isEmpty() ? declared : transition;
    if (actor == QStringLiteral("apo.agent_core")
        && phase == QStringLiteral("PLANNED")
        && payload.value(QStringLiteral("action_type")).toString()
            == QStringLiteral("dialogs.inspect_server")) {
        const auto scope = payload.value(QStringLiteral("account_scope")).toString();
        if (scope.isEmpty()) return;
        _nativeReadRecovery.insert(actionId, {
            actionId, scope,
            payload.value(QStringLiteral("source_trace_id")).toString(),
            QString(), phase, timestampMs
        });
        return;
    }
    auto it = _nativeReadRecovery.find(actionId);
    if (it == _nativeReadRecovery.end()) return;
    // A terminal success without a preceding observed native postcondition
    // is not evidence and must remain unresolved across restarts.
    if (phase == QStringLiteral("STATE_CONFIRMED")
        && actor == QStringLiteral("apo.executor.native")
        && payload.value(QStringLiteral("result")).toString()
            == QStringLiteral("NATIVE_READ_POSTCONDITION_MATCHED")) {
        it->postconditionVerified = true;
    }
    if (phase == QStringLiteral("CLOSED")) {
        if (actor == QStringLiteral("apo.executor.native")
            && it->postconditionVerified
            && payload.value(QStringLiteral("result")).toString()
                == QStringLiteral("SUCCESS_READ_ONLY")) {
            _nativeReadRecovery.erase(it);
        } else {
            it->phase = QStringLiteral("CLOSED_WITHOUT_POSTCONDITION_EVIDENCE");
            it->timestampMs = timestampMs;
        }
        return;
    }
    if (phase == QStringLiteral("FAILED")
        || phase == QStringLiteral("REJECTED")
        || phase == QStringLiteral("BLOCKED")
        || phase == QStringLiteral("DENIED")) {
        _nativeReadRecovery.erase(it);
        return;
    }
    if (phase.isEmpty()) return;
    const auto req = payload.value(QStringLiteral("request_id"));
    if (req.isDouble() && req.toDouble() > 0) {
        it->requestId = QString::number(static_cast<int64>(req.toDouble()));
    }
    it->phase = phase;
    it->timestampMs = timestampMs;
}

void BehaviorLedger::indexOmegaStateUnlocked(
	const QString &eventType,
	const QString &actor,
	const QString &traceId,
	const QJsonObject &payload,
	int64 timestampMs) {
	// Only an actual, hash-chain-persisted native observation may enter
	// the state history. A model prior or arbitrary caller-supplied S0
	// cannot be promoted into evidence by naming a trace ID.
	if (eventType != QStringLiteral("RAW.STATE_REDUCE")
		|| actor != QStringLiteral("apo.cognitive.omega_measurement")
		|| payload.value(QStringLiteral("kind")).toString()
			!= QStringLiteral("NATIVE_DIALOGS_STATE")
		|| !payload.value(QStringLiteral("measurement_complete")).toBool()
		|| payload.value(QStringLiteral("source")).toString()
			!= QStringLiteral("Data::Session.chatsList")
		|| traceId.isEmpty() || timestampMs <= 0) {
		return;
	}
	const auto scope = payload.value(QStringLiteral("account_scope")).toString();
	const auto value = payload.value(QStringLiteral("state"));
	if (scope.isEmpty() || !value.isObject()) {
		return;
	}
	const auto state = value.toObject();
	for (const auto name : { "C", "R", "E", "B" }) {
		const auto component = state.value(QLatin1String(name));
		if (!component.isDouble() || !std::isfinite(component.toDouble())
			|| component.toDouble() < 0.0 || component.toDouble() > 1.0) {
			return;
		}
	}
	// Recompute all four operands from the persisted native observation
	// contract. The sealed fixture values can never enter this index.
	const auto inspected = payload.value(QStringLiteral("inspected_rows")).toInt(-1);
	const auto total = payload.value(QStringLiteral("total_loaded_rows")).toInt(-1);
	const auto known = payload.value(QStringLiteral("known_rows")).toInt(-1);
	const auto measuredAt = static_cast<int64>(
		payload.value(QStringLiteral("measured_at_ms")).toDouble());
	if (!payload.value(QStringLiteral("source_loaded")).toBool()
		|| inspected <= 0 || inspected > 512 || total != inspected
		|| known <= 0 || known > inspected || measuredAt <= 0
		|| measuredAt > timestampMs || timestampMs - measuredAt > 5000) {
		return;
	}
	const auto matches = [&](const char *key, double expected) {
		return std::abs(state.value(QLatin1String(key)).toDouble() - expected)
			< 1e-12;
	};
	if (!matches("C", double(inspected) / 512.0)
		|| !matches("R", double(inspected - known) / double(inspected))
		|| !matches("E", double(known) / double(inspected))
		|| !matches("B", 1.0)) {
		return;
	}
	auto &history = _omegaHistory[scope];
	if (!history.isEmpty() && history.constLast().timestampMs >= timestampMs) {
		return; // A clock rollback cannot silently authorize a transition.
	}
	history.push_back({ traceId, scope, state, timestampMs });
	if (history.size() > 2) {
		history.remove(0);
	}
}

void BehaviorLedger::init(const QString &storagePath) {
	QMutexLocker locker(&_mutex);
	initUnlocked(storagePath);
}

void BehaviorLedger::initUnlocked(const QString &storagePath) {
	if (_initialized) {
		return;
	}
	_omegaHistory.clear();
	_nativeReadRecovery.clear();

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
			// Rebuild only from records whose hash and chain were verified.
			indexNativeReadUnlocked(
				json.value(QStringLiteral("actor")).toString(),
				json.value(QStringLiteral("trace_id")).toString(),
				json.value(QStringLiteral("payload")).toObject(),
				static_cast<int64>(json.value(QStringLiteral("timestamp_ms")).toDouble()));
			indexOmegaStateUnlocked(
				json.value(QStringLiteral("event_type")).toString(),
				json.value(QStringLiteral("actor")).toString(),
				json.value(QStringLiteral("trace_id")).toString(),
				json.value(QStringLiteral("payload")).toObject(),
				static_cast<int64>(json.value(QStringLiteral("timestamp_ms")).toDouble()));
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
	// A failed explicit init(path) must never silently fall back to the
	// default AppData ledger. That would make a corrupted ledger appear
	// writable and break the provenance of the entire event chain.
	if (!_initialized && _filePath.isEmpty()) {
		initUnlocked(QString());
	}
	if (!_initialized) {
		// Do not create unverifiable in-memory events when persistence failed.
		return false;
	}

	const auto nowMs = QDateTime::currentMSecsSinceEpoch();

	RawEvent event;
	event.sequence = _sequence + 1;
	event.traceId = traceId.isEmpty()
		? QStringLiteral("trace-%1-%2").arg(event.sequence).arg(nowMs)
		: traceId;
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
	indexNativeReadUnlocked(event.actor, event.traceId, event.payload, event.timestampMs);
	indexOmegaStateUnlocked(
		EventTypeToString(event.type),
		event.actor,
		event.traceId,
		event.payload,
		event.timestampMs);
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

QVector<NativeReadRecoveryEvidence> BehaviorLedger::unresolvedNativeReads(
    const QString &accountScope) const {
    QMutexLocker locker(&_mutex);
    QVector<NativeReadRecoveryEvidence> result;
    if (!_initialized || accountScope.isEmpty()) return result;
    for (auto it = _nativeReadRecovery.cbegin(); it != _nativeReadRecovery.cend(); ++it) {
        if (it.value().accountScope == accountScope) result.push_back(it.value());
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        if (a.timestampMs != b.timestampMs) return a.timestampMs < b.timestampMs;
        return a.actionId < b.actionId;
    });
    return result;
}

std::optional<OmegaStateEvidence> BehaviorLedger::latestOmegaState(
	const QString &accountScope) const {
	QMutexLocker locker(&_mutex);
	if (!_initialized) return std::nullopt;
	const auto it = _omegaHistory.constFind(accountScope);
	if (it == _omegaHistory.cend() || it.value().isEmpty()) {
		return std::nullopt;
	}
	return it.value().constLast();
}

bool BehaviorLedger::verifyOmegaStatePair(
	const QString &accountScope,
	const QString &previousTrace,
	const QJsonObject &previousState,
	const QString &currentTrace,
	const QJsonObject &currentState) const {
	QMutexLocker locker(&_mutex);
	if (!_initialized || accountScope.isEmpty() || previousTrace.isEmpty()
		|| currentTrace.isEmpty() || previousTrace == currentTrace) {
		return false;
	}
	const auto it = _omegaHistory.constFind(accountScope);
	if (it == _omegaHistory.cend() || it.value().size() != 2) {
		return false;
	}
	const auto &before = it.value().at(0);
	const auto &after = it.value().at(1);
	return before.traceId == previousTrace
		&& after.traceId == currentTrace
		&& before.state == previousState
		&& after.state == currentState
		&& before.timestampMs < after.timestampMs;
}

void BehaviorLedger::flush() {
	QMutexLocker locker(&_mutex);
	if (_file && _file->isOpen()) {
		_file->flush();
	}
}

} // namespace apo
