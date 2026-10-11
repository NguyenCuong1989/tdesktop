/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "apo/apo_verifier.h"
#include "apo/apo_authority.h"
#include "apo/apo_economy_state.h"
#include "apo/apo_behavior_ledger.h"
#include <QtCore/QCryptographicHash>
#include <QtCore/QJsonDocument>
#include <QtCore/QDateTime>

namespace apo {

ApoVerifier &ApoVerifier::Instance() {
	static ApoVerifier instance;
	return instance;
}

VerificationResult ApoVerifier::verifyAction(
	const QString &actionType,
	const QString &target,
	const QJsonObject &parameters,
	double estimatedValue,
	double estimatedCost) {
	VerificationResult res;
	res.expectedValue = estimatedValue - estimatedCost;

	// 1. Authority Gate Check: a_t \in AuthorizedActions_t
	const auto authLevel = AuthorityManager::Instance().getActionAuthority(actionType);
	if (authLevel == AuthorityLevel::Deny) {
		res.passed = false;
		res.rationale = QStringLiteral("DENIED: Action explicitly forbidden by policy.");
		return res;
	}

	// 2. Trust Capital Gate Check: T_t >= T_min (0.35)
	const double currentTrust = EconomyEngine::Instance().currentTrust();
	res.trustMargin = currentTrust;
	if (currentTrust < 0.35) {
		res.passed = false;
		res.rationale = QStringLiteral("DENIED: System trust capital below safety threshold (%1 < 0.35)")
			.arg(currentTrust);
		return res;
	}

	// 3. Margin / Cost Gate Check
	if (res.expectedValue < -5.0) { // Unbounded loss prevention
		res.passed = false;
		res.rationale = QStringLiteral("DENIED: Negative margin violates economic preservation constraint.");
		return res;
	}

	// 4. Action Passed APO Verification: Generate Proof Hash
	res.passed = true;
	res.rationale = QStringLiteral("VERIFIED: Action satisfies Capabilities, Authority, and Trust constraints.");

	const auto nowMs = QDateTime::currentMSecsSinceEpoch();
	const auto proofPayload = QStringLiteral("%1:%2:%3:%4:%5")
		.arg(actionType)
		.arg(target)
		.arg(res.expectedValue)
		.arg(currentTrust)
		.arg(nowMs);

	const auto hashBytes = QCryptographicHash::hash(
		proofPayload.toUtf8(),
		QCryptographicHash::Sha256);
	res.proofHash = QString::fromUtf8(hashBytes.toHex());

	// Record APO_VERIFY event in the Raw Behavior Ledger
	QJsonObject logPayload = res.toJson();
	logPayload[QStringLiteral("action_type")] = actionType;
	logPayload[QStringLiteral("target")] = target;

	RecordBehavior(
		EventType::ApoVerify,
		res.proofHash.left(16),
		QStringLiteral("apo.verifier"),
		logPayload);

	return res;
}

} // namespace apo
