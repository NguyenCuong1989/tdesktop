#pragma once

#include "prelude.h"

namespace apo {

struct HookCall {
	QString event;
	QJsonObject payload;
	std::int64_t owner = 0;
};

inline std::vector<HookCall> HookCalls;

inline void HookServerUpdate(
		const QString &event,
		const QJsonObject &payload,
		std::int64_t owner) {
	HookCalls.push_back({ event, payload, owner });
}

} // namespace apo
