/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "apo/apo_common.h"
#include <QtCore/QString>
#include <QtCore/QMutex>
#include <QtCore/QJsonObject>

namespace apo {

struct SelfState {
	int64 userId = 0;
	QString username;
	QString phone;
	QString firstName;
	QString lastName;
	bool isPremium = false;
	int unreadTotal = 0;
	int activeDialogsCount = 0;
	// External wallet TON is distinct from Telegram's in-app TON credits.
	int64 tonBalanceNano = 0;
	bool tonWalletBalanceKnown = false;
	int64 telegramTonWhole = 0;
	int64 telegramTonNano = 0;
	bool telegramTonKnown = false;
	int64 starsBalance = 0;
	int64 starsBalanceNano = 0;
	bool starsBalanceKnown = false;
	int64 lastUpdatedMs = 0;

	[[nodiscard]] QJsonObject toJson() const {
		QJsonObject obj;
		obj[QStringLiteral("user_id")] = static_cast<qint64>(userId);
		obj[QStringLiteral("username")] = username;
		obj[QStringLiteral("phone")] = phone;
		obj[QStringLiteral("first_name")] = firstName;
		obj[QStringLiteral("last_name")] = lastName;
		obj[QStringLiteral("is_premium")] = isPremium;
		obj[QStringLiteral("unread_total")] = unreadTotal;
		obj[QStringLiteral("active_dialogs_count")] = activeDialogsCount;
		obj[QStringLiteral("ton_wallet_balance_known")] = tonWalletBalanceKnown;
		if (tonWalletBalanceKnown) {
			obj[QStringLiteral("ton_balance_nano")] = static_cast<qint64>(tonBalanceNano);
		}
		obj[QStringLiteral("telegram_ton_balance_known")] = telegramTonKnown;
		if (telegramTonKnown) {
			obj[QStringLiteral("telegram_ton_whole")] = static_cast<qint64>(telegramTonWhole);
			obj[QStringLiteral("telegram_ton_fractional_nano")] = static_cast<qint64>(telegramTonNano);
		}
		obj[QStringLiteral("stars_balance_known")] = starsBalanceKnown;
		if (starsBalanceKnown) {
			obj[QStringLiteral("stars_balance")] = static_cast<qint64>(starsBalance);
			obj[QStringLiteral("stars_fractional_nano")] = static_cast<qint64>(starsBalanceNano);
		}
		obj[QStringLiteral("last_updated_ms")] = static_cast<qint64>(lastUpdatedMs);
		return obj;
	}
};

class StateBridge final {
public:
	static StateBridge &Instance();

	void updateUserProfile(
		int64 userId,
		const QString &username,
		const QString &phone,
		const QString &firstName,
		const QString &lastName,
		bool isPremium);

	// Accept unread state only from the currently active account.
	void updateUnreadCount(int64 accountUserId, int unreadTotal);
	void updateBalances(int64 accountUserId, int64 tonBalanceNano, int64 starsBalance);
	void updateStarsCredits(int64 accountUserId, int64 whole, int64 nano);
	void updateTelegramTonCredits(int64 accountUserId, int64 whole, int64 nano);

	[[nodiscard]] SelfState currentSelf() const;

private:
	StateBridge() = default;
	~StateBridge() = default;

	StateBridge(const StateBridge &) = delete;
	StateBridge &operator=(const StateBridge &) = delete;

	mutable QMutex _mutex;
	SelfState _self;
};

} // namespace apo
