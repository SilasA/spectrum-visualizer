/* Primary access point between Winamp and this plugin. Everything needed to create
 * the plugin is defined here.
 */

#include "pch.h"
#include <windows.h>
#include "vis_visualizer.h"
#include "Spectrum.h"
#include "Background.h"
#include <gl/gl.h>
#include <gl/glu.h>
#include <Winamp/wa_ipc.h>

char description[] = "Spectrum Visualizer";

// Visualizer data
static winampVisualizer _vis = {
	{
		VERSION,
		description,
		getModule
	},
	{
		description,
		NULL,
		NULL,
		0,
		0,
		0,
		RENDER_PERIOD,
		2,
		2,
		{ 0, },
		{ 0, },
		config,
		init,
		render,
		quit
	},

	NULL,
	NULL,
	NULL
};

// Visualization module
Spectrum _spectrumVis(
	getModule(0),
	{ 
		40, 
		DEFAULT_WINDOW_HEIGHT - 25, 
		DEFAULT_WINDOW_WIDTH - 40, 
		25 
	},
	512
);

Background _background(
	getModule(0),
	{
		40,
		DEFAULT_WINDOW_HEIGHT - 25,
		DEFAULT_WINDOW_WIDTH - 40,
		25
	}
);

winampVisualizer* getVisInstance() {
	return &_vis;
}

winampVisModule* getModule(int which) {
	switch (which) {
	case 0:
		return &_vis.mod;
	default:
		return NULL;
	}
}

static HINSTANCE GetMyInstance() {
	MEMORY_BASIC_INFORMATION mbi = { 0 };
	if (VirtualQuery(GetMyInstance, &mbi, sizeof(mbi)))
		return (HINSTANCE)mbi.AllocationBase;
	return NULL;
}

void config(struct winampVisModule* this_mod) {
#ifdef DEBUG
	MessageBox(this_mod->hwndParent, L"Configuration...", L"", MB_OK);
#endif
}

// Initialize the window and the plugin
int init(struct winampVisModule* this_mod) {
	int styles;
	HWND parent = NULL;
	WNDCLASS windowClass;
	HWND(*e)(embedWindowState* v);

	PIXELFORMATDESCRIPTOR pfd;
	int nPixelFormat;

	getVisInstance()->windowState.flags |= EMBED_FLAGS_NOTRANSPARENCY;
	getVisInstance()->windowState.r.left = 50;
	getVisInstance()->windowState.r.top = 50;
	getVisInstance()->windowState.r.right = 50 + DEFAULT_WINDOW_WIDTH;
	getVisInstance()->windowState.r.bottom = 50 + DEFAULT_WINDOW_HEIGHT;

	*(void**)&e = (void*)SendMessage(this_mod->hwndParent, WM_WA_IPC, (LPARAM)0, IPC_GET_EMBEDIF);

	if (!e) {
		MessageBox(this_mod->hwndParent, L"This plugin requires Winamp 5.0+.", L"Error", MB_OK | MB_ICONERROR);
		return FAILURE;
	}

	parent = e(&getVisInstance()->windowState);

	SetWindowText(getVisInstance()->windowState.me, (LPCWSTR)this_mod->description);

	memset(&windowClass, 0, sizeof(windowClass));
	windowClass.lpfnWndProc = WndProc;
	windowClass.hInstance = this_mod->hDllInstance;
	windowClass.lpszClassName = L"Visualizer";

	if (!RegisterClass(&windowClass)) {
		MessageBox(this_mod->hwndParent, L"Error registering window class.", L"Error", MB_OK | MB_ICONERROR);
		return FAILURE;
	}

	styles = WS_VISIBLE | WS_CHILDWINDOW | WS_OVERLAPPED | WS_CLIPCHILDREN | WS_CLIPSIBLINGS
		| CS_HREDRAW | CS_VREDRAW | CS_OWNDC;

	getVisInstance()->hWnd = CreateWindowEx(
		0,
		L"Visualizer",
		NULL,
		styles,
		0, 0,
		DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_WIDTH,
		parent,
		NULL,
		this_mod->hDllInstance,
		0);

	if (!getVisInstance()->hWnd) {
		MessageBox(this_mod->hwndParent, L"Error while creating window.", L"Error", MB_OK | MB_ICONERROR);
		return FAILURE;
	}

	// Set window visible and requestion the plugin directory path
	SendMessage(this_mod->hwndParent, WM_WA_IPC, (WPARAM)getVisInstance()->hWnd, IPC_SETVISWND);
	char* dir = (char *)SendMessage(this_mod->hwndParent, WM_WA_IPC, 0, IPC_GETPLUGINDIRECTORY);

	// Setup OpenGL in the window
	if (!(getVisInstance()->hDC = GetDC(getVisInstance()->hWnd))) {
		return FAILURE;
	}

	memset(&pfd, 0, sizeof(pfd));
	pfd.nSize		= sizeof(pfd);
	pfd.nVersion	= 1;
	pfd.dwFlags		= PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType	= PFD_TYPE_RGBA;
	pfd.cColorBits	= 16;
	pfd.cDepthBits	= 16;
	pfd.iLayerType	= PFD_MAIN_PLANE;

	nPixelFormat = ChoosePixelFormat(getVisInstance()->hDC, &pfd);
	SetPixelFormat(getVisInstance()->hDC, nPixelFormat, &pfd);

	if (!(getVisInstance()->hRC = wglCreateContext(getVisInstance()->hDC))) {
		return FAILURE;
	}

	if (!wglMakeCurrent(getVisInstance()->hDC, getVisInstance()->hRC)) {
		return FAILURE;
	}

	// Setup graphics environment
	glShadeModel(GL_SMOOTH);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth(1.0f);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	
	resizeWindow(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT);

	ShowWindow(parent, SW_SHOWNORMAL);
	
	// Initialize graphics
	if (_spectrumVis.Init(dir)) {
		MessageBox(this_mod->hwndParent, L"Spectrum Init Failed", L"", MB_OK);
		return FAILURE;
	}

	if (_background.Init()) {
		MessageBox(this_mod->hwndParent, L"Background Init Failed", L"", MB_OK);
		return FAILURE;
	}

	return SUCCESS;
}

// Render plugin graphics
// Called periodically by plugin host
int render(struct winampVisModule* this_mod) {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	// This can remain the default because the window resize just scales.
	glOrtho(
		0, DEFAULT_WINDOW_WIDTH,
		0, DEFAULT_WINDOW_HEIGHT,
		0, 1);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	// Render
	if (_background.Render() || _spectrumVis.Render())
		return FAILURE;

	glFlush();
	SwapBuffers(getVisInstance()->hDC);

	return SUCCESS;
}

void quit(struct winampVisModule* this_mod) {
	// Stop/dispose of graphics
	_spectrumVis.Quit();
	_background.Quit();

	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(getVisInstance()->hRC);
	ReleaseDC(getVisInstance()->hWnd, getVisInstance()->hDC);

	getVisInstance()->hDC = NULL;
	getVisInstance()->hRC = NULL;

	SendMessage(this_mod->hwndParent, WM_WA_IPC, 0, IPC_SETVISWND);

	if (getVisInstance()->windowState.me) {
		SetForegroundWindow(getVisInstance()->mod.hwndParent);
		DestroyWindow(getVisInstance()->windowState.me);
	}

	UnregisterClass(L"Visualizer", this_mod->hDllInstance);

#ifdef DEBUG
	MessageBox(this_mod->hwndParent, L"Quitting...", L"", MB_OK);
#endif
}

// Recompute environment upon window resize
void resizeWindow(int width, int height) {
	if (height == 0)
		height = 1;

	glViewport(0, 0, width, height);
	
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(45.0f, (float)width / (float)height, 0.1f, 1000);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	
}

// Window event handler
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		//TODO: handle more specific messages as needed

	case WM_SYSCOMMAND:
		switch (wParam)
		{
		case SC_SCREENSAVE:
		case SC_MONITORPOWER:
			return 0;
		}
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_SIZE:
	
		resizeWindow(LOWORD(lParam), HIWORD(lParam));
		return 0;
	
	case WM_MOVE:

		return 0;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

// Export plugin header
extern "C" __declspec(dllexport) winampVisHeader* winampVisGetHeader(HWND hwndParent) {
	return &_vis.hdr;
}
