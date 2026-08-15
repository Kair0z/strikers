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
}