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
	if (_self.userId != userId) {
		// A session switch/logout must not leak the previous account's state.
		_self = SelfState();
	}
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

void StateBridge::updateUnreadCount(int64 accountUserId, int unreadTotal) {
	QMutexLocker locker(&_mutex);
	if (!accountUserId || _self.userId != accountUserId
		|| unreadTotal < 0 || _self.unreadTotal == unreadTotal) {
		return;
	}
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

void StateBridge::updateBalances(int64 accountUserId, int64 tonBalanceNano, int64 starsBalance) {
	QMutexLocker locker(&_mutex);
	if (!accountUserId || _self.userId != accountUserId) {
		return;
	}
	_self.tonBalanceNano = tonBalanceNano;
	_self.tonWalletBalanceKnown = true;
	_self.starsBalance = starsBalance;
	_self.starsBalanceNano = 0;
	_self.starsBalanceKnown = true;
	_self.lastUpdatedMs = QDateTime::currentMSecsSinceEpoch();

	QJsonObject payload;
	payload[QStringLiteral("ton_wallet_balance_updated")] = true;
	payload[QStringLiteral("stars_balance_updated")] = true;
	RecordBehavior(
		EventType::StateReduce,
		QString(),
		QStringLiteral("apo.state_bridge.balances"),
		payload);
}

void StateBridge::updateStarsCredits(
	int64 accountUserId,
	int64 whole,
	int64 nano) {
	QMutexLocker locker(&_mutex);
	if (!accountUserId || _self.userId != accountUserId
		|| nano < 0 || nano >= 1000000000) {
		return;
	}
	if (_self.starsBalanceKnown
		&& _self.starsBalance == whole
		&& _self.starsBalanceNano == nano) {
		return;
	}
	_self.starsBalance = whole;
	_self.starsBalanceNano = nano;
	_self.starsBalanceKnown = true;
	_self.lastUpdatedMs = QDateTime::currentMSecsSinceEpoch();
	QJsonObject payload;
	payload[QStringLiteral("source")] = QStringLiteral("telegram_credits_stars");
	payload[QStringLiteral("balance_loaded")] = true;
	RecordBehavior(EventType::StateReduce, QString(),
		QStringLiteral("apo.state_bridge.stars"), payload);
}

void StateBridge::updateTelegramTonCredits(
	int64 accountUserId,
	int64 whole,
	int64 nano) {
	QMutexLocker locker(&_mutex);
	if (!accountUserId || _self.userId != accountUserId
		|| nano < 0 || nano >= 1000000000) {
		return;
	}
	if (_self.telegramTonKnown
		&& _self.telegramTonWhole == whole
		&& _self.telegramTonNano == nano) {
		return;
	}
	_self.telegramTonWhole = whole;
	_self.telegramTonNano = nano;
	_self.telegramTonKnown = true;
	_self.lastUpdatedMs = QDateTime::currentMSecsSinceEpoch();
	QJsonObject payload;
	payload[QStringLiteral("source")] = QStringLiteral("telegram_credits_ton");
	payload[QStringLiteral("balance_loaded")] = true;
	RecordBehavior(EventType::StateReduce, QString(),
		QStringLiteral("apo.state_bridge.telegram_ton"), payload);
}

SelfState StateBridge::currentSelf() const {
	QMutexLocker locker(&_mutex);
	return _self;
}

} // namespace apo
