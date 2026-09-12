#pragma once
#include "common.h"

namespace strikers
{
	class iniman
	{
	public:
		static bool parse(const stringview& filepath, umap<string, string>& out_settings)
		{
            out_settings.clear();

            std::ifstream file(filepath.data());
            if (!file) return false;

            string section;
            string line;
            while (std::getline(file, line))
            {
                if (line.empty()) continue;

                // [section]
                if (line.front() == '[' && line.back() == ']')
                {
                    section = line.substr(1, line.size() - 2);
                    continue;
                }

                // cb = toad;
                auto equals = line.find('=');
                if (equals == std::string::npos)
                    continue;

                string key = line.substr(0, equals);
                string value = line.substr(equals + 1);

                // Remove whitespace
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t;") + 1);
                out_settings[section + "." + key] = value ;
            }
            return true;
		}
	};
}