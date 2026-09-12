#pragma once
#include "common.h"

namespace strikers {

class command
{
public:
	enum flags
	{
		none = 0,
		oneshot = (1 << 0),
	};

	command(const char* name, const char* default_value, flags flg = none);
	const string& name() const { return m_name; }
	const string& default_value() const { return m_default_value; }
	const string& value() const { return m_value; }

	void set_to_default();
	void tick();
	void set_value(const string& value);

	template <typename _t = float>
	_t get_value() const
	{
		return (_t)std::stof(m_value);
	}
	template <typename _t = float>
	bool get_default_value(_t& out_value) const
	{
		return (_t)std::stof(m_value);
	}

	bool enabled() const
	{
		return get_value<float>() > 0.0f;
	}

private:
	friend class commandman;
	string m_name;
	string m_default_value;
	string m_value;
	flags m_flags;
	int m_lifetime = -1;
};

class commandman
{
	friend class command;
	using file_time = std::filesystem::file_time_type;
	file_time m_last_command_script_check;

	umap<string, command*> m_commands;
	void register_command(command* cmd);

public:
	static commandman& get()
	{
		static commandman singleton{};
		return singleton;
	}

	template <typename _t>
	bool get_value(const char* cmd, _t& out_value) const
	{
		auto found = m_commands.find(string(cmd));
		if (found != m_commands.cend())
		{
			out_value = found->second->get_value(out_value);
			return true;
		}
		else return false;
	}
	template <typename _t>
	bool get_default_value(const char* cmd, _t& out_value) const
	{
		auto found = m_commands.find(string(cmd));
		if (found != m_commands.cend())
		{
			out_value = found->second->get_default_value(out_value);
			return true;
		}
		else return false;
	}
	
	void command_script(const stringview& filepath);
	void tick();
};
}