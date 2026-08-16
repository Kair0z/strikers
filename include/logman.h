#pragma once

#include "common.h"

namespace strikers {
class logman final
{
public:
	template<typename... _args>
	static void log(const stringview& fmt, _args&&... args)
	{
		std::string formatted = "[strk] ";
		formatted += std::vformat(fmt, std::make_format_args(args...));
		formatted += "\n";

		std::cout << formatted;
		OutputDebugStringA(formatted.c_str());
	}
};

#define log_with_cooldown(delta, cd, mssg, ...) \
	{ \
	static float s_timer = 0.0f; \
	s_timer -= delta; \
	if (s_timer < 0.0f) logman::log(mssg, __VA_ARGS__), s_timer = cd;\
	}

}