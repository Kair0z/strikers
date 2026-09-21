#pragma once
#include "common.h"

namespace strikers {
class contentman;
class fontman final
{
public:
	static fontman& get() { static fontman singleton; return singleton; }

	bool parse_font(
		contentman& cman,
		const stringview& filepath, 
		const char character,
		image_id& out_image,
		rect& out_rect_uv) const;
};
}