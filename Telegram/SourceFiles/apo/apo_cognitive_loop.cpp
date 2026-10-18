/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "apo/apo_cognitive_loop.h"
#include "apo/apo_state_bridge.h"
#include "apo/apo_economy_state.h"
#include "apo/apo_verifier.h"
#include "apo/apo_executor.h"
#include "apo/apo_behavior_ledger.h"
#include <QtCore/QDateTime>

namespace apo {

CognitiveLoop &CognitiveLoop::Instance() {
	static CognitiveLoop instance;
	return instance;
}

CognitiveLoop::CognitiveLoop() {
}

CognitiveLoop::~CognitiveLoop() {
	stop();
}

void CognitiveLoop::start(int intervalMs) {
	if (_running) {
		return;
	}
	_running = true;

	_loopTimer = std::make_unique<QTimer>(this);
	connect(_loopTimer.get(), &QTimer::timeout, this, [this] {
		step();
	});
	_loopTimer->start(intervalMs);

	QJsonObject payload;
	payload[QStringLiteral("status")] = QStringLiteral("COGNITIVE_LOOP_STARTED");
	payload[QStringLiteral("interval_ms")] = intervalMs;
	RecordBehavior(
		EventType::StateReduce,
		QStringLiteral("cognitive.start"),
		QStringLiteral("apo.cognitive_loop"),
		payload);
}

void CognitiveLoop::stop() {
	if (!_running) {
		return;
	}
	_running = false;
	if (_loopTimer) {
		_loopTimer->stop();
		_loopTimer.reset();
	}
}

bool CognitiveLoop::isRunning() const {
	return _running;
}

void CognitiveLoop::step() {
	if (!_running) {
		return;
	}
	++_cycleCount;
	const auto traceId = QStringLiteral("cycle-%1-%2")
		.arg(_cycleCount)
		.arg(QDateTime::currentMSecsSinceEpoch());

	// 1. OBSERVE: Fetch native session state
	const auto self = StateBridge::Instance().currentSelf();

	// 2. ESTIMATE: Evaluate economic state vector X_t
	const auto ecoState = EconomyEngine::Instance().currentState();

	// 3. COMPUTE: Calculate active capital and unread pressure
	const double activeCap = ecoState.activeCapital();
	const int unreadCount = self.unreadTotal;

	QJsonObject computePayload;
	computePayload[QStringLiteral("cycle")] = static_cast<qint64>(_cycleCount);
	computePayload[QStringLiteral("has_user_identity")] = (self.userId != 0);
	computePayload[QStringLiteral("unread")] = unreadCount;
	computePayload[QStringLiteral("active_capital")] = activeCap;
	computePayload[QStringLiteral("trust")] = ecoState.T;

	RecordBehavior(
		EventType::Computation,
		traceId,
		QStringLiteral("apo.cognitive.evaluator"),
		computePayload);

	// 4. DISCOVER: Look for candidate proactive opportunities
	if (unreadCount > 0) {
		// Found opportunity to draft smart summaries / prioritize unread dialogs
		const auto candidateAction = QStringLiteral("compute.draft_reply");
		const auto targetChat = QStringLiteral("unread_inbox");

		// 5. DECIDE & 6. VERIFY: Evaluate dynamic candidate state through canonical APO verifier
		const double dynamicCost = std::clamp(unreadCount * 0.005, 0.0, 0.05);
		const double dynamicRisk = std::clamp(0.04 + (unreadCount > 10 ? 0.02 : 0.0), 0.01, 0.08);
		const double dynamicEfficiency = std::clamp(0.95 + (ecoState.E * 0.05), 0.9, 1.0);
		const double dynamicBoundary = std::clamp(0.95 + (ecoState.T * 0.045), 0.95, 0.995);

		const ApoStateQuad candidateState{
			dynamicCost,
			dynamicRisk,
			dynamicEfficiency,
			dynamicBoundary
		};

		// The executor runs the Omega(P) verifier at the only action boundary.
		// A caller cannot advance an action by omitting mathematical verification.
		// 7. ACT: Submit action to ActionExecutor (respects Auto vs Require Approval)
			QJsonObject actionParams;
			actionParams[QStringLiteral("unread_count")] = unreadCount;
			actionParams[QStringLiteral("priority")] = QStringLiteral("HIGH");

			ActionExecutor::Instance().submitAction(
				candidateAction,
				targetChat,
				actionParams,
				QStringLiteral("Autonomous proactive triage for incoming chats"),
				candidateState,
				[](bool success) {
					// 8. OBSERVE RESULT & 9. LEARN: Reinforce trust capital
					if (success) {
						EconomyEngine::Instance().applyTrustFeedback(
							1.0,  // Value delivered
							1.0,  // Provenance verified
							0.5,  // Scale quality
							0.0,  // No adversarial penalty
							0.0,  // No abuse
							0.0   // No shadow risk
						);
					}
				}
			);
	}
}

} // namespace apo
