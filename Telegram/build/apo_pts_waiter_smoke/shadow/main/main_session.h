#pragma once

#include "prelude.h"

namespace Main {

struct UserId {
	std::int64_t bare = 0;
};

class Session final {
public:
	explicit Session(std::int64_t accountId) : _userId{ accountId } {
	}
	UserId userId() const {
		return _userId;
	}

private:
	UserId _userId;

};

} // namespace Main
