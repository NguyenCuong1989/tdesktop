/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_pts_waiter.h"

#include "api/api_updates.h"
#include "apo/apo_runtime.h"
#include "main/main_session.h"

#include <utility>

namespace {

template <typename Update>
void RecordPtsHandlerReturned(
		not_null<Api::Updates*> owner,
		const Update &update,
		const QString &dispatchMode) {
	apo::HookServerUpdate(
		QStringLiteral("PTS_HANDLER_RETURNED"),
		QJsonObject{
			{ QStringLiteral("constructor_id"),
				static_cast<qint64>(update.type()) },
			{ QStringLiteral("phase"),
				QStringLiteral("PTS_HANDLER_RETURNED") },
			{ QStringLiteral("dispatch_mode"), dispatchMode },
		},
		owner->session().userId().bare);
}

} // namespace

PtsWaiter::PtsWaiter(not_null<Api::Updates*> owner) : _owner(owner) {
}

uint64 PtsWaiter::ptsKey(PtsSkippedQueue queue, int32 pts) {
	return _queue.emplace(
		uint64(uint32(pts)) << 32 | (++_skippedKey),
		queue
	).first->first;
}

void PtsWaiter::setWaitingForSkipped(ChannelData *channel, crl::time ms) {
	if (ms >= 0) {
		_owner->ptsWaiterStartTimerFor(channel, ms);
		_waitingForSkipped = true;
	} else {
		_waitingForSkipped = false;
		checkForWaiting(channel);
	}
}

void PtsWaiter::setWaitingForShortPoll(ChannelData *channel, crl::time ms) {
	if (ms >= 0) {
		_owner->ptsWaiterStartTimerFor(channel, ms);
		_waitingForShortPoll = true;
	} else {
		_waitingForShortPoll = false;
		checkForWaiting(channel);
	}
}

void PtsWaiter::checkForWaiting(ChannelData *channel) {
	if (!_waitingForSkipped && !_waitingForShortPoll) {
		_owner->ptsWaiterStartTimerFor(channel, -1);
	}
}

void PtsWaiter::applySkippedUpdates(ChannelData *channel) {
	if (!_waitingForSkipped) {
		return;
	}

	setWaitingForSkipped(channel, -1);

	if (_queue.empty()) {
		return;
	}

	++_applySkippedLevel;
	while (!_queue.empty()) {
		const auto i = _queue.cbegin();
		const auto key = i->first;
		const auto type = i->second;
		_queue.erase(i);
		switch (type) {
		case SkippedUpdate: {
			const auto update = _updateQueue[key];
			_updateQueue.erase(key);
			_owner->applyUpdateNoPtsCheck(update);
			RecordPtsHandlerReturned(_owner, update, u"queued"_q);
		} break;
		case SkippedUpdates: {
			const auto updates = _updatesQueue[key];
			_updatesQueue.erase(key);
			_owner->applyUpdatesNoPtsCheck(updates);
			RecordPtsHandlerReturned(_owner, updates, u"queued"_q);
		} break;
		}
	}
	--_applySkippedLevel;
	clearSkippedUpdates();
	// A native update can arrive synchronously while a queued handler runs.
	// Apply it only after the earlier PTS batch has fully drained.
	while (!_deferredApply.empty()) {
		auto next = std::move(_deferredApply.front());
		_deferredApply.pop_front();
		next();
	}
}

void PtsWaiter::clearSkippedUpdates() {
	_queue.clear();
	_updateQueue.clear();
	_updatesQueue.clear();
}

bool PtsWaiter::updated(
		ChannelData *channel,
		int32 pts,
		int32 count,
		const MTPUpdates &updates) {
	if (_requesting || _applySkippedLevel) {
		return true;
	} else if (pts <= _good && count > 0) {
		return false;
	} else if (check(channel, pts, count)) {
		return true;
	}
	_updatesQueue.emplace(ptsKey(SkippedUpdates, pts), updates);
	return false;
}

bool PtsWaiter::updated(
		ChannelData *channel,
		int32 pts,
		int32 count,
		const MTPUpdate &update) {
	if (_requesting || _applySkippedLevel) {
		return true;
	} else if (pts <= _good && count > 0) {
		return false;
	} else if (check(channel, pts, count)) {
		return true;
	}
	_updateQueue.emplace(ptsKey(SkippedUpdate, pts), update);
	return false;
}

bool PtsWaiter::updated(ChannelData *channel, int32 pts, int32 count) {
	if (_requesting || _applySkippedLevel) {
		return true;
	} else if (pts <= _good && count > 0) {
		return false;
	}
	return check(channel, pts, count);
}

bool PtsWaiter::updateAndApply(
		ChannelData *channel,
		int32 pts,
		int32 count,
		const MTPUpdates &updates) {
	if (_applySkippedLevel) {
		_deferredApply.emplace_back([this, channel, pts, count, updates] {
			updateAndApply(channel, pts, count, updates);
		});
		return true;
	}
	if (!updated(channel, pts, count, updates)) {
		return false;
	}
	if (!_waitingForSkipped || _queue.empty()) {
		// Optimization - no need to put in queue and back.
		_owner->applyUpdatesNoPtsCheck(updates);
		RecordPtsHandlerReturned(_owner, updates, u"direct"_q);
	} else {
		_updatesQueue.emplace(ptsKey(SkippedUpdates, pts), updates);
		applySkippedUpdates(channel);
	}
	return true;
}

bool PtsWaiter::updateAndApply(
		ChannelData *channel,
		int32 pts,
		int32 count,
		const MTPUpdate &update) {
	if (_applySkippedLevel) {
		_deferredApply.emplace_back([this, channel, pts, count, update] {
			updateAndApply(channel, pts, count, update);
		});
		return true;
	}
	if (!updated(channel, pts, count, update)) {
		return false;
	}
	if (!_waitingForSkipped || _queue.empty()) {
		// Optimization - no need to put in queue and back.
		_owner->applyUpdateNoPtsCheck(update);
		RecordPtsHandlerReturned(_owner, update, u"direct"_q);
	} else {
		_updateQueue.emplace(ptsKey(SkippedUpdate, pts), update);
		applySkippedUpdates(channel);
	}
	return true;
}

bool PtsWaiter::updateAndApply(
		ChannelData *channel,
		int32 pts,
		int32 count) {
	if (_applySkippedLevel) {
		_deferredApply.emplace_back([this, channel, pts, count] {
			updateAndApply(channel, pts, count);
		});
		return true;
	}
	if (!updated(channel, pts, count)) {
		return false;
	}
	applySkippedUpdates(channel);
	return true;
}

// Return false if need to save that update and apply later.
bool PtsWaiter::check(ChannelData *channel, int32 pts, int32 count) {
	if (!inited()) {
		init(pts);
		return true;
	}

	_last = std::max(_last, pts);
	_count += count;
	if (_last == _count) {
		_good = _last;
		return true;
	} else if (_last < _count) {
		setWaitingForSkipped(channel, 1);
	} else {
		setWaitingForSkipped(channel, kWaitForSkippedTimeout);
	}
	return !count;
}
