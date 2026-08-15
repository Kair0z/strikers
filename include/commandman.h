#pragma once
#include "common.h"

namespace strikers {

class commandman
{
public:
	void execute(const stringview& cmd, const stringview& args);
};
}