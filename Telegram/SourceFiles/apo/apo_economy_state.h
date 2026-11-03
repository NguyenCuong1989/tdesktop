/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "apo/apo_common.h"
#include <QtCore/QJsonObject>
#include <QtCore/QMutex>
#include <cmath>

namespace apo {

// Canonical Economic & State Vector:
// X_t = (U_t, E_t, Q_t, P_t, I_t, S_t, G_t, B_t, X_t^{eco}, C_t, T_t, \Pi_t)
struct EconomyStateVector {
	double U = 0.0;          // Users / Audience base (Active subscribers/contacts)
	double E = 0.0;          // Engagement rate (Reads, forwards, reactions)
	double Q = 0.0;          // Quality coefficient (0.0 to 1.0)
	double P = 0.0;          // Pricing / Monetization power
	double I = 0.0;          // Inventory (Ad slots, product catalog, services)
	double S = 0.0;          // Telegram Stars balance / Velocity
	double G = 0.0;          // Gifts / Collectibles valuation
	double B = 0.0;          // Business tier / Verified presence level
	double X_eco = 0.0;      // TON Ecosystem liquidity / On-chain assets
	double C = 0.0;          // Total operational costs
	double T = 1.0;          // Trust capital (0.0 to 1.0)
	double Profit = 0.0;     // Net profit \Pi_t = R_t - C_t

	// Derived metrics
	[[nodiscard]] double activeCapital() const {
		return U * E * Q;    // AC_t = U_t * E_t * Q_t
	}

	[[nodiscard]] QJsonObject toJson() const {
		QJsonObject obj;
		obj[QStringLiteral("U")] = U;
		obj[QStringLiteral("E")] = E;
		obj[QStringLiteral("Q")] = Q;
		obj[QStringLiteral("P")] = P;
		obj[QStringLiteral("I")] = I;
		obj[QStringLiteral("S")] = S;
		obj[QStringLiteral("G")] = G;
		obj[QStringLiteral("B")] = B;
		obj[QStringLiteral("X_eco")] = X_eco;
		obj[QStringLiteral("C")] = C;
		obj[QStringLiteral("T")] = T;
		obj[QStringLiteral("Profit")] = Profit;
		obj[QStringLiteral("AC")] = activeCapital();
		return obj;
	}
};

struct RevenueBreakdown {
	double rPremium = 0.0;
	double rAds = 0.0;
	double rStars = 0.0;
	double rGate = 0.0;
	double rEco = 0.0;
	double rOther = 0.0;

	[[nodiscard]] double total() const {
		return rPremium + rAds + rStars + rGate + rEco + rOther;
	}

	[[nodiscard]] QJsonObject toJson() const {
		QJsonObject obj;
		obj[QStringLiteral("r_prem")] = rPremium;
		obj[QStringLiteral("r_ads")] = rAds;
		obj[QStringLiteral("r_stars")] = rStars;
		obj[QStringLiteral("r_gate")] = rGate;
		obj[QStringLiteral("r_eco")] = rEco;
		obj[QStringLiteral("r_other")] = rOther;
		obj[QStringLiteral("total_revenue")] = total();
		return obj;
	}
};

class EconomyEngine final {
public:
	static EconomyEngine &Instance();

	void updateState(
		double deltaAcquisition,
		double deltaChurn,
		double engagement,
		double quality,
		const RevenueBreakdown &rev,
		double operationalCost);

	// Trust update:
	// T_{t+1} = T_t + \beta_1 V_t + \beta_2 Pr_t + \beta_3 Sc_t - \beta_4 Ad_t - \beta_5 Ab_t - \beta_6 Sh_t
	void applyTrustFeedback(
		double valueDelivered,
		double provenanceChainValid,
		double scaleQuality,
		double adversarialPenalty,
		double abusePenalty,
		double shadowRisk);

	[[nodiscard]] EconomyStateVector currentState() const;
	[[nodiscard]] double currentTrust() const;

private:
	EconomyEngine();
	~EconomyEngine() = default;

	EconomyEngine(const EconomyEngine &) = delete;
	EconomyEngine &operator=(const EconomyEngine &) = delete;

	mutable QMutex _mutex;
	EconomyStateVector _state;
};

} // namespace apo
