/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "apo/apo_verifier.h"
#include "apo/apo_authority.h"
#include "apo/apo_behavior_ledger.h"
#include <QtCore/QCryptographicHash>
#include <QtCore/QJsonDocument>
#include <QtCore/QDateTime>

namespace apo {

ApoVerifier &ApoVerifier::Instance() {
	static ApoVerifier instance;
	return instance;
}

OmegaEvaluation ApoVerifier::evaluateOmega(
	const ApoStateQuad &s0,
	const ApoStateQuad &s1) const {
	OmegaEvaluation eval;
	eval.S0 = s0;
	eval.S1 = s1;

	eval.D0 = s0.computeD();
	eval.D1 = s1.computeD();

	eval.diff0 = s0.computeDiff();
	eval.diff1 = s1.computeDiff();

	eval.V0 = s0.computeV();
	eval.V1 = s1.computeV();

	eval.deltaD = eval.D1 - eval.D0;
	eval.deltaV = eval.V1 - eval.V0;

	eval.converged = (std::abs(eval.deltaD) < kMaxDeltaDConvergence)
		&& (eval.V1 >= kMinimumTruthThresholdV);

	return eval;
}

VerificationResult ApoVerifier::verifyAction(
	const QString &actionType,
	const QString &target,
	const QJsonObject &parameters,
	const ApoStateQuad &candidateState) {
	VerificationResult res;

	// 1. Authority Gate Check: a_t \in AuthorizedActions_t
	const auto authLevel = AuthorityManager::Instance().getActionAuthority(actionType);
	if (authLevel == AuthorityLevel::Deny) {
		res.passed = false;
		res.rationale = QStringLiteral("DENIED: Action explicitly forbidden by policy.");
		return res;
	}

	// 2. Canonical Omega(P) Convergence Gate
	// Baseline state: S0 = (C=0.0, R=0.07, E=1.0, B=0.985)
	const ApoStateQuad s0{ 0.0, 0.07, 1.0, 0.985 };
	res.omega = evaluateOmega(s0, candidateState);

	if (!res.omega.converged) {
		res.passed = false;
		res.rationale = QStringLiteral(
			"REJECTED: APO convergence failed. delta_D=%1 (need < 0.005), V1=%2 (need >= 0.9800)")
			.arg(res.omega.deltaD)
			.arg(res.omega.V1);
		return res;
	}

	// 3. Action Passed Canonical Verification: Generate SHA-256 Proof Hash
	res.passed = true;
	res.rationale = QStringLiteral("VERIFIED: Action satisfies Authority and Canonical Omega(P) convergence.");

	const auto nowMs = QDateTime::currentMSecsSinceEpoch();
	const auto proofPayload = QStringLiteral("%1:%2:%3:%4:%5")
		.arg(actionType)
		.arg(target)
		.arg(res.omega.deltaD)
		.arg(res.omega.V1)
		.arg(nowMs);

	const auto hashBytes = QCryptographicHash::hash(
		proofPayload.toUtf8(),
		QCryptographicHash::Sha256);
	res.proofHash = QString::fromUtf8(hashBytes.toHex());

	// Record APO_VERIFY event in the Raw Behavior Ledger
	QJsonObject logPayload = res.toJson();
	logPayload[QStringLiteral("action_type")] = actionType;
	logPayload[QStringLiteral("target_present")] = !target.isEmpty();

	if (!RecordBehavior(
		EventType::ApoVerify,
		res.proofHash.left(16),
		QStringLiteral("apo.verifier"),
		logPayload)) {
		res.passed = false;
		res.proofHash.clear();
		res.rationale = QStringLiteral("REJECTED: audit ledger persistence unavailable.");
	}

	return res;
}

} // namespace apo
