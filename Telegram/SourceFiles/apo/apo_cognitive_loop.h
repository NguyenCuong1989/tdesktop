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
#include <memory>

namespace apo {

class CognitiveLoop final : public QObject {
public:
	static CognitiveLoop &Instance();

	void start(int intervalMs = 10000);
	void stop();
	[[nodiscard]] bool isRunning() const;

	// Single step execution of the active cognitive loop:
	// observe -> estimate -> compute -> discover -> decide -> verify -> act -> observe result -> learn
	void step();

private:
	CognitiveLoop();
	~CognitiveLoop() override;

	CognitiveLoop(const CognitiveLoop &) = delete;
	CognitiveLoop &operator=(const CognitiveLoop &) = delete;

	bool _running = false;
	std::unique_ptr<QTimer> _loopTimer;
	int64 _cycleCount = 0;
	int64 _lastRankingAccount = 0;
	int64 _lastRemoteInspectionAccount = 0;
	int64 _lastRemoteInspectionAtMs = 0;
	int64 _remoteInspectionGeneration = 0;
	bool _remoteInspectionInFlight = false;
	bool _lastRemoteInspectionSucceeded = false;
	QByteArray _lastRankingEvidence;
};

} // namespace apo
