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

// Native body perception assembled from real Telegram chat-list rows.
// It is a typed observation, not an Ω(P) gate or authorization for action.
struct NativeBodyProjection {
    int64 accountUserId = 0;
    QString accountScope;
    QString sourceTraceId;
    int64 measuredAtMs = 0;
    int totalLoadedRows = 0;
    int inspectedRows = 0;
    int historyRows = 0;
    int userDialogs = 0;
    int basicGroups = 0;
    int supergroups = 0;
    int broadcastChannels = 0;
    int unclassifiedPeers = 0;
    int knownUnreadRows = 0;
    int unreadDialogs = 0;
    int mentionedDialogs = 0;
    bool sourceLoaded = false;
    bool complete = false;

    [[nodiscard]] bool structurallyValid() const {
        if (accountUserId <= 0 || accountScope.isEmpty() || sourceTraceId.isEmpty()
            || measuredAtMs <= 0 || totalLoadedRows < 0 || inspectedRows < 0
            || inspectedRows > 512 || inspectedRows > totalLoadedRows
            || historyRows < 0 || historyRows > inspectedRows
            || userDialogs < 0 || basicGroups < 0 || supergroups < 0
            || broadcastChannels < 0 || unclassifiedPeers < 0
            || knownUnreadRows < 0 || knownUnreadRows > historyRows
            || unreadDialogs < 0 || unreadDialogs > knownUnreadRows
            || mentionedDialogs < 0 || mentionedDialogs > knownUnreadRows) {
            return false;
        }
        if (userDialogs + basicGroups + supergroups + broadcastChannels
            + unclassifiedPeers != historyRows) return false;
        return !complete || (sourceLoaded
            && totalLoadedRows == inspectedRows && unclassifiedPeers == 0);
    }

    [[nodiscard]] QJsonObject toJson() const {
        QJsonObject result;
        result[QStringLiteral("kind")] = QStringLiteral("NATIVE_BODY_PROJECTION");
        result[QStringLiteral("source")] = QStringLiteral("Data::Session.chatsList/History::peer");
        result[QStringLiteral("account_scope")] = accountScope;
        result[QStringLiteral("source_trace_id")] = sourceTraceId;
        result[QStringLiteral("measured_at_ms")] = static_cast<qint64>(measuredAtMs);
        result[QStringLiteral("total_loaded_rows")] = totalLoadedRows;
        result[QStringLiteral("inspected_rows")] = inspectedRows;
        result[QStringLiteral("history_rows")] = historyRows;
        result[QStringLiteral("user_dialogs")] = userDialogs;
        result[QStringLiteral("basic_groups")] = basicGroups;
        result[QStringLiteral("supergroups")] = supergroups;
        result[QStringLiteral("broadcast_channels")] = broadcastChannels;
        result[QStringLiteral("unclassified_peers")] = unclassifiedPeers;
        result[QStringLiteral("known_unread_rows")] = knownUnreadRows;
        result[QStringLiteral("unread_dialogs")] = unreadDialogs;
        result[QStringLiteral("mentioned_dialogs")] = mentionedDialogs;
        result[QStringLiteral("source_loaded")] = sourceLoaded;
        result[QStringLiteral("measurement_complete")] = complete;
        result[QStringLiteral("result")] = QStringLiteral("OBSERVED_ONLY");
        result[QStringLiteral("native_dispatch")] = false;
        return result;
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

	// Account-bound, ledger-backed native body observation for existing APΩ modules.
	[[nodiscard]] bool updateNativeBodyProjection(const NativeBodyProjection &projection);
	[[nodiscard]] std::optional<NativeBodyProjection> currentNativeBodyProjection(
		int64 accountUserId) const;
	[[nodiscard]] SelfState currentSelf() const;

private:
	StateBridge() = default;
	~StateBridge() = default;

	StateBridge(const StateBridge &) = delete;
	StateBridge &operator=(const StateBridge &) = delete;

	mutable QMutex _mutex;
	SelfState _self;
	std::optional<NativeBodyProjection> _bodyProjection;
};

} // namespace apo
