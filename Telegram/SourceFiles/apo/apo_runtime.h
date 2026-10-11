/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "apo/apo_common.h"
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <memory>

namespace apo {

class Runtime final : public QObject {
public:
	static Runtime &Instance();

	void start();
	void stop();
	[[nodiscard]] bool isRunning() const;

	// Strategic Hook Points inside Telegram Desktop
	void onUiEvent(
		const QString &widgetName,
		const QString &actionName,
		const QJsonObject &details = QJsonObject());

	void onMtprotoRequest(
		int64 reqId,
		const QString &method,
		const QJsonObject &params = QJsonObject());

	void onMtprotoResponse(
		int64 reqId,
		const QString &method,
		bool success,
		const QJsonObject &result = QJsonObject());

	void onServerUpdate(
		const QString &updateType,
		const QJsonObject &updateData = QJsonObject());

private:
	Runtime();
	~Runtime() override;

	Runtime(const Runtime &) = delete;
	Runtime &operator=(const Runtime &) = delete;

	void onHeartbeatTick();

	bool _running = false;
	std::unique_ptr<QTimer> _heartbeatTimer;
};

// Global hook shortcuts for tdesktop core code
inline void HookUiEvent(const QString &widget, const QString &action, const QJsonObject &d = QJsonObject()) {
	Runtime::Instance().onUiEvent(widget, action, d);
}

inline void HookMtprotoRequest(int64 reqId, const QString &method, const QJsonObject &p = QJsonObject()) {
	Runtime::Instance().onMtprotoRequest(reqId, method, p);
}

inline void HookMtprotoResponse(int64 reqId, const QString &method, bool ok, const QJsonObject &res = QJsonObject()) {
	Runtime::Instance().onMtprotoResponse(reqId, method, ok, res);
}

inline void HookServerUpdate(const QString &type, const QJsonObject &d = QJsonObject()) {
	Runtime::Instance().onServerUpdate(type, d);
}

} // namespace apo
