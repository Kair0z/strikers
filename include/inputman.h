#pragma once
#include "common.h"

namespace strikers {
static const char* k_button_names[]
{
	"lmouse",
	"rmouse",
	"escape",
	"space",
	"enter",
	"shift",
	"ctrl",
	"alt",
	"left",
	"right",
	"up",
	"down",
	"ascii"
};

class inputman
{
	struct button_state;
	struct input_state;

public:
	enum class button
	{
		lmouse,
		rmouse,
		escape,
		space,
		enter,
		shift,
		ctrl,
		alt,
		left,
		right,
		up,
		down,
		ascii, // any character
		num
	};

	static inputman& get() { static inputman singleton{}; return singleton; }
	void write_button_state(button btn, bool is_down);
	void write_button_state(char ascii, bool is_down);
	void write_mouse_position(const float2& new_position);

	uint32 get_button_state_index(button btn) const;
	uint32 get_button_state_index(char ascii) const;

	bool is_button_down(button btn, uint32* num_frames_since_change = nullptr);
	bool is_button_down(char ascii, uint32* num_frames_since_change = nullptr);
	bool is_button_down(const char* btn_str, uint32* num_frames_since_change = nullptr);

	float2 get_mouse_delta() const;
	float2 get_mouse_position() const;

	void tick();
	void reset();

private:
	struct button_state final
	{
		uint32 m_is_down;
		uint32 m_frames_since_change;
	};
	struct mouse_state final
	{
		float2 m_current_position;
		float2 m_previous_position;
		uint32 m_frames_since_change;
	};

	static constexpr uint32 k_num_ascii_values = 256;
	static constexpr uint32 k_num_button_states = ((uint32)button::num - 1) + k_num_ascii_values;

	struct input_state final
	{
		mouse_state m_mouse;
		button_state m_buttons[k_num_button_states];
	};
	input_state m_input_state;

	button_state& get_button_state(button btn);
	button_state& get_button_state(char ascii);

	const button_state& get_button_state(button btn) const;
	const button_state& get_button_state(char ascii) const;
	const input_state& get_input_state() const;

	static bool parse_button_from_name(const char* name, button& out_button)
	{
		for (uint32 i = 0u; i < (uint32)button::num; ++i)
		{
			if (strcmp(k_button_names[i], name) == 0)
			{
				out_button = (button)i;
				return true;
			}
		}
		return false;
	}
};
}