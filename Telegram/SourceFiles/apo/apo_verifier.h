/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "apo/apo_common.h"
#include <QtCore/QString>
#include <QtCore/QJsonObject>
#include <cmath>

namespace apo {

// Canonical APΩ State Quad: S_k = (C_k, R_k, E_k, B_k) \in [0, 1]^4
struct ApoStateQuad {
	double C = 0.0; // Capacity / Cost (0.0 to 1.0)
	double R = 0.0; // Risk / Resistance (0.0 to 1.0)
	double E = 1.0; // Efficiency / Energy (0.0 to 1.0)
	double B = 1.0; // Boundary / Baseline (0.0 to 1.0)

	[[nodiscard]] bool withinDomain() const {
		const auto valid = [](double value) {
			return std::isfinite(value) && value >= 0.0 && value <= 1.0;
		};
		return valid(C) && valid(R) && valid(E) && valid(B);
	}

	[[nodiscard]] double computeD() const {
		return (C + R + E + B) / 4.0;
	}

	[[nodiscard]] double computeDiff() const {
		return (E + (1.0 - R)) / 2.0;
	}

	[[nodiscard]] double computeV() const {
		const double D = computeD();
		const double diff = computeDiff();
		return std::max(0.0, 1.0 - D * (1.0 - diff));
	}

	[[nodiscard]] QJsonObject toJson() const {
		QJsonObject obj;
		obj[QStringLiteral("C")] = C;
		obj[QStringLiteral("R")] = R;
		obj[QStringLiteral("E")] = E;
		obj[QStringLiteral("B")] = B;
		obj[QStringLiteral("D")] = computeD();
		obj[QStringLiteral("diff")] = computeDiff();
		obj[QStringLiteral("V")] = computeV();
		return obj;
	}
};

struct OmegaEvaluation {
	ApoStateQuad S0;
	ApoStateQuad S1;
	double D0 = 0.0;
	double D1 = 0.0;
	double diff0 = 0.0;
	double diff1 = 0.0;
	double V0 = 0.0;
	double V1 = 0.0;
	double deltaD = 0.0;
	double deltaV = 0.0;
	bool converged = false;

	[[nodiscard]] QJsonObject toJson() const {
		QJsonObject obj;
		obj[QStringLiteral("S0")] = S0.toJson();
		obj[QStringLiteral("S1")] = S1.toJson();
		obj[QStringLiteral("delta_D")] = deltaD;
		obj[QStringLiteral("delta_V")] = deltaV;
		obj[QStringLiteral("converged")] = converged;
		return obj;
	}
};

struct VerificationResult {
	bool passed = false;
	QString rationale;
	QString proofHash;
	OmegaEvaluation omega;

	[[nodiscard]] QJsonObject toJson() const {
		QJsonObject obj;
		obj[QStringLiteral("passed")] = passed;
		obj[QStringLiteral("rationale")] = rationale;
		obj[QStringLiteral("proof_hash")] = proofHash;
		obj[QStringLiteral("omega")] = omega.toJson();
		return obj;
	}
};

class ApoVerifier final {
public:
	static constexpr double kMinimumTruthThresholdV = 0.9800;
	static constexpr double kMaxDeltaDConvergence = 0.005;

	static ApoVerifier &Instance();

	// Canonical evaluation of Omega(P)
	[[nodiscard]] OmegaEvaluation evaluateOmega(
		const ApoStateQuad &s0,
		const ApoStateQuad &s1) const;

	// Full mathematical APO verification of a proposed action:
	// a_t \in Capabilities_t \cap Permissions_t \cap AuthorizedActions_t
	// conditioned upon mathematical convergence: |Delta D| < 0.005 & V1 >= 0.9800
	[[nodiscard]] VerificationResult verifyAction(
		const QString &actionType,
		const QString &target,
		const QJsonObject &parameters,
		const ApoStateQuad &candidateState);

private:
	ApoVerifier() = default;
	~ApoVerifier() = default;

	ApoVerifier(const ApoVerifier &) = delete;
	ApoVerifier &operator=(const ApoVerifier &) = delete;
};

} // namespace apo
