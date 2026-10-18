/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "apo/apo_state_bridge.h"
#include "apo/apo_behavior_ledger.h"

namespace apo {

StateBridge &StateBridge::Instance() {
	static StateBridge instance;
	return instance;
}

void StateBridge::updateUserProfile(
	int64 userId,
	const QString &username,
	const QString &phone,
	const QString &firstName,
	const QString &lastName,
	bool isPremium) {
	QMutexLocker locker(&_mutex);
	_self.userId = userId;
	_self.username = username;
	_self.phone = phone;
	_self.firstName = firstName;
	_self.lastName = lastName;
	_self.isPremium = isPremium;
	_self.lastUpdatedMs = QDateTime::currentMSecsSinceEpoch();

	// Account profile fields (especially phone) must not enter raw telemetry.
	QJsonObject safeProfile;
	safeProfile[QStringLiteral("profile_updated")] = true;
	safeProfile[QStringLiteral("has_identity")] = (userId != 0);
	safeProfile[QStringLiteral("is_premium")] = isPremium;
	RecordBehavior(
		EventType::StateReduce,
		QString(),
		QStringLiteral("apo.state_bridge"),
		safeProfile);
}

void StateBridge::updateUnreadCount(int unreadTotal) {
	QMutexLocker locker(&_mutex);
	_self.unreadTotal = unreadTotal;
	_self.lastUpdatedMs = QDateTime::currentMSecsSinceEpoch();

	QJsonObject payload;
	payload[QStringLiteral("unread_total")] = unreadTotal;
	RecordBehavior(
		EventType::StateReduce,
		QString(),
		QStringLiteral("apo.state_bridge.unread"),
		payload);
}

void StateBridge::updateBalances(int64 tonBalanceNano, int64 starsBalance) {
	QMutexLocker locker(&_mutex);
	_self.tonBalanceNano = tonBalanceNano;
	_self.starsBalance = starsBalance;
	_self.lastUpdatedMs = QDateTime::currentMSecsSinceEpoch();

	QJsonObject payload;
	payload[QStringLiteral("ton_balance_nano")] = static_cast<qint64>(tonBalanceNano);
	payload[QStringLiteral("stars_balance")] = static_cast<qint64>(starsBalance);
	RecordBehavior(
		EventType::StateReduce,
		QString(),
		QStringLiteral("apo.state_bridge.balances"),
		payload);
}

SelfState StateBridge::currentSelf() const {
	QMutexLocker locker(&_mutex);
	return _self;
}

} // namespace apo
