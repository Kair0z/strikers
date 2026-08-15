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
public:
	struct button_state;
	struct input_state;

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

	enum class input_action
	{

	};
	enum class input_axis
	{
		move
	};

	static inputman& get() { static inputman singleton{}; return singleton; }
	void write_button_state(button btn, bool is_down);
	void write_button_state(char ascii, bool is_down);
	uint32 get_button_state_index(button btn) const;
	uint32 get_button_state_index(char ascii) const;
	const button_state& get_button_state(button btn) const;
	const button_state& get_button_state(char ascii) const;
	const input_state& get_input_state() const;

	bool is_button_down(button btn, uint32* num_frames_since_change = nullptr);
	bool is_button_down(char ascii, uint32* num_frames_since_change = nullptr);

	void tick();
	void reset();

private:
	button_state& get_button_state(button btn);
	button_state& get_button_state(char ascii);

	struct button_state final
	{
		uint32 m_is_down;
		uint32 m_frames_since_change;
	};

	static constexpr uint32 k_num_ascii_values = 256;
	static constexpr uint32 k_num_button_states = ((uint32)button::num - 1) + k_num_ascii_values;

	struct input_state final
	{
		button_state m_buttons[k_num_button_states];
	};
	input_state m_input_state;
};
}