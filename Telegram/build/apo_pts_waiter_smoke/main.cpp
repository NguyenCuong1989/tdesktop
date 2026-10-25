#include "data/data_pts_waiter.h"

#include "api/api_updates.h"
#include "apo/apo_runtime.h"

#include <iostream>
#include <numeric>
#include <random>
#include <string_view>

namespace {

constexpr auto kAccountId = std::int64_t(424242);

struct Fixture {
	Api::Updates owner = Api::Updates(kAccountId);
	PtsWaiter waiter = PtsWaiter(&owner);
	ChannelData channel;
};

bool HooksMatch(
		const std::vector<int> &ids,
		const std::vector<QString> &origins) {
	if (apo::HookCalls.size() != ids.size() || ids.size() != origins.size()) {
		return false;
	}
	for (auto i = std::size_t(0); i != ids.size(); ++i) {
		const auto &call = apo::HookCalls[i];
		if (call.event != u"PTS_HANDLER_RETURNED"_q
			|| call.owner != kAccountId
			|| call.payload[u"constructor_id"_q].toDouble() != ids[i]
			|| call.payload[u"phase"_q].toString() != u"PTS_HANDLER_RETURNED"_q
			|| call.payload[u"dispatch_mode"_q].toString() != origins[i]) {
			return false;
		}
	}
	return true;
}

bool Run(const char *name, const std::function<bool()> &check) {
	apo::HookCalls.clear();
	const auto passed = check();
	std::cout << (passed ? "[PASS] " : "[FAIL] ") << name << '\n';
	return passed;
}

bool RunShuffleStress() {
	constexpr auto kFirstPts = 101;
	constexpr auto kLastPts = 132;
	auto generator = std::mt19937(0xA90F4242U);
	auto sawDelayed = false;
	auto sawQueued = false;
	for (auto iteration = 0; iteration != 100; ++iteration) {
		Fixture f;
		f.waiter.init(kFirstPts - 1);
		auto arrival = std::vector<int>(kLastPts - kFirstPts + 1);
		std::iota(arrival.begin(), arrival.end(), kFirstPts);
		std::shuffle(arrival.begin(), arrival.end(), generator);
		for (const auto pts : arrival) {
			const auto before = f.owner.applied.size();
			f.waiter.updateAndApply(
				&f.channel,
				pts,
				1,
				MTPUpdate(pts));
			sawDelayed = sawDelayed || (f.owner.applied.size() == before);
		}
		auto expected = std::vector<int>(arrival.size());
		std::iota(expected.begin(), expected.end(), kFirstPts);
		if (f.waiter.current() != kLastPts
			|| f.owner.applied != expected
			|| apo::HookCalls.size() != expected.size()) {
			return false;
		}
		for (auto i = std::size_t(0); i != expected.size(); ++i) {
			const auto &call = apo::HookCalls[i];
			const auto origin = call.payload[u"dispatch_mode"_q].toString();
			if (call.event != u"PTS_HANDLER_RETURNED"_q
				|| call.owner != kAccountId
				|| call.payload[u"constructor_id"_q].toDouble()
					!= expected[i]
				|| call.payload[u"phase"_q].toString()
					!= u"PTS_HANDLER_RETURNED"_q
				|| (origin != u"direct"_q && origin != u"queued"_q)) {
				return false;
			}
			sawQueued = sawQueued || (origin == u"queued"_q);
		}
		apo::HookCalls.clear();
	}
	return sawDelayed && sawQueued;
}

void PrintValues(const char *name, const std::vector<int> &values) {
	std::cout << name << '=';
	for (auto i = std::size_t(0); i != values.size(); ++i) {
		std::cout << (i ? "," : "") << values[i];
	}
	std::cout << '\n';
}

int RunReentrantArrivalProbe() {
	apo::HookCalls.clear();
	Fixture f;
	f.waiter.init(10);
	f.waiter.updateAndApply(&f.channel, 12, 1, MTPUpdate(12));
	f.waiter.updateAndApply(&f.channel, 13, 1, MTPUpdate(13));
	f.owner.onApply = [&] {
		f.owner.onApply = {};
		f.waiter.updateAndApply(&f.channel, 14, 1, MTPUpdate(14));
	};
	f.waiter.updateAndApply(&f.channel, 11, 1, MTPUpdate(11));

	auto markerOrder = std::vector<int>();
	markerOrder.reserve(apo::HookCalls.size());
	for (const auto &call : apo::HookCalls) {
		markerOrder.push_back(
			static_cast<int>(
				call.payload[u"constructor_id"_q].toDouble()));
	}
	const auto expected = std::vector<int>{ 11, 12, 13, 14 };
	const auto exactlyOnce = [&] {
		auto sorted = f.owner.applied;
		std::sort(sorted.begin(), sorted.end());
		return sorted == expected;
	}();
	const auto monotonic = (f.owner.applied == expected);
	const auto ptsAdvanced = (f.waiter.current() == 14);
	const auto markerMatchesApply = (markerOrder == f.owner.applied);
	const auto valid = exactlyOnce
		&& monotonic
		&& ptsAdvanced
		&& markerMatchesApply;

	PrintValues("MEASURED_APPLY_ORDER", f.owner.applied);
	std::cout << "MEASURED_CURRENT_PTS=" << f.waiter.current() << '\n';
	PrintValues("MEASURED_PTS_HANDLER_RETURNED_ORDER", markerOrder);
	std::cout << "NEW_EVENT_OVERTAKES_QUEUED="
		<< (monotonic ? "false" : "true") << '\n';
	std::cout << "LEDGER_MARKER_ORDER_INVERSES_CALLBACK_RETURN="
		<< (markerMatchesApply ? "false" : "true") << '\n';
	std::cout << "EXACTLY_ONCE=" << (exactlyOnce ? "true" : "false")
		<< '\n';
	std::cout << (valid
		? "[PASS] reentrant arrival preserves bounded PTS semantics\n"
		: "[BEHAVIORAL HAZARD] source-level reentrant arrival violates "
			"bounded PTS semantics\n");
	std::cout << "NATIVE_REPRODUCTION=NOT_RUN\n";
	return valid ? 0 : 1;
}

} // namespace

int main(int argc, char *argv[]) {
	if (argc == 2
		&& std::string_view(argv[1]) == "--probe-reentrant-arrival") {
		return RunReentrantArrivalProbe();
	} else if (argc != 1) {
		std::cerr << "usage: apo-pts-waiter-smoke.exe "
			"[--probe-reentrant-arrival]\n";
		return 2;
	}
	auto failures = 0;
	failures += !Run("init(10) applies pts11 directly once", [] {
		Fixture f;
		f.waiter.init(10);
		const auto accepted = f.waiter.updateAndApply(
			&f.channel, 11, 1, MTPUpdate(111));
		return accepted
			&& f.owner.applied == std::vector<int>{ 111 }
			&& HooksMatch({ 111 }, { u"direct"_q });
	});
	failures += !Run("out-of-order 12 waits for 11 and drains in order", [] {
		Fixture f;
		f.waiter.init(10);
		const auto queued = !f.waiter.updateAndApply(
			&f.channel, 12, 1, MTPUpdate(112));
		const auto drained = f.waiter.updateAndApply(
			&f.channel, 11, 1, MTPUpdate(111));
		return queued && drained
			&& f.owner.applied == std::vector<int>({ 111, 112 })
			&& HooksMatch({ 111, 112 }, { u"queued"_q, u"queued"_q });
	});
	failures += !Run("duplicate pts12 is ignored", [] {
		Fixture f;
		f.waiter.init(10);
		f.waiter.updateAndApply(&f.channel, 11, 1, MTPUpdate(111));
		f.waiter.updateAndApply(&f.channel, 12, 1, MTPUpdate(112));
		const auto duplicate = f.waiter.updateAndApply(
			&f.channel, 12, 1, MTPUpdate(212));
		return !duplicate
			&& f.owner.applied == std::vector<int>({ 111, 112 })
			&& HooksMatch({ 111, 112 }, { u"direct"_q, u"direct"_q });
	});
	failures += !Run("unfilled gap schedules wait timer without dispatch", [] {
		Fixture f;
		f.waiter.init(10);
		const auto queued = !f.waiter.updateAndApply(
			&f.channel, 12, 1, MTPUpdate(112));
		return queued
			&& f.owner.applied.empty()
			&& apo::HookCalls.empty()
			&& f.owner.timers == std::vector<crl::time>{ 1000 };
	});
	failures += !Run("handler init(50) cancels remaining queued entries", [] {
		Fixture f;
		f.waiter.init(10);
		f.waiter.updateAndApply(&f.channel, 12, 1, MTPUpdate(112));
		f.waiter.updateAndApply(&f.channel, 13, 1, MTPUpdate(113));
		f.owner.onApply = [&] {
			f.owner.onApply = {};
			f.waiter.init(50);
		};
		f.waiter.updateAndApply(&f.channel, 11, 1, MTPUpdate(111));
		return f.waiter.current() == 50
			&& f.owner.applied == std::vector<int>{ 111 }
			&& HooksMatch({ 111 }, { u"queued"_q });
	});
	failures += !Run("handler setRequesting(true) cancels remaining queue", [] {
		Fixture f;
		f.waiter.init(10);
		f.waiter.updateAndApply(&f.channel, 12, 1, MTPUpdate(112));
		f.waiter.updateAndApply(&f.channel, 13, 1, MTPUpdate(113));
		f.owner.onApply = [&] {
			f.owner.onApply = {};
			f.waiter.setRequesting(true);
		};
		f.waiter.updateAndApply(&f.channel, 11, 1, MTPUpdate(111));
		return f.waiter.requesting()
			&& f.owner.applied == std::vector<int>{ 111 }
			&& HooksMatch({ 111 }, { u"queued"_q });
	});
	failures += !Run("one handler-return hook per direct or queued apply", [] {
		Fixture direct;
		direct.waiter.init(20);
		direct.waiter.updateAndApply(
			&direct.channel, 21, 1, MTPUpdates(221));
		Fixture queued;
		queued.waiter.init(30);
		queued.waiter.updateAndApply(
			&queued.channel, 32, 1, MTPUpdates(232));
		queued.waiter.updateAndApply(
			&queued.channel, 31, 1, MTPUpdates(231));
		return direct.owner.applied == std::vector<int>{ 221 }
			&& queued.owner.applied == std::vector<int>({ 231, 232 })
			&& HooksMatch(
				{ 221, 231, 232 },
				{ u"direct"_q, u"queued"_q, u"queued"_q });
	});
	failures += !Run(
		"100 fixed-seed contiguous shuffles drain exactly once in PTS order",
		[] { return RunShuffleStress(); });
	failures += !Run(
		"reentrant requesting reset then init recovers cleanly",
		[] {
			Fixture f;
			f.waiter.init(10);
			f.waiter.updateAndApply(&f.channel, 12, 1, MTPUpdate(112));
			f.waiter.updateAndApply(&f.channel, 13, 1, MTPUpdate(113));
			f.owner.onApply = [&] {
				f.owner.onApply = {};
				f.waiter.setRequesting(true);
			};
			f.waiter.updateAndApply(&f.channel, 11, 1, MTPUpdate(111));
			f.waiter.setRequesting(false);
			f.waiter.init(20);
			const auto recovered = f.waiter.updateAndApply(
				&f.channel,
				21,
				1,
				MTPUpdate(221));
			return recovered
				&& !f.waiter.requesting()
				&& f.waiter.current() == 21
				&& f.owner.applied == std::vector<int>({ 111, 221 })
				&& HooksMatch(
					{ 111, 221 },
					{ u"queued"_q, u"direct"_q });
		});
	failures += !Run("mixed constructors preserve queued and direct origins", [] {
		Fixture f;
		f.waiter.init(40);
		f.waiter.updateAndApply(&f.channel, 42, 1, MTPUpdates(442));
		f.waiter.updateAndApply(&f.channel, 41, 1, MTPUpdate(441));
		f.waiter.updateAndApply(&f.channel, 43, 1, MTPUpdates(443));
		return f.owner.applied == std::vector<int>({ 441, 442, 443 })
			&& HooksMatch(
				{ 441, 442, 443 },
				{ u"queued"_q, u"queued"_q, u"direct"_q });
	});
	failures += !Run("count zero is accepted without advancing good PTS", [] {
		Fixture f;
		f.waiter.init(50);
		const auto accepted = f.waiter.updateAndApply(
			&f.channel,
			80,
			0,
			MTPUpdate(480));
		return accepted
			&& f.waiter.current() == 50
			&& f.owner.applied == std::vector<int>{ 480 }
			&& f.owner.timers == std::vector<crl::time>{ 1000 }
			&& HooksMatch({ 480 }, { u"direct"_q });
	});
	std::cout << (failures
		? "[FAIL] isolated actual-source smoke suite\n"
		: "[PASS] isolated actual-source smoke suite\n");
	return failures ? 1 : 0;
}
