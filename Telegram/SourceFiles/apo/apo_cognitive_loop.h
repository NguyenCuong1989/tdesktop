/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "apo/apo_common.h"
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QByteArray>
#include <QtCore/QStringList>
#include <memory>

namespace apo {

class CognitiveLoop final : public QObject {
public:
	static CognitiveLoop &Instance();

	void start(int intervalMs = 10000);
	// Only an orderly runtime shutdown may persist deferred sensory work.
	void stop(bool recordPendingWake = false);
	[[nodiscard]] bool isRunning() const;
	void requestWake(
		const QString &source,
		const QString &sourceTraceId,
		int64 admittedAtMs,
		int64 accountUserId);

	// Single step execution of the active cognitive loop:
	// observe -> estimate -> compute -> discover -> decide -> verify -> act -> observe result -> learn
	void step();

private:
	CognitiveLoop();
	~CognitiveLoop() override;

	CognitiveLoop(const CognitiveLoop &) = delete;
	CognitiveLoop &operator=(const CognitiveLoop &) = delete;
	void stepNow();

	bool _running = false;
	std::unique_ptr<QTimer> _loopTimer;
	std::unique_ptr<QTimer> _wakeTimer;
	bool _stepActive = false;
	int _pendingWakeCount = 0;
	int64 _pendingWakeAdmittedAtMs = 0;
	int64 _pendingWakeAccountUserId = 0;
	QStringList _pendingWakeSources;
	QString _pendingWakeSourceTraceId;
	int _activeWakeCount = 0;
	int64 _activeWakeAdmittedAtMs = 0;
	int64 _activeWakeStartedAtMs = 0;
	int64 _activeWakeAccountUserId = 0;
	QStringList _activeWakeSources;
	QString _activeWakeSourceTraceId;
	int64 _cycleCount = 0;
	int64 _lastRankingAccount = 0;
	int64 _lastRemoteInspectionAccount = 0;
	int64 _lastRemoteInspectionAtMs = 0;
	int64 _remoteInspectionGeneration = 0;
	bool _remoteInspectionInFlight = false;
	bool _lastRemoteInspectionSucceeded = false;
	QString _lastRecoveryAccountScope;
	QString _lastRemoteInspectionActionId;
	QString _lastRemoteInspectionSourceTrace;
	QString _lastRemoteInspectionAccountScope;
	QByteArray _lastRankingEvidence;
};

} // namespace apo
