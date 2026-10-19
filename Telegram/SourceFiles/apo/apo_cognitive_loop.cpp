/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "apo/apo_cognitive_loop.h"
#include "apo/apo_state_bridge.h"
#include "apo/apo_economy_state.h"
#include "apo/apo_verifier.h"
#include "apo/apo_executor.h"
#include "apo/apo_behavior_ledger.h"
#include "core/application.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "dialogs/dialogs_main_list.h"
#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <algorithm>
#include <vector>

namespace apo {

CognitiveLoop &CognitiveLoop::Instance() {
	static CognitiveLoop instance;
	return instance;
}

CognitiveLoop::CognitiveLoop() {
}

CognitiveLoop::~CognitiveLoop() {
	stop();
}

void CognitiveLoop::start(int intervalMs) {
	if (_running) {
		return;
	}
	_running = true;

	_loopTimer = std::make_unique<QTimer>(this);
	connect(_loopTimer.get(), &QTimer::timeout, this, [this] {
		step();
	});
	_loopTimer->start(intervalMs);

	QJsonObject payload;
	payload[QStringLiteral("status")] = QStringLiteral("COGNITIVE_LOOP_STARTED");
	payload[QStringLiteral("interval_ms")] = intervalMs;
	RecordBehavior(
		EventType::StateReduce,
		QStringLiteral("cognitive.start"),
		QStringLiteral("apo.cognitive_loop"),
		payload);
}

void CognitiveLoop::stop() {
	if (!_running) {
		return;
	}
	_running = false;
	_lastRankingAccount = 0;
	_lastRankingEvidence.clear();
	if (_loopTimer) {
		_loopTimer->stop();
		_loopTimer.reset();
	}
}

bool CognitiveLoop::isRunning() const {
	return _running;
}

void CognitiveLoop::step() {
	if (!_running) {
		return;
	}
	++_cycleCount;
	const auto traceId = QStringLiteral("cycle-%1-%2")
		.arg(_cycleCount)
		.arg(QDateTime::currentMSecsSinceEpoch());

	// 1. OBSERVE: Fetch native session state
	const auto self = StateBridge::Instance().currentSelf();

	// 2. ESTIMATE: Evaluate economic state vector X_t
	const auto ecoState = EconomyEngine::Instance().currentState();

	// 3. COMPUTE: Calculate active capital and unread pressure
	const double activeCap = ecoState.activeCapital();
	const int unreadCount = self.unreadTotal;

	QJsonObject computePayload;
	computePayload[QStringLiteral("cycle")] = static_cast<qint64>(_cycleCount);
	computePayload[QStringLiteral("has_user_identity")] = (self.userId != 0);
	computePayload[QStringLiteral("unread")] = unreadCount;
	computePayload[QStringLiteral("active_capital")] = activeCap;
	computePayload[QStringLiteral("trust")] = ecoState.T;

	RecordBehavior(
		EventType::Computation,
		traceId,
		QStringLiteral("apo.cognitive.evaluator"),
		computePayload);

	// 4. OBSERVE native dialogue context rather than interpreting the
	// whole-account unread badge as an addressable chat or a customer request.
	// A nonzero badge alone cannot authorize "compute.draft_reply": there
	// is no message context, recipient, actual draft or measurable cost.
	if (self.userId == 0 || unreadCount <= 0) {
		return;
	}
	const auto session = Core::App().activeAccount().maybeSession();
	if (!session || session->userId().bare != self.userId) {
		return;
	}
	const auto list = session->data().chatsList();
	constexpr auto kMaxInspected = 512;
	constexpr auto kTop = 5;
	struct RankedUnread {
		int listPosition = 0;
		int unread = 0;
		int unmuted = 0;
		int mentions = 0;
	};
	std::vector<RankedUnread> ranked;
	const auto &rows = list->indexed()->all();
	const auto limit = std::min(int(rows.size()), kMaxInspected);
	int knownRows = 0;
	int historyRows = 0;
	auto rowIterator = rows.cbegin();
	for (auto i = 0; i != limit; ++i, ++rowIterator) {
		const auto row = *rowIterator;
		if (!row->history()) {
			continue; // A folder is not an independent conversation.
		}
		++historyRows;
		const auto state = row->entry()->chatListUnreadState();
		if (!state.known) {
			continue; // Unknown is not zero unread.
		}
		++knownRows;
		const auto unread = std::max(state.messages, 0);
		const auto unmuted = std::max(unread - state.messagesMuted, 0);
		const auto mentions = std::max(state.mentions, 0);
		if (unread > 0 || mentions > 0) {
			ranked.push_back({ i, unread, unmuted, mentions });
		}
	}
	// Lexicographic priority uses measured Telegram values, not invented
	// business value: mentions, nonmuted unread, total unread, recency.
	std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
		if (a.mentions != b.mentions) return a.mentions > b.mentions;
		if (a.unmuted != b.unmuted) return a.unmuted > b.unmuted;
		if (a.unread != b.unread) return a.unread > b.unread;
		return a.listPosition < b.listPosition;
	});
	QJsonArray top;
	for (auto i = 0, count = std::min(int(ranked.size()), kTop);
			i != count; ++i) {
		const auto &item = ranked[i];
		QJsonObject one;
		one[QStringLiteral("priority_rank")] = i + 1;
		one[QStringLiteral("loaded_list_position")] = item.listPosition;
		one[QStringLiteral("unread_messages")] = item.unread;
		one[QStringLiteral("unmuted_messages")] = item.unmuted;
		one[QStringLiteral("mentions")] = item.mentions;
		top.append(one);
	}
	QJsonObject observation;
	observation[QStringLiteral("kind")] = QStringLiteral("NATIVE_UNREAD_PRIORITY_OBSERVATION");
	observation[QStringLiteral("source")] = QStringLiteral("Data::Session.chatsList");
	observation[QStringLiteral("source_loaded")] = list->loaded();
	observation[QStringLiteral("total_loaded_rows")] = int(rows.size());
	observation[QStringLiteral("inspected_rows")] = limit;
	observation[QStringLiteral("history_rows")] = historyRows;
	observation[QStringLiteral("known_unread_rows")] = knownRows;
	observation[QStringLiteral("unread_ranked_rows")] = int(ranked.size());
	observation[QStringLiteral("full_loaded_list_covered")] =
		list->loaded() && (limit == int(rows.size()));
	observation[QStringLiteral("top")] = top;
	observation[QStringLiteral("result")] = QStringLiteral("OBSERVED_ONLY");
	observation[QStringLiteral("native_dispatch")] = false;
	// No peer IDs, chat names or message text leave the native process.
	const auto snapshot = QJsonDocument(observation).toJson(QJsonDocument::Compact);
	if (_lastRankingAccount != self.userId
		|| _lastRankingEvidence != snapshot) {
		if (RecordBehavior(EventType::Computation,
			traceId,
			QStringLiteral("apo.cognitive.unread_ranking"),
			observation)) {
			_lastRankingAccount = self.userId;
			_lastRankingEvidence = snapshot;
		}
	}
}

} // namespace apo
