#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include "base/flat_map.h"
#include <string>
#include <utility>
#include <vector>

#include <QtCore/QJsonObject>
#include <QtCore/QString>

using int32 = std::int32_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

namespace crl {
using time = std::int64_t;
} // namespace crl

template <typename Pointer>
class not_null {
public:
	not_null(Pointer value) : _value(value) {
	}
	Pointer operator->() const {
		return _value;
	}
	operator Pointer() const {
		return _value;
	}

private:
	Pointer _value;

};

inline QString operator""_q(const char16_t *value, std::size_t size) {
	return QString::fromUtf16(value, int(size));
}

class ChannelData final {
};

class MTPUpdate final {
public:
	explicit MTPUpdate(int id = 0) : _id(id) {
	}
	int type() const {
		return _id;
	}

private:
	int _id = 0;

};

class MTPUpdates final {
public:
	explicit MTPUpdates(int id = 0) : _id(id) {
	}
	int type() const {
		return _id;
	}

private:
	int _id = 0;

};
