#include <Windows.h>
#include "../platform.h"
#include "../mouse.h"
#include "../process.h"

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void platform_kernel_panic(char *message) {
	MessageBoxA(NULL, message, "Perception Kernel Panic", MB_ICONERROR);
	exit(-1);
}

HWND hWnd;
HINSTANCE hInstance;
bool tracking_mouse;

void platform_graphics_initialize() {
	WNDCLASSEX wc;

	char *className = "Perception";

	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = wndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = GetModuleHandle(NULL);
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = className;
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

	if(!RegisterClassEx(&wc))
		platform_kernel_panic("Cannot register window.");

	RECT rect;
	rect.left = 0;
	rect.top = 0;
	rect.right = 640;
	rect.bottom = 480;
	AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CLIENTEDGE);

	if(!(hWnd = CreateWindowEx(WS_EX_CLIENTEDGE, className, "Perception", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, hInstance, NULL)))
		platform_kernel_panic("Cannot create window.");

	ShowWindow(hWnd, SW_SHOW);
	UpdateWindow(hWnd);
}

void platform_mouse_initialize() {
	mouse_connected();
	mouse_set_position(640 / 2, 480 / 2);
	tracking_mouse = true;
}

void start_scheduling() {
	MSG msg;
	while(GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

DWORD WINAPI ThreadProc(LPVOID parameter) {
	process_thread_main((ProcessLaunchInfo *)parameter);
	return 1;
}

 /* create a thread, with this entry point */
bool platform_thread_create(ProcessLaunchInfo *tag) {
	HANDLE t = CreateThread(NULL, 0, ThreadProc, tag, 0, NULL);
	return t != NULL;
}

void *platform_allocate_memory(size_t size) {
	return malloc(size);
}

void platform_free_memory(void *ptr) {
	free(ptr);
}

void platform_memory_copy(void *dest, void *src, size_t size) {
	memcpy_s(dest, size, src, size);
}


LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
	case WM_CLOSE:
		if(MessageBoxA(hwnd, "Do you want to close Perception?", "Perception", MB_YESNO) == IDYES)
			exit(-1);
		else
			return 0;
	case WM_MOUSEMOVE:
		if(!tracking_mouse) {
			tracking_mouse = true;
			mouse_connected();
		}
		break;
	case WM_LBUTTONDOWN: mouse_button_down(0); break;
	case WM_LBUTTONUP: mouse_button_up(0); break;
	case WM_MBUTTONDOWN: mouse_button_down(1); break;
	case WM_MBUTTONUP: mouse_button_up(1); break;
	case WM_RBUTTONDOWN: mouse_button_down(2); break;
	case WM_RBUTTONUP: mouse_button_up(2); break;
	case WM_XBUTTONDOWN: mouse_button_down(HIWORD(wParam) == XBUTTON1 ? 3 : 4); break;
	case WM_XBUTTONUP: mouse_button_up(HIWORD(wParam) == XBUTTON1 ? 3 : 4); break;
		break;
	case WM_MOUSELEAVE:
		if(tracking_mouse) {
			tracking_mouse = false;
			mouse_disconnected();
		}
		break;
	}

	return DefWindowProc(hwnd,msg,wParam,lParam);
}
