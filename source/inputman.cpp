#include "inputman.h"
#include "logman.h"

namespace strikers
{
uint32 inputman::get_button_state_index(button btn) const
{
	return (uint32)btn;
}
uint32 inputman::get_button_state_index(char ascii) const
{
	static const uint32 num_regular_buttons = (uint32)(button::num) - 1u;
	return num_regular_buttons + (uint32)ascii;
}
void inputman::write_button_state(button btn, bool is_down)
{
	// logman::log("input: {}-{}", k_button_names[(uint32)btn], is_down);

	button_state& state = get_button_state(btn);
	if (state.m_is_down != (uint32)is_down)
	{
		state.m_frames_since_change = 0;
	}
	state.m_is_down = is_down;
}
void inputman::write_button_state(char ascii, bool is_down)
{
	ascii = tolower(ascii);

	// logman::log("input: {}-{}", ascii, is_down);
	button_state& state = get_button_state(ascii);
	if (state.m_is_down != (uint32)is_down)
	{
		state.m_frames_since_change = 0;
	}
	state.m_is_down = is_down;
}

bool inputman::is_button_down(button btn, uint32* num_frames_since_change)
{
	const inputman::button_state& state = get_button_state(btn);
	if (num_frames_since_change) (*num_frames_since_change) = state.m_frames_since_change;
	return state.m_is_down;
}
bool inputman::is_button_down(char ascii, uint32* num_frames_since_change)
{
	const inputman::button_state& state = get_button_state(ascii);
	if (num_frames_since_change) (*num_frames_since_change) = state.m_frames_since_change;
	return state.m_is_down;
}

void inputman::tick()
{
	for (uint32 i = 0u; i < k_num_button_states; ++i)
	{
		button_state& btn = m_input_state.m_buttons[i];
		btn.m_frames_since_change++;
	}
}

void inputman::reset()
{
	m_input_state = {};
}

const inputman::button_state& inputman::get_button_state(button btn) const
{
	return m_input_state.m_buttons[get_button_state_index(btn)];
}
const inputman::button_state& inputman::get_button_state(char ascii) const
{
	return m_input_state.m_buttons[get_button_state_index(ascii)];
}
const inputman::input_state& inputman::get_input_state() const
{
	return m_input_state;
}
inputman::button_state& inputman::get_button_state(button btn)
{
	return m_input_state.m_buttons[get_button_state_index(btn)];
}
inputman::button_state& inputman::get_button_state(char ascii)
{
	return m_input_state.m_buttons[get_button_state_index(ascii)];
}
}

