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

	eval.converged = s0.withinDomain() && s1.withinDomain()
		&& (std::abs(eval.deltaD) < kMaxDeltaDConvergence)
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

	// An S component outside [0,1], NaN or infinite cannot be evidence
	// for an action, even if its arithmetic accidentally fits the threshold.
	if (!candidateState.withinDomain()) {
		res.passed = false;
		res.rationale = QStringLiteral("REJECTED: INVALID_APO_STATE_DOMAIN");
		return res;
	}

	// 2. Runtime Omega(P) requires an observed, provenance-linked S0.
	// The sealed mathematical S0/S1 fixture belongs in tests only.
	const auto previous = parameters.value(QStringLiteral("apo_previous_state"));
	const auto provenance = parameters.value(QStringLiteral("apo_previous_state_source"));
	if (!previous.isObject() || provenance.toString().isEmpty()) {
		res.passed = false;
		res.rationale = QStringLiteral("UNKNOWN: APO_PREVIOUS_STATE_EVIDENCE_MISSING");
		return res;
	}
	const auto state = previous.toObject();
	const auto get = [&](const char *name, double &value) {
		const auto json = state.value(QLatin1String(name));
		if (!json.isDouble()) return false;
		value = json.toDouble();
		return std::isfinite(value);
	};
	ApoStateQuad s0;
	if (!get("C", s0.C) || !get("R", s0.R)
		|| !get("E", s0.E) || !get("B", s0.B)
		|| !s0.withinDomain()) {
		res.passed = false;
		res.rationale = QStringLiteral("UNKNOWN: INVALID_APO_PREVIOUS_STATE");
		return res;
	}
	// A nonempty provenance string is not proof. Both states must be
	// consecutive, account-scoped observations restored from the verified
	// durable ledger. The native dialogs vector cannot authorize other domains.
	const auto accountScope =
		parameters.value(QStringLiteral("apo_account_scope")).toString();
	const auto currentTrace =
		parameters.value(QStringLiteral("apo_current_state_source")).toString();
	if (actionType != QStringLiteral("dialogs.inspect_server")
		|| parameters.value(QStringLiteral("apo_state_domain")).toString()
			!= actionType) {
		res.rationale = QStringLiteral("UNKNOWN: APO_STATE_DOMAIN_UNSUPPORTED");
		return res;
	}
	if (!BehaviorLedger::Instance().verifyOmegaStatePair(
		accountScope, provenance.toString(), state,
		currentTrace, candidateState.toJson())) {
		res.rationale = QStringLiteral("UNKNOWN: APO_STATE_LEDGER_PAIR_UNVERIFIED");
		return res;
	}
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
