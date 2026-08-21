#include "commandman.h"
#include "logman.h"

namespace strikers
{
command cm_log_cmds("log.clear", "0", command::flags::oneshot);

command::command(const char* name, const char* default_value, flags flg)
    : m_name{ name }, m_default_value{ default_value }, m_value{ default_value }, m_flags{flg}
{
    commandman::get().register_command(this);
}

void command::set_value(const string& value)
{
    if (cm_log_cmds.get_value() > 0) 
        logman::log("cmd.{}({})", m_name, value);

    m_value = value;
    if (m_flags & flags::oneshot)
    {
        m_lifetime = 2;
    }
}

void command::set_to_default()
{
    set_value(m_default_value);
}

void command::tick()
{
    if (m_lifetime >= 0)
    {
        m_lifetime--;
        if (m_lifetime == 0)
        {
            // revert to default
            m_value = m_default_value;
        }
    }
}

void commandman::register_command(command* cmd)
{
    m_commands[cmd->name()] = cmd;
}

void commandman::command_script(const stringview& filepath)
{
    const auto current_time = std::filesystem::last_write_time(filepath);
    if (current_time != m_last_command_script_check)
    {
        struct command
        {
            string m_name;
            vector<string> m_arguments;
        };

        std::ifstream file(filepath.data());
        vector<command> commands;
        string line;
        while (std::getline(file, line))
        {
            std::istringstream stream(line);

            command cmd;
            stream >> cmd.m_name;

            if (cmd.m_name.empty())
                continue;

            // Remove trailing ';' from command-only lines.
            if (!cmd.m_name.empty() && cmd.m_name.back() == ';')
            {
                cmd.m_name.pop_back();
                commands.push_back(std::move(cmd));
                continue;
            }

            std::string argument;
            while (stream >> argument)
            {
                // Last argument may end in ';'
                if (!argument.empty() && argument.back() == ';')
                    argument.pop_back();

                if (!argument.empty())
                    cmd.m_arguments.push_back(std::move(argument));
            }

            commands.push_back(std::move(cmd));
        }

        // execute commands
        for (const auto& cmd : commands)
        {
            auto found = m_commands.find(cmd.m_name);
            if (found != m_commands.cend())
            {
                found->second->set_value(cmd.m_arguments[0]);
            }
        }

        m_last_command_script_check = current_time;
    }
}

void commandman::tick()
{
    command_script("D:/Git/strikers/commands.md");

    for (auto& cmd : m_commands)
    {
        cmd.second->tick();
    }
}
}