#include <Windows.h>
#include <gl\gl.h>

#include "engine_platform.h"
#include "engine_debug.h"

#include "win32_render_opengl.h"

#include "engine_debug_internal.h"

#include "game.h"

// NOTE(final): WGL definitions
#define PFNWGLSWAPINTERVALPROC(name) BOOL name(int value)
typedef PFNWGLSWAPINTERVALPROC(wgl_swap_interval);

// NOTE(final): Global variables
global_variable B32 globalRunning;
global_variable S64 globalPerfCounterFrequency;
global_variable wgl_swap_interval *wglSwapIntervalEXT;

DebugTable *globalDebugTable = 0;
DebugMemory *globalDebugMemory = 0;

inline LARGE_INTEGER Win32GetWallClock() {
	LARGE_INTEGER result;
	QueryPerformanceCounter(&result);
	return(result);
}

inline F32 Win32GetSecondsElapsed(LARGE_INTEGER start, LARGE_INTEGER end) {
	F32 result = (F32)(end.QuadPart - start.QuadPart) / (F32)globalPerfCounterFrequency;
	return(result);
}

internal B32 Win32SetPixelFormat(HDC deviceContext) {
	PIXELFORMATDESCRIPTOR pfd = {};
	pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DOUBLEBUFFER | PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.cAlphaBits = 8;
	pfd.iLayerType = PFD_MAIN_PLANE;

	int nPixelFormat = ChoosePixelFormat(deviceContext, &pfd);
	if (nPixelFormat == 0) {
		return false;
	}

	if (!SetPixelFormat(deviceContext, nPixelFormat, &pfd)) {
		return false;
	}

	return true;
}

internal void Win32LoadWGLExtensions(void) {
	WNDCLASSA windowClass = {};
	windowClass.lpfnWndProc = DefWindowProcA;
	windowClass.hInstance = GetModuleHandle(0);
	windowClass.lpszClassName = "EditorWGLLoader";

	if (RegisterClassA(&windowClass)) {
		HWND window = CreateWindowExA(
			0,
			windowClass.lpszClassName,
			"Editor WGL Loader",
			0,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			0,
			0,
			windowClass.hInstance,
			0);

		HDC WindowDC = GetDC(window);
		Win32SetPixelFormat(WindowDC);
		HGLRC OpenGLRC = wglCreateContext(WindowDC);
		if (wglMakeCurrent(WindowDC, OpenGLRC)) {
			wglSwapIntervalEXT = (wgl_swap_interval *)wglGetProcAddress("wglSwapIntervalEXT");
			wglMakeCurrent(0, 0);
		}

		wglDeleteContext(OpenGLRC);
		ReleaseDC(window, WindowDC);
		DestroyWindow(window);
	}
}

internal B32 Win32OpenGLLoad(HDC deviceContext, HGLRC *renderingContext) {
	Win32LoadWGLExtensions();

	if (!Win32SetPixelFormat(deviceContext)) {
		return false;
	}

	HGLRC rc = wglCreateContext(deviceContext);
	if (!rc) {
		return false;
	}

	if (!wglMakeCurrent(deviceContext, rc)) {
		wglDeleteContext(rc);
		return false;
	}

	if (wglSwapIntervalEXT) {
		wglSwapIntervalEXT(1);
	}

	Win32RenderOpenGLInit();

	*renderingContext = rc;

	return 1;
}

internal void Win32OpenGLRelease(HGLRC *renderingContext) {
	wglMakeCurrent(NULL, NULL);
	if (renderingContext) {
		wglDeleteContext(*renderingContext);
		*renderingContext = 0;
	}
}

internal void Win32ProcessKeyboardMessage(ButtonState *newState, B32 isDown) {
	if (newState->endedDown != isDown) {
		newState->endedDown = isDown;
		++newState->halfTransitionCount;
	}
}
internal void Win32ProcessMessages(InputState *input) {
	MSG msg;
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		switch (msg.message) {
			case WM_QUIT:
			case WM_CLOSE:
			{
				globalRunning = false;
			}; break;
			case WM_SYSKEYDOWN:
			case WM_SYSKEYUP:
			case WM_KEYDOWN:
			case WM_KEYUP:
			{
				U64 keyCode = msg.wParam;
				B32 altKeyWasDown = (msg.lParam & (1 << 29));
				B32 wasDown = ((msg.lParam & (1 << 30)) != 0);
				B32 isDown = ((msg.lParam & (1 << 31)) == 0);

				if (wasDown != isDown) {
					switch (keyCode) {
						case VK_SPACE:
							Win32ProcessKeyboardMessage(&input->keyboard.space, isDown);
							break;
						case VK_F1:
						case VK_F2:
						case VK_F3:
						case VK_F4:
						case VK_F5:
						case VK_F6:
						case VK_F7:
						case VK_F8:
							Win32ProcessKeyboardMessage(&input->keyboard.functionkeys[keyCode - VK_F1], isDown);
							break;
						case VK_UP:
						case 'W':
							Win32ProcessKeyboardMessage(&input->keyboard.moveUp, isDown);
							break;
						case VK_DOWN:
						case 'S':
							Win32ProcessKeyboardMessage(&input->keyboard.moveDown, isDown);
							break;
						case VK_LEFT:
						case 'A':
							Win32ProcessKeyboardMessage(&input->keyboard.moveLeft, isDown);
							break;
						case VK_RIGHT:
						case 'D':
							Win32ProcessKeyboardMessage(&input->keyboard.moveRight, isDown);
							break;
						default:
							break;
					}

					if (isDown) {
						if (keyCode == VK_F4 && altKeyWasDown) {
							globalRunning = false;
						}
					}
				}
			}; break;
			case WM_MOUSEWHEEL:
			{
				short zDelta = GET_WHEEL_DELTA_WPARAM(msg.wParam);
				input->mouse.wheelDelta = zDelta / (F32)WHEEL_DELTA;
			}; break;
			default:
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}; break;
		}
	}
}

LRESULT CALLBACK Win32MessageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_DESTROY:
		case WM_CLOSE:
			globalRunning = 0;
			break;
		default:
			return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

#define EDITOR_CLASSNAME "SparseEditorC"
#define EDITOR_APPNAME "SparseEditor-C"
#define EDITOR_SCREEN_WIDTH 1280
#define EDITOR_SCREEN_HEIGHT 720

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow) {
	LARGE_INTEGER perfCounterFreqResult;
	QueryPerformanceFrequency(&perfCounterFreqResult);
	globalPerfCounterFrequency = perfCounterFreqResult.QuadPart;

	U32 desiredSchedulerMS = 1;
	B32 sleepIsGranular = timeBeginPeriod(desiredSchedulerMS) == TIMERR_NOERROR;

	WNDCLASSEX wcex = {};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wcex.lpfnWndProc = Win32MessageProc;
	wcex.hInstance = hInstance;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = EDITOR_CLASSNAME;

	if (!RegisterClassEx(&wcex)) {
		return -1;
	}

	HWND windowHandle = CreateWindowEx(
		WS_EX_OVERLAPPEDWINDOW,
		EDITOR_CLASSNAME,
		EDITOR_APPNAME,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, EDITOR_SCREEN_WIDTH, EDITOR_SCREEN_HEIGHT,
		NULL, NULL, hInstance, NULL);
	if (windowHandle == NULL) {
		return -1;
	}

	HDC deviceContext = GetDC(windowHandle);
	if (!deviceContext) {
		return -1;
	}

	HGLRC renderingContext = 0;
	if (!Win32OpenGLLoad(deviceContext, &renderingContext)) {
		return -1;
	}

	// Get monitor/game refresh rate in hz
	int monitorRefreshHz = 60;
	int refreshRate = GetDeviceCaps(deviceContext, VREFRESH);
	if (refreshRate > 1) {
		monitorRefreshHz = refreshRate;
	}
	F32 gameUpdateHz = (F32)monitorRefreshHz;
	F32 targetSecondsPerFrame = 1.0f / gameUpdateHz;

	AppState appState = {};
	appState.renderStorageSize = RENDER_MAX_COMMAND_COUNT * sizeof(RenderCommand);
	appState.persistentStorageSize = MegaBytes(500LL);
	appState.transientStorageSize = MegaBytes(32LL);

#ifdef _DEBUG
	LPVOID baseAddress = (LPVOID)TeraBytes(2ULL);
#else
	LPVOID baseAddress = 0;
#endif
	memory_size totalMemorySize = appState.renderStorageSize + appState.persistentStorageSize + appState.transientStorageSize;
	void *appMemoryBase = VirtualAlloc(baseAddress, totalMemorySize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

	appState.renderStorageBase = appMemoryBase;
	appState.persistentStorageBase = (U8 *)appMemoryBase + appState.renderStorageSize;
	appState.transientStorageBase = (U8 *)appMemoryBase + appState.renderStorageSize + appState.persistentStorageSize;

	MemoryBlock renderMemory = MemoryBlockCreate(appState.renderStorageBase, appState.renderStorageSize);

	RenderState renderState = {};
	renderState.commandCapacity = RENDER_MAX_COMMAND_COUNT;
	renderState.commands = PushArray(&renderMemory, RenderCommand, RENDER_MAX_COMMAND_COUNT);

	void *debugTableBase = VirtualAlloc(0, sizeof(DebugTable), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	globalDebugTable = (DebugTable *)debugTableBase;

	DebugMemory debugMemory = {};
	debugMemory.storageSize = GigaBytes(1LL);
	debugMemory.storageBase = VirtualAlloc(0, debugMemory.storageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	globalDebugMemory = &debugMemory;
	DEBUGInit();

	InputState input[2] = {};
	InputState *newInput = &input[0];
	InputState *oldInput = &input[1];

	F32 deltaTime = 1.0f / 60.0f;

	LARGE_INTEGER flipWallClock = Win32GetWallClock();
	LARGE_INTEGER lastCounter = Win32GetWallClock();

	globalRunning = true;
	ShowWindow(windowHandle, nCmdShow);
	UpdateWindow(windowHandle);
	while (globalRunning) {
		BEGIN_BLOCK("Input processing");
		RECT windowSize;
		GetClientRect(windowHandle, &windowSize);
		renderState.screenSize.w = (windowSize.right - windowSize.left) + 1;
		renderState.screenSize.h = (windowSize.bottom - windowSize.top) + 1;

		// NOTE(final): Reset but preserve keyboard button state from previous frame
		// NOTE(final): Reset mouse states from previous frame
		{
			KeyboardState *oldKeyboardState = &oldInput->keyboard;
			KeyboardState *newKeyboardState = &newInput->keyboard;
			*newKeyboardState = {};
			for (int buttonIndex = 0; buttonIndex < ArrayCount(newKeyboardState->keys); ++buttonIndex) {
				newKeyboardState->keys[buttonIndex].endedDown = oldKeyboardState->keys[buttonIndex].endedDown;
			}
			newInput->mouse.wheelDelta = 0;
		}

		// NOTE(final): Process window/keyboard messages
		Win32ProcessMessages(newInput);

		// NOTE(final): Mouse handling
		{
			POINT mousePos;
			GetCursorPos(&mousePos);
			ScreenToClient(windowHandle, &mousePos);
			S32 mouseX = mousePos.x;
			S32 mouseY = (renderState.screenSize.h - 1) - mousePos.y;
			newInput->mouse.mousePos = RenderUnproject(&renderState, mouseX, mouseY);

			DWORD WinButtonID[MouseButton_Count] =
			{
				VK_LBUTTON,
				VK_MBUTTON,
				VK_RBUTTON,
				VK_XBUTTON1,
				VK_XBUTTON2,
			};
			for (U32 buttonIndex = 0; buttonIndex < MouseButton_Count; ++buttonIndex) {
				newInput->mouse.buttons[buttonIndex] = oldInput->mouse.buttons[buttonIndex];
				newInput->mouse.buttons[buttonIndex].halfTransitionCount = 0;
				Win32ProcessKeyboardMessage(&newInput->mouse.buttons[buttonIndex], GetKeyState(WinButtonID[buttonIndex]) & (1 << 15));
			}
		}
		newInput->deltaTime = deltaTime;
		END_BLOCK();

		// NOTE(final): Update and render editor
		BEGIN_BLOCK("Game Update And Render");
		GameUpdateAndRender(&appState, &renderState, newInput);
		END_BLOCK();

		// NOTE(final): Render frame
		BEGIN_BLOCK("Render Commands");
		Win32RenderOpenGL(&renderState);
		END_BLOCK();

		BEGIN_BLOCK("FrameSleep");
		LARGE_INTEGER workCounter = Win32GetWallClock();
		F32 workSecondsElapsed = Win32GetSecondsElapsed(lastCounter, workCounter);

		F32 secondsElapsedForFrame = workSecondsElapsed;
		if (secondsElapsedForFrame < targetSecondsPerFrame) {
			while (secondsElapsedForFrame < targetSecondsPerFrame) {
				if (sleepIsGranular) {
					DWORD sleepMS = (DWORD)(1000.0f * (targetSecondsPerFrame - secondsElapsedForFrame));
					Sleep(sleepMS);
				}
				secondsElapsedForFrame = Win32GetSecondsElapsed(lastCounter, Win32GetWallClock());
			}
		} else {
			// TODO(final): Missed framerate here
			// TODO(final): Logging
		}
		END_BLOCK();

		// NOTE(final): Present frame and swap input
		BEGIN_BLOCK("Present Frame");
		SwapBuffers(deviceContext);
		flipWallClock = Win32GetWallClock();
		END_BLOCK();

		// NOTE(final): Prepare states for next frame
		renderState.commandCount = 0;
		SwapPtr(InputState, newInput, oldInput);

		// NOTE(final): Finalize Frame
		LARGE_INTEGER endCounter = Win32GetWallClock();
		FRAME_MARKER(Win32GetSecondsElapsed(lastCounter, endCounter));
		lastCounter = endCounter;

		DEBUGFrameEnd();
	}

	Win32OpenGLRelease(&renderingContext);

	ReleaseDC(windowHandle, deviceContext);
	DestroyWindow(windowHandle);
	UnregisterClass(wcex.lpszClassName, wcex.hInstance);

	VirtualFree(globalDebugTable, 0, MEM_RELEASE);
	VirtualFree(appMemoryBase, 0, MEM_RELEASE);

	return 0;
}