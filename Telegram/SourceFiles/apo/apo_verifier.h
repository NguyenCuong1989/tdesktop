/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "apo/apo_common.h"
#include <QtCore/QString>
#include <QtCore/QJsonObject>

namespace apo {

struct VerificationResult {
	bool passed = false;
	QString rationale;
	QString proofHash;
	double expectedValue = 0.0;
	double trustMargin = 0.0;

	[[nodiscard]] QJsonObject toJson() const {
		QJsonObject obj;
		obj[QStringLiteral("passed")] = passed;
		obj[QStringLiteral("rationale")] = rationale;
		obj[QStringLiteral("proof_hash")] = proofHash;
		obj[QStringLiteral("expected_value")] = expectedValue;
		obj[QStringLiteral("trust_margin")] = trustMargin;
		return obj;
	}
};

class ApoVerifier final {
public:
	static ApoVerifier &Instance();

	// Full mathematical APO verification of a proposed action:
	// a_t \in Capabilities_t \cap Permissions_t \cap AuthorizedActions_t
	[[nodiscard]] VerificationResult verifyAction(
		const QString &actionType,
		const QString &target,
		const QJsonObject &parameters,
		double estimatedValue,
		double estimatedCost);

private:
	ApoVerifier() = default;
	~ApoVerifier() = default;

	ApoVerifier(const ApoVerifier &) = delete;
	ApoVerifier &operator=(const ApoVerifier &) = delete;
};

} // namespace apo
