#pragma once

#include "main/main_session.h"

class PtsWaiter;

namespace Api {

class Updates final {
public:
	explicit Updates(std::int64_t accountId) : _session(accountId) {
	}
	Main::Session &session() {
		return _session;
	}
	void ptsWaiterStartTimerFor(ChannelData*, crl::time ms) {
		timers.push_back(ms);
	}
	void applyUpdateNoPtsCheck(const MTPUpdate &update) {
		applied.push_back(update.type());
		if (onApply) {
			onApply();
		}
	}
	void applyUpdatesNoPtsCheck(const MTPUpdates &updates) {
		applied.push_back(updates.type());
		if (onApply) {
			onApply();
		}
	}

	std::vector<crl::time> timers;
	std::vector<int> applied;
	std::function<void()> onApply;

private:
	Main::Session _session;

};

} // namespace Api
