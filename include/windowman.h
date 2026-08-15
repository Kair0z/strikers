#pragma once
#include "common.h"

namespace strikers {
	using window_id = id;
	struct window_create_args final
	{
		stringview	m_title = "";
		uint2		m_size = {};
		bool		m_show = true;
	};

namespace platform {
	using window_handle = void*;

#if DF_WINDOWS
	LRESULT CALLBACK windows_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	inline string get_last_error_as_string()
	{
		DWORD error = GetLastError();
		if (error == 0) return "";

		LPSTR buffer = nullptr;
		DWORD size = FormatMessageA(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPSTR)&buffer,
			0,
			nullptr
		);

		string message(buffer, size);
		LocalFree(buffer);
		return message;
	}

	result<window_handle> create_window(const window_create_args& args);
	
	inline void poll_window(window_handle handle, bool& out_quit_requested)
	{
		MSG msg;
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				out_quit_requested = true;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
#else
	static_assert("platform not implemented!");
#endif
} // platform

	class windowman final
	{
		struct window_data final
		{
			window_create_args m_create_args = {};
			uint2 m_current_size;
			platform::window_handle m_platform_handle = {};
		};
		vector<window_data> m_windows{};

	public:
		result<window_id> new_window(const window_create_args& args)
		{
			using restype = result<window_id>;
			const window_id new_id = m_windows.size();

			window_data new_data;
			new_data.m_create_args = args;
			new_data.m_platform_handle = platform::create_window(args).claim();

			m_windows.push_back(new_data);
			return restype::make_success(new_id);
		}

		result<> destroy_window(window_id id)
		{
			using restype = result<>;
			if (!is_valid_id(id))
			{
				return restype::make_fail("destroy_window(id) failed > id not valid!");
			}

			// swap & erase
			m_windows[id] = m_windows.back();
			m_windows.pop_back();
			return {};
		}

		bool is_valid_id(window_id id) const
		{
			return id != k_id_invalid && id < m_windows.size();
		}

		result<uint2> get_window_size(window_id id) const
		{
			using restype = result<uint2>;
			if (!is_valid_id(id))
				return restype::make_fail("get_window_size(id) failed > id is invalid!");

			const uint2 res = m_windows[id].m_create_args.m_size;
			return restype::make_success(res);
		}
		
		result<platform::window_handle> get_window_platform_handle(window_id id) const
		{
			using restype = result<platform::window_handle>;

			if (!is_valid_id(id))
				return restype::make_fail("get_window_platform_handle(id) failed > id is invalid!");
			
			return m_windows[id].m_platform_handle;
		}

		void poll_windows()
		{
			for (window_id id = 0; id < m_windows.size(); ++id)
			{
				const window_data& window = m_windows[id];

				bool quit_requested = false;
				platform::window_handle handle = window.m_platform_handle;
				platform::poll_window(handle, quit_requested);

				if (quit_requested)
				{
					destroy_window(id).claim();
				}
			}
		}
	};
}