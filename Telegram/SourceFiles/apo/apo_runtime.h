/*
This file is part of Telegram Desktop APΩ Agent Edition.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "apo/apo_common.h"
#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <memory>

namespace apo {

struct NativeChangeNotificationCounts final {
	int peer = 0;
	int history = 0;
	int message = 0;
	int entry = 0;
	int topic = 0;
	int sublist = 0;
	int story = 0;
};

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
		const QJsonObject &updateData,
		int64 accountUserId);

	// Session-owned notification flush, not a confirmed external postcondition.
	void onNativeChangeNotificationsFlushed(
		int64 accountUserId,
		NativeChangeNotificationCounts counts);

private:
	Runtime();
	~Runtime() override;

	Runtime(const Runtime &) = delete;
	Runtime &operator=(const Runtime &) = delete;

	void onHeartbeatTick();
	void onLedgerAdmissionFailure();
	void recordUiEvent(
		const QString &widgetName,
		const QString &actionName,
		const QJsonObject &details,
		int64 accountUserId);
	void recordMtprotoRequest(
		int64 reqId,
		const QString &method,
		const QJsonObject &params,
		int64 accountUserId);
	void recordMtprotoResponse(
		int64 reqId,
		const QString &method,
		bool success,
		const QJsonObject &result,
		int64 accountUserId);
	void recordServerUpdate(
		const QString &updateType,
		const QJsonObject &updateData,
		int64 accountUserId);
	void recordNativeChangeNotificationsFlushed(
		int64 accountUserId,
		NativeChangeNotificationCounts counts);

	bool _running = false;
	std::unique_ptr<QTimer> _heartbeatTimer;
	int64 _sensorySequence = 0;
	QMutex _ingressMutex;
	bool _ingressOpen = false;
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

inline void HookServerUpdate(
		const QString &type,
		const QJsonObject &data,
		int64 accountUserId) {
	Runtime::Instance().onServerUpdate(type, data, accountUserId);
}

inline void HookNativeChangeNotificationsFlushed(
		int64 accountUserId,
		NativeChangeNotificationCounts counts) {
	Runtime::Instance().onNativeChangeNotificationsFlushed(
		accountUserId,
		counts);
}

} // namespace apo
