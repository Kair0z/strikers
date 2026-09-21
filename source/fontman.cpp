#include "fontman.h"
#include "contentman.h"

namespace strikers
{
	bool fontman::parse_font(
		contentman& cman,
		const stringview& filepath, 
		const char character, 
		image_id& out_image, 
		rect& out_rect_uv) const
	{
		uint32 quarter_idx = 0;

		out_image = contentman::make_image_id(filepath);
		image_asset const* image = cman.find_typed_asset<asset_type::image>(out_image).claim();
		if (image)
		{
			float& min_x = out_rect_uv.m_min_max.x;
			float& min_y = out_rect_uv.m_min_max.y;
			float& max_x = out_rect_uv.m_min_max.z;
			float& max_y = out_rect_uv.m_min_max.w;
			
			min_x = 0.0f;
			min_y = 0.0f;
			max_x = 0.96f;
			max_y = 0.92f;
			return true;
		}
		else return false;
	}
}