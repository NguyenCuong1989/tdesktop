/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "apo/apo_behavior_ledger.h"
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>
#include <QtCore/QStorageInfo>
#include <QtCore/QTextStream>
#include <algorithm>
#include <cmath>

namespace apo {

namespace {
[[nodiscard]] bool IsAsyncEvidenceGap(
		const QString &actor,
		const QJsonObject &payload) {
	return actor == QStringLiteral("apo.behavior_ledger")
		&& payload.value(QStringLiteral("status")).toString()
			== QStringLiteral("ASYNC_EVIDENCE_GAP");
}

} // namespace

bool LedgerStorageAllowsAppend(
		int64 currentSize,
		int64 appendSize,
		int64 availableBytes,
		const LedgerLimits &limits) {
	return currentSize >= 0
		&& appendSize > 0
		&& availableBytes >= 0
		&& limits.maxLedgerBytes >= appendSize
		&& currentSize <= limits.maxLedgerBytes - appendSize
		&& appendSize <= availableBytes
		&& limits.reservedStorageBytes >= 0
		&& availableBytes - appendSize >= limits.reservedStorageBytes;
}

BehaviorLedger &BehaviorLedger::Instance() {
	static BehaviorLedger instance;
	return instance;
}

BehaviorLedger::BehaviorLedger()
	: _lastHash(QStringLiteral("0000000000000000000000000000000000000000000000000000000000000000")) {
}

BehaviorLedger::~BehaviorLedger() {
	stopAsyncWriter();
}

void BehaviorLedger::startAsyncWriter() {
	std::lock_guard lock(_asyncMutex);
	if (_asyncWriter.joinable() || _writerStopping) {
		return;
	}
	_asyncWriter = std::thread([this] {
		asyncWriterLoop();
	});
}

void BehaviorLedger::stopAsyncWriter() {
	{
		std::lock_guard lock(_asyncMutex);
		_writerStopping = true;
		_admissionSealed = true;
	}
	_asyncWake.notify_all();
	if (_asyncWriter.joinable()) {
		_asyncWriter.join();
	}
}

void BehaviorLedger::drainAsyncWriter() {
	std::unique_lock lock(_asyncMutex);
	_asyncDrained.wait(lock, [this] {
		return _asyncQueue.empty() && !_asyncWriting;
	});
}

void BehaviorLedger::asyncWriterLoop() {
	while (true) {
		auto pending = std::optional<PendingEvent>();
		{
			std::unique_lock lock(_asyncMutex);
			_asyncWake.wait(lock, [this] {
				return _writerStopping || !_asyncQueue.empty();
			});
			if (_asyncQueue.empty()) {
				break;
			}
			pending.emplace(std::move(_asyncQueue.front()));
			_asyncQueue.pop_front();
			_asyncWriting = true;
		}
		const auto persisted = persistEvent(*pending);
		auto failedCompletions = std::vector<
			std::shared_ptr<std::promise<bool>>>();
		{
			std::lock_guard lock(_asyncMutex);
			if (pending->asynchronous && _pendingAsyncEvents > 0) {
				--_pendingAsyncEvents;
			}
			if (!persisted) {
				_admissionFailed = true;
				for (const auto &queued : _asyncQueue) {
					if (queued.completion) {
						failedCompletions.push_back(queued.completion);
					}
				}
				_asyncQueue.clear();
				_pendingAsyncEvents = 0;
			}
		}
		if (pending->completion) {
			pending->completion->set_value(persisted);
		}
		for (const auto &completion : failedCompletions) {
			completion->set_value(false);
		}
		{
			std::lock_guard lock(_asyncMutex);
			_asyncWriting = false;
			if (_asyncQueue.empty()) {
				_asyncDrained.notify_all();
			}
		}
	}
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
    // Reconstruct write outcomes from the verified RAW chain. A write is
    // indexed only after a durable pre-dispatch intent with a random_id.
    // Missing ACK, missing observed state, or crash must not imply failure.
    if (actor == QStringLiteral("apo.executor.native")
        && phase == QStringLiteral("PREPARING_DISPATCH")
        && payload.value(QStringLiteral("native_method")).toString()
            == QStringLiteral("messages.sendMessage")) {
        const auto scope = payload.value(QStringLiteral("account_scope")).toString();
        const auto randomHex = payload.value(QStringLiteral("random_id_hex")).toString();
        if (scope.isEmpty() || randomHex.isEmpty()) return;
        _nativeWriteRecovery.insert(actionId, {
            actionId, scope, QString(), randomHex, phase, timestampMs
        });
        return;
    }
    auto write = _nativeWriteRecovery.find(actionId);
    if (write != _nativeWriteRecovery.end()) {
        if (actor != QStringLiteral("apo.executor.native")) return;
        const auto observedRandom =
            payload.value(QStringLiteral("random_id_hex")).toString();
        // A write cannot gain ACK, observed-state or CLOSED evidence from
        // a record lacking its persisted MTProto random_id correlation.
        const auto positiveEvidence = phase == QStringLiteral("ACKNOWLEDGED")
            || phase == QStringLiteral("STATE_CONFIRMED")
            || phase == QStringLiteral("CLOSED");
        if ((!observedRandom.isEmpty() && observedRandom != write->randomIdHex)
            || (positiveEvidence && observedRandom.isEmpty())) {
            write->phase = observedRandom.isEmpty()
                ? QStringLiteral("CORRELATION_MISSING")
                : QStringLiteral("CORRELATION_MISMATCH");
            write->timestampMs = timestampMs;
            return;
        }
        const auto requestId = payload.value(QStringLiteral("request_id"));
        const auto validRequestId = requestId.isDouble()
            && std::isfinite(requestId.toDouble())
            && requestId.toDouble() > 0;
        // All positive transitions must refer to the same native request.
        // random_id identifies the message, but not the RPC that observed it.
        if (positiveEvidence && !validRequestId) {
            write->phase = phase == QStringLiteral("ACKNOWLEDGED")
                ? QStringLiteral("ACK_REQUEST_ID_MISSING")
                : QStringLiteral("POSTCONDITION_REQUEST_ID_MISSING");
            write->timestampMs = timestampMs;
            return;
        }
        if (validRequestId) {
            const auto observedRequestId =
                QString::number(static_cast<int64>(requestId.toDouble()));
            if (!write->requestId.isEmpty()
                && write->requestId != observedRequestId) {
                write->phase = QStringLiteral("REQUEST_ID_MISMATCH");
                write->timestampMs = timestampMs;
                return;
            }
            write->requestId = observedRequestId;
        }
        const auto outcome = payload.value(QStringLiteral("result")).toString();
        if (phase == QStringLiteral("ACKNOWLEDGED")
            && outcome == QStringLiteral("SERVER_RESPONSE_RECEIVED")) {
            write->serverAcknowledged = true;
        }
        // A success label alone is not native-state evidence. Require the
        // exact message ID and observed outgoing projection emitted by the
        // real send/readback path before permitting CLOSED on replay.
        if (phase == QStringLiteral("STATE_CONFIRMED")
            && outcome == QStringLiteral("NATIVE_SEND_POSTCONDITION_MATCHED")) {
            const auto messageId = payload.value(QStringLiteral("server_message_id"));
            if (messageId.isDouble() && messageId.toDouble() > 0
                && payload.value(QStringLiteral("native_projection_present"))
                    == QJsonValue(true)) {
                write->postconditionVerified = true;
            }
        }
        if (phase == QStringLiteral("CLOSED")) {
            if (write->serverAcknowledged && write->postconditionVerified
                && outcome == QStringLiteral("SUCCESS_NATIVE_SEND_OBSERVED")) {
                _nativeWriteRecovery.erase(write);
            } else {
                write->phase = QStringLiteral("CLOSED_WITHOUT_COMPLETE_EVIDENCE");
                write->timestampMs = timestampMs;
            }
            return;
        }
        // A post-dispatch error and an unresolved write remain visible
        // across restarts. Never automatically issue a second send.
        if (!phase.isEmpty()) {
            write->phase = phase;
            write->timestampMs = timestampMs;
        }
        return;
    }
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
    // A generic native RPC error after dispatch is not proof that the
    // server did not apply the request. Preserve the verified intent and
    // request ID for reconciliation, including after a process restart.
    // Pre-dispatch rejections still close without creating a false unknown.
    if (phase == QStringLiteral("FAILED")
        && actor == QStringLiteral("apo.executor.native")
        && payload.value(QStringLiteral("result")).toString()
            == QStringLiteral("NATIVE_RPC_ERROR")
        && (it->phase == QStringLiteral("DISPATCHED")
            || it->phase == QStringLiteral("ACKNOWLEDGED")
            || it->phase == QStringLiteral("RECONCILING")
            || it->phase == QStringLiteral("UNKNOWN_OUTCOME"))) {
        const auto req = payload.value(QStringLiteral("request_id"));
        if (req.isDouble() && req.toDouble() > 0) {
            it->requestId = QString::number(static_cast<int64>(req.toDouble()));
        }
        it->phase = QStringLiteral("UNKNOWN_OUTCOME");
        it->timestampMs = timestampMs;
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

void BehaviorLedger::init(
		const QString &storagePath,
		const LedgerLimits &limits) {
	{
		std::lock_guard lock(_asyncMutex);
		if (_admissionFailed || _admissionSealed || _writerStopping) {
			return;
		}
	}
	{
		QMutexLocker locker(&_mutex);
		_limits = limits;
		initUnlocked(storagePath);
	}
	if (isInitialized()) {
		startAsyncWriter();
	}
}

void BehaviorLedger::initUnlocked(const QString &storagePath) {
	if (_initialized) {
		return;
	}
	_file.reset();
	_omegaHistory.clear();
	_nativeReadRecovery.clear();
	_nativeWriteRecovery.clear();

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
	auto restoredGap = false;
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
			const auto hashVersionValue = json.value(QStringLiteral("hash_version"));
			const auto hashVersionNumber = hashVersionValue.isUndefined()
				? 1.0 : hashVersionValue.toDouble(-1.0);
			if (!std::isfinite(hashVersionNumber)
				|| std::floor(hashVersionNumber) != hashVersionNumber) {
				return;
			}
			const auto hashVersion = static_cast<int>(hashVersionNumber);
			if (hashVersion != 1 && hashVersion != 2) {
				return;
			}
			const auto timestampValue = json.value(QStringLiteral("timestamp_ms"));
			const auto timestampNumber = timestampValue.toDouble(-1.0);
			if (!timestampValue.isDouble() || !std::isfinite(timestampNumber)
				|| timestampNumber < 0.0
				|| timestampNumber > 9007199254740991.0
				|| std::floor(timestampNumber) != timestampNumber) {
				return;
			}
			const auto timestampMs = static_cast<int64>(timestampNumber);
			const auto prevHash = json.value(QStringLiteral("prev_hash")).toString();
			const auto eventHash = json.value(QStringLiteral("current_hash")).toString();
			if (sequence != restoredSequence + 1 || prevHash != restoredHash || eventHash.size() != 64) {
				return;
			}
			const auto payloadJson = QString::fromUtf8(QJsonDocument(
				json.value(QStringLiteral("payload")).toObject()).toJson(QJsonDocument::Compact));
			const auto canonicalData = (hashVersion == 1)
				? QStringLiteral("%1:%2:%3:%4:%5:%6")
					.arg(sequence)
					.arg(json.value(QStringLiteral("trace_id")).toString())
					.arg(json.value(QStringLiteral("event_type")).toString())
					.arg(json.value(QStringLiteral("actor")).toString())
					.arg(prevHash)
					.arg(payloadJson)
				: QStringLiteral("%1:%2:%3:%4:%5:%6:%7:%8:%9")
					.arg(hashVersion)
					.arg(sequence)
					.arg(json.value(QStringLiteral("trace_id")).toString())
					.arg(json.value(QStringLiteral("parent_trace_id")).toString())
					.arg(json.value(QStringLiteral("event_type")).toString())
					.arg(json.value(QStringLiteral("actor")).toString())
					.arg(timestampMs)
					.arg(prevHash)
					.arg(payloadJson);
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
				timestampMs);
			indexOmegaStateUnlocked(
				json.value(QStringLiteral("event_type")).toString(),
				json.value(QStringLiteral("actor")).toString(),
				json.value(QStringLiteral("trace_id")).toString(),
				json.value(QStringLiteral("payload")).toObject(),
				timestampMs);
			if (IsAsyncEvidenceGap(
				json.value(QStringLiteral("actor")).toString(),
				json.value(QStringLiteral("payload")).toObject())) {
				restoredGap = true;
			}
			restoredHash = eventHash;
			restoredSequence = sequence;
		}
	}
	_sequence = restoredSequence;
	_lastHash = restoredHash;
	if (restoredGap) {
		return;
	}

	_file = std::make_unique<QFile>(_filePath);
	if (_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
		_initialized = true;
	}
}

bool BehaviorLedger::prepareForAdmission() {
	{
		QMutexLocker locker(&_mutex);
		// A failed explicit init(path) must never silently fall back to the
		// default AppData ledger. That would make a corrupted ledger appear
		// writable and break the provenance of the entire event chain.
		if (!_initialized && _filePath.isEmpty()) {
			initUnlocked(QString());
		}
		if (!_initialized) {
			return false;
		}
	}
	startAsyncWriter();
	return true;
}

bool BehaviorLedger::enqueueSynchronous(
		PendingEvent pending,
		bool sealAdmission) {
	if (!prepareForAdmission()) {
		return false;
	}
	auto completion = std::make_shared<std::promise<bool>>();
	auto result = completion->get_future();
	pending.completion = completion;
	{
		std::lock_guard lock(_asyncMutex);
		if (_writerStopping || _admissionFailed || _admissionSealed) {
			return false;
		}
		pending.timestampMs = QDateTime::currentMSecsSinceEpoch();
		if (sealAdmission) {
			_admissionSealed = true;
		}
		_asyncQueue.push_back(std::move(pending));
	}
	_asyncWake.notify_one();
	return result.get();
}

bool BehaviorLedger::persistEvent(const PendingEvent &pending) {
	QMutexLocker locker(&_mutex);
	if (!_initialized) {
		return false;
	}

	RawEvent event;
	event.sequence = _sequence + 1;
	event.traceId = pending.traceId.isEmpty()
		? QStringLiteral("trace-%1-%2")
			.arg(event.sequence)
			.arg(pending.timestampMs)
		: pending.traceId;
	event.parentTraceId = pending.parentTraceId;
	event.type = pending.type;
	event.actor = pending.actor.isEmpty()
		? QStringLiteral("tdesktop.core")
		: pending.actor;
	event.payload = pending.payload;
	event.prevHash = _lastHash;
	event.timestampMs = pending.timestampMs;

	// Version 2 also binds event timing and trace parentage. Existing v1
	// records remain readable so a verified ledger can upgrade in place.
	const auto canonicalData = QStringLiteral("%1:%2:%3:%4:%5:%6:%7:%8:%9")
		.arg(event.hashVersion)
		.arg(event.sequence)
		.arg(event.traceId)
		.arg(event.parentTraceId)
		.arg(EventTypeToString(event.type))
		.arg(event.actor)
		.arg(event.timestampMs)
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
	auto storage = QStorageInfo(QFileInfo(_filePath).absolutePath());
	storage.refresh();
	const auto availableBytes = (storage.isValid() && storage.isReady())
		? storage.bytesAvailable()
		: int64(-1);
	if (!LedgerStorageAllowsAppend(
		previousSize,
		line.size(),
		availableBytes,
		_limits)) {
		_file->close();
		_initialized = false;
		return false;
	}
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

bool BehaviorLedger::recordEvent(
		EventType type,
		const QString &traceId,
		const QString &actor,
		const QJsonObject &payload,
		const QString &parentTraceId) {
	auto pending = PendingEvent();
	pending.type = type;
	pending.traceId = traceId;
	pending.actor = actor;
	pending.payload = payload;
	pending.parentTraceId = parentTraceId;
	return enqueueSynchronous(std::move(pending), false);
}

bool BehaviorLedger::recordEventAsync(
		EventType type,
		const QString &traceId,
		const QString &actor,
		const QJsonObject &payload,
		const QString &parentTraceId) {
	if (!prepareForAdmission()) {
		return false;
	}
	auto accepted = false;
	auto wakeWriter = false;
	{
		std::lock_guard lock(_asyncMutex);
		if (_writerStopping || _admissionFailed || _admissionSealed) {
			return false;
		}
		const auto timestampMs = QDateTime::currentMSecsSinceEpoch();
		if (_pendingAsyncEvents >= _limits.maxAsyncEvents) {
			_admissionFailed = true;
			if (!_gapQueued) {
				auto gap = PendingEvent();
				gap.type = EventType::StateReduce;
				gap.traceId = QStringLiteral("ledger-async-gap");
				gap.actor = QStringLiteral("apo.behavior_ledger");
				gap.payload[QStringLiteral("status")] =
					QStringLiteral("ASYNC_EVIDENCE_GAP");
				gap.payload[QStringLiteral("reason")] =
					QStringLiteral("ASYNC_QUEUE_CAPACITY_EXCEEDED");
				gap.payload[QStringLiteral("attempted_event_type")] =
					EventTypeToString(type);
				gap.payload[QStringLiteral("attempted_trace_supplied")] =
					!traceId.isEmpty();
				gap.timestampMs = timestampMs;
				_asyncQueue.push_back(std::move(gap));
				_gapQueued = true;
				wakeWriter = true;
			}
		} else {
			auto pending = PendingEvent();
			pending.type = type;
			pending.traceId = traceId;
			pending.actor = actor;
			pending.payload = payload;
			pending.parentTraceId = parentTraceId;
			pending.timestampMs = timestampMs;
			pending.asynchronous = true;
			_asyncQueue.push_back(std::move(pending));
			++_pendingAsyncEvents;
			accepted = true;
			wakeWriter = true;
		}
	}
	if (wakeWriter) {
		_asyncWake.notify_one();
	}
	return accepted;
}

bool BehaviorLedger::seal(
		EventType type,
		const QString &traceId,
		const QString &actor,
		const QJsonObject &payload,
		const QString &parentTraceId) {
	auto pending = PendingEvent();
	pending.type = type;
	pending.traceId = traceId;
	pending.actor = actor;
	pending.payload = payload;
	pending.parentTraceId = parentTraceId;
	return enqueueSynchronous(std::move(pending), true);
}

bool BehaviorLedger::isInitialized() const {
	auto initialized = false;
	{
		QMutexLocker locker(&_mutex);
		initialized = _initialized;
	}
	if (!initialized) {
		return false;
	}
	std::lock_guard lock(_asyncMutex);
	return !_writerStopping && !_admissionFailed && !_admissionSealed;
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

QVector<NativeWriteRecoveryEvidence> BehaviorLedger::unresolvedNativeWrites(
    const QString &accountScope) const {
    QMutexLocker locker(&_mutex);
    QVector<NativeWriteRecoveryEvidence> result;
    if (!_initialized || accountScope.isEmpty()) return result;
    for (auto it = _nativeWriteRecovery.cbegin();
            it != _nativeWriteRecovery.cend(); ++it) {
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
	drainAsyncWriter();
}

} // namespace apo
