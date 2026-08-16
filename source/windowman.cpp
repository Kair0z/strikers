#include "windowman.h"
#include "inputman.h"

namespace strikers {
#if DF_WINDOWS
namespace platform {
void write_button_state(inputman& input, WPARAM wparam, bool is_down)
{
	using btn = inputman::button; // sue me I'm lazy
	switch (wparam)
	{
	case VK_ESCAPE:		input.write_button_state(btn::escape, is_down); return;
	case VK_SPACE:		input.write_button_state(btn::space, is_down); return;
	case VK_RETURN:		input.write_button_state(btn::enter, is_down); return;
	case VK_SHIFT:		input.write_button_state(btn::shift, is_down); return;
	case VK_CONTROL:	input.write_button_state(btn::ctrl, is_down); return;
	case VK_MENU:		input.write_button_state(btn::alt, is_down); return;
	case VK_LEFT:		input.write_button_state(btn::left, is_down); return;
	case VK_RIGHT:		input.write_button_state(btn::right, is_down); return;
	case VK_UP:			input.write_button_state(btn::up, is_down); return;
	case VK_DOWN:		input.write_button_state(btn::down, is_down); return;
	}

	// is_ascii ?
	if ((wparam >= 'A' && wparam <= 'Z') || (wparam >= '0' && wparam <= '9'))
	{
		input.write_button_state((char)wparam, is_down);
	}
}

LRESULT CALLBACK windows_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	inputman& input = inputman::get();

	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_KEYDOWN:		write_button_state(input, wParam, true); break;
	case WM_KEYUP:			write_button_state(input, wParam, false); break;
	case WM_LBUTTONDOWN:	input.write_button_state(inputman::button::lmouse, true); break;
	case WM_LBUTTONUP:		input.write_button_state(inputman::button::lmouse, false); break;
	case WM_RBUTTONDOWN:	input.write_button_state(inputman::button::rmouse, true); break;
	case WM_RBUTTONUP:		input.write_button_state(inputman::button::rmouse, false); break;
	
	case WM_MOUSEMOVE:
	case WM_MOUSEHOVER:
	{
		int x = static_cast<short>(lParam & 0xFFFF);
		int y = static_cast<short>((lParam >> 16) & 0xFFFF);
		input.write_mouse_position(float2(x, y));
		break;
	};
	case WM_SIZE:
		if (wParam != SIZE_MINIMIZED)
		{
			UINT width = LOWORD(lParam);
			UINT height = HIWORD(lParam);
			// resize??...
		}
		return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

result<window_handle> create_window(const window_create_args& args)
{
	using restype = result<window_handle>;
	const wchar_t CLASS_NAME[] = L"MyWindowClass";

	const HINSTANCE current_instance = GetModuleHandle(NULL);

	static bool once = true;
	if (once)
	{
		WNDCLASS wc = {};
		wc.lpfnWndProc = windows_proc;
		wc.hInstance = current_instance;
		wc.lpszClassName = CLASS_NAME;
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		RegisterClass(&wc);

		const string error = get_last_error_as_string();
		if (!error.empty()) return restype::make_fail(error.c_str());

		once = false;
	}

	HWND hwnd = CreateWindowEx(
		0,
		CLASS_NAME,
		to_wstring(args.m_title).c_str(),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		args.m_size.x, args.m_size.y,
		nullptr,
		nullptr,
		current_instance,
		nullptr
	);

	if (hwnd == nullptr)
	{
		const string error = get_last_error_as_string();
		return restype::make_fail(error.c_str());
	}

	ShowWindow(hwnd, args.m_show);
	UpdateWindow(hwnd);
	return hwnd;
}
}
#endif // DF_WINDOWS
}