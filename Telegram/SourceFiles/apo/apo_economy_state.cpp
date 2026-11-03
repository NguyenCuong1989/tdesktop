/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "apo/apo_economy_state.h"
#include "apo/apo_behavior_ledger.h"
#include <algorithm>

namespace apo {

EconomyEngine &EconomyEngine::Instance() {
	static EconomyEngine instance;
	return instance;
}

EconomyEngine::EconomyEngine() {
	_state.U = 1.0;
	_state.E = 1.0;
	_state.Q = 1.0;
	_state.P = 1.0;
	_state.I = 1.0;
	_state.T = 1.0;
}

void EconomyEngine::updateState(
	double deltaAcquisition,
	double deltaChurn,
	double engagement,
	double quality,
	const RevenueBreakdown &rev,
	double operationalCost) {
	QMutexLocker locker(&_mutex);

	// Dynamics: U_{t+1} = U_t + A_t - Ch_t
	_state.U = std::max(0.0, _state.U + deltaAcquisition - deltaChurn);
	_state.E = std::clamp(engagement, 0.0, 1.0);
	_state.Q = std::clamp(quality, 0.0, 1.0);

	const auto totalRev = rev.total();
	_state.C = operationalCost;
	_state.Profit = totalRev - operationalCost;

	_state.S += rev.rStars;
	_state.X_eco += rev.rEco;

	QJsonObject payload;
	payload[QStringLiteral("state")] = _state.toJson();
	payload[QStringLiteral("revenue")] = rev.toJson();

	RecordBehavior(
		EventType::StateReduce,
		QString(),
		QStringLiteral("apo.economy"),
		payload);
}

void EconomyEngine::applyTrustFeedback(
	double valueDelivered,
	double provenanceChainValid,
	double scaleQuality,
	double adversarialPenalty,
	double abusePenalty,
	double shadowRisk) {
	QMutexLocker locker(&_mutex);

	// Weight parameters for trust evolution:
	// \beta_1 = 0.05, \beta_2 = 0.05, \beta_3 = 0.02
	// \beta_4 = 0.15, \beta_5 = 0.25, \beta_6 = 0.20
	constexpr double beta1 = 0.05;
	constexpr double beta2 = 0.05;
	constexpr double beta3 = 0.02;
	constexpr double beta4 = 0.15;
	constexpr double beta5 = 0.25;
	constexpr double beta6 = 0.20;

	const double positiveDelta = (beta1 * valueDelivered)
		+ (beta2 * provenanceChainValid)
		+ (beta3 * scaleQuality);

	const double negativeDelta = (beta4 * adversarialPenalty)
		+ (beta5 * abusePenalty)
		+ (beta6 * shadowRisk);

	const double prevTrust = _state.T;
	_state.T = std::clamp(_state.T + positiveDelta - negativeDelta, 0.0, 1.0);

	QJsonObject payload;
	payload[QStringLiteral("prev_trust")] = prevTrust;
	payload[QStringLiteral("new_trust")] = _state.T;
	payload[QStringLiteral("positive_delta")] = positiveDelta;
	payload[QStringLiteral("negative_delta")] = negativeDelta;

	RecordBehavior(
		EventType::StateReduce,
		QString(),
		QStringLiteral("apo.economy.trust"),
		payload);
}

EconomyStateVector EconomyEngine::currentState() const {
	QMutexLocker locker(&_mutex);
	return _state;
}

double EconomyEngine::currentTrust() const {
	QMutexLocker locker(&_mutex);
	return _state.T;
}

} // namespace apo
