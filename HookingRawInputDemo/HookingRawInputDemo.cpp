#include "stdafx.h"
#include "HookingRawInputDemo.h"
#include "HookingRawInputDemoDLL.h"

#define MAX_LOADSTRING 100
#define MAX_PATH_LEN 255

#define KEY_NUM 45
#define KEY_LIST L"1234567890 QWERTYUIOPASDFGHJKLZXCVBNM,.-<\'/*+";
#define CONFIG_FILE ".\\launchpad_config.txt"

typedef struct {
	wchar_t ch;
	wchar_t file[MAX_PATH_LEN];
	HWND button;
	HWND textbox;
} MacroKey;

// Global Variables:
HINSTANCE hInst;								// current instance
TCHAR szTitle[MAX_LOADSTRING];					// The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];			// the main window class name

// HWND of main executable
HWND mainHwnd;
// Windows message for communication between main executable and DLL module
UINT const WM_HOOK = WM_APP + 1;
// How long should Hook processing wait for the matching Raw Input message (ms)
DWORD maxWaitingTime = 100;
// Device name of my numeric keyboard (use escape for slashes)
WCHAR* numericKeyboardDeviceName = NULL;
// Buffer for the decisions whether to block the input with Hook
std::deque<DecisionRecord> decisionBuffer;

// Forward declarations of functions included in this code module:
ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK	About(HWND, UINT, WPARAM, LPARAM);

MacroKey macros[KEY_NUM];
OPENFILENAME ofn = { sizeof ofn };
void load_macros();
void save_macros();
void paired_key(HWND* hwnd);
void play_vkey(USHORT vkey);

int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPTSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

 	// TODO: Place code here.
	MSG msg;
	HACCEL hAccelTable;

	// Initialize global strings
	LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadString(hInstance, IDC_HOOKINGRAWINPUTDEMO, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);

	// Perform application initialization:
	if (!InitInstance (hInstance, nCmdShow))
	{
		return FALSE;
	}

	hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_HOOKINGRAWINPUTDEMO));

	// Main message loop:
	while (GetMessage(&msg, NULL, 0, 0))
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			if (msg.message == WM_LBUTTONUP) {
				for (int i = 0; i < KEY_NUM; i++) {
					if (msg.hwnd == macros[i].button) {
						macros[i].file[0] = '\0';
						ofn.lpstrFile = macros[i].file;
						ofn.nMaxFile = sizeof(macros[i].file);
						GetOpenFileName(&ofn);
						SetWindowText(macros[i].textbox, macros[i].file);
						break;
					}
				}
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return (int) msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= WndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= LoadIcon(hInstance, MAKEINTRESOURCE(IDI_HOOKINGRAWINPUTDEMO));
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName	= MAKEINTRESOURCE(IDC_HOOKINGRAWINPUTDEMO);
	wcex.lpszClassName	= szWindowClass;
	wcex.hIconSm		= LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
	return RegisterClassEx(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	HWND hWnd;
	hInst = hInstance; // Store instance handle in our global variable
	hWnd = CreateWindowEx(NULL, 
		szWindowClass, 
		szTitle, 
		WS_OVERLAPPEDWINDOW, 
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		1050, 750,
		NULL, NULL, 
		hInstance, NULL);
	if (!hWnd)
	{
		return FALSE;
	}
	// Save the HWND
	mainHwnd = hWnd;
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	// Register for receiving Raw Input for keyboards
	RAWINPUTDEVICE rawInputDevice[1];
	rawInputDevice[0].usUsagePage = 1;
	rawInputDevice[0].usUsage = 6;
	rawInputDevice[0].dwFlags = RIDEV_INPUTSINK;
	rawInputDevice[0].hwndTarget = hWnd;
	RegisterRawInputDevices (rawInputDevice, 1, sizeof (rawInputDevice[0]));

	ofn.lpstrFilter = L"Format: WAV\0*.wav\0";
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

	// Set up the keyboard Hook
	InstallHook (hWnd);

	return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	PAINTSTRUCT ps;
	HDC hdc;

	switch (message)
	{
	// Raw Input Message
	case WM_INPUT:
	{
		UINT bufferSize;

		// Prepare buffer for the data
		GetRawInputData ((HRAWINPUT)lParam, RID_INPUT, NULL, &bufferSize, sizeof (RAWINPUTHEADER));
		LPBYTE dataBuffer = new BYTE[bufferSize];
		// Load data into the buffer
		GetRawInputData((HRAWINPUT)lParam, RID_INPUT, dataBuffer, &bufferSize, sizeof (RAWINPUTHEADER));

		RAWINPUT* raw = (RAWINPUT*)dataBuffer;

		// Get the virtual key code of the key and report it
		USHORT virtualKeyCode = raw->data.keyboard.VKey;
		USHORT keyPressed = raw->data.keyboard.Flags & RI_KEY_BREAK ? 0 : 1;
		WCHAR text[128];
		swprintf_s (text, 128, L"Raw Input: %X (%d)\n", virtualKeyCode, keyPressed);
		//OutputDebugString (text);

		// Prepare string buffer for the device name
		GetRawInputDeviceInfo (raw->header.hDevice, RIDI_DEVICENAME, NULL, &bufferSize);
		WCHAR* stringBuffer = new WCHAR[bufferSize];
		// Load the device name into the buffer
		GetRawInputDeviceInfo (raw->header.hDevice, RIDI_DEVICENAME, stringBuffer, &bufferSize);
		if (numericKeyboardDeviceName == NULL) {
			numericKeyboardDeviceName = (WCHAR*) malloc(bufferSize * sizeof(wchar_t));
			wcscpy_s(numericKeyboardDeviceName, bufferSize, stringBuffer);
		}
		//OutputDebugString(stringBuffer);
		// and remember the decision whether to block the input
		if (wcscmp (stringBuffer, numericKeyboardDeviceName) == 0)
		{
			decisionBuffer.push_back (DecisionRecord (virtualKeyCode, TRUE));
		}
		else
		{
			decisionBuffer.push_back (DecisionRecord (virtualKeyCode, FALSE));
		}

		delete[] stringBuffer;
		delete[] dataBuffer; 
		return 0;
	}

	// Message from Hooking DLL
	case WM_HOOK:
	{
		USHORT virtualKeyCode = (USHORT)wParam;
		USHORT keyPressed = lParam & 0x80000000 ? 0 : 1;
		WCHAR text[128];
		swprintf_s (text, 128, L"Hook: %X (%d)\n", virtualKeyCode, keyPressed);
		//OutputDebugString (text);

		// Check the buffer if this Hook message is supposed to be blocked; return 1 if it is
		BOOL blockThisHook = FALSE;
		BOOL recordFound = FALSE;
		int index = 1;
		if (!decisionBuffer.empty ())
		{
			// Search the buffer for the matching record
			std::deque<DecisionRecord>::iterator iterator = decisionBuffer.begin ();
			while (iterator != decisionBuffer.end ())
			{
				if (iterator->virtualKeyCode == virtualKeyCode)
				{
					blockThisHook = iterator->decision;
					recordFound = TRUE;
					// Remove this and all preceding messages from the buffer
					for (int i = 0; i < index; ++i)
					{
						decisionBuffer.pop_front ();
					}
					// Stop looking
					break;
				}
				++iterator;
				++index;
			}
		}

		// Wait for the matching Raw Input message if the decision buffer was empty or the matching record wasn't there
		DWORD currentTime, startTime;
		startTime = GetTickCount ();
		while (!recordFound)
		{
			MSG rawMessage;
			while (!PeekMessage (&rawMessage, mainHwnd, WM_INPUT, WM_INPUT, PM_REMOVE))
			{
				// Test for the maxWaitingTime
				currentTime = GetTickCount ();
				// If current time is less than start, the time rolled over to 0
				if ((currentTime < startTime ? ULONG_MAX - startTime + currentTime : currentTime - startTime) > maxWaitingTime)
				{
					// Ignore the Hook message, if it exceeded the limit
					WCHAR text[128];
					swprintf_s (text, 128, L"Hook TIMED OUT: %X (%d)\n", virtualKeyCode, keyPressed);
					//OutputDebugString (text);
					return 0;
				}
			}

			// The Raw Input message has arrived; decide whether to block the input
			UINT bufferSize;

			// Prepare buffer for the data
			GetRawInputData ((HRAWINPUT)rawMessage.lParam, RID_INPUT, NULL, &bufferSize, sizeof (RAWINPUTHEADER));
			LPBYTE dataBuffer = new BYTE[bufferSize];
			// Load data into the buffer
			GetRawInputData((HRAWINPUT)rawMessage.lParam, RID_INPUT, dataBuffer, &bufferSize, sizeof (RAWINPUTHEADER));
			RAWINPUT* raw = (RAWINPUT*)dataBuffer;
			// Get the virtual key code of the key and report it
			USHORT rawVirtualKeyCode = raw->data.keyboard.VKey;
			USHORT rawKeyPressed = raw->data.keyboard.Flags & RI_KEY_BREAK ? 0 : 1;
			WCHAR text[128];
			swprintf_s (text, 128, L"Raw Input WAITING: %X (%d)\n", rawVirtualKeyCode, rawKeyPressed);
			//OutputDebugString (text);

			// Prepare string buffer for the device name
			GetRawInputDeviceInfo (raw->header.hDevice, RIDI_DEVICENAME, NULL, &bufferSize);
			WCHAR* stringBuffer = new WCHAR[bufferSize];
			// Load the device name into the buffer
			GetRawInputDeviceInfo (raw->header.hDevice, RIDI_DEVICENAME, stringBuffer, &bufferSize);

			// If the Raw Input message doesn't match the Hook, push it into the buffer and continue waiting
			if (virtualKeyCode != rawVirtualKeyCode)
			{
				// decide whether to block the input
				if (wcscmp (stringBuffer, numericKeyboardDeviceName) == 0)
				{
					decisionBuffer.push_back (DecisionRecord (rawVirtualKeyCode, TRUE));
				}
				else
				{
					decisionBuffer.push_back (DecisionRecord (rawVirtualKeyCode, FALSE));
				}
			}
			else
			{
				// This is correct Raw Input message
				recordFound = TRUE;
				// decide whether to block the input
				if (wcscmp (stringBuffer, numericKeyboardDeviceName) == 0)
				{
					blockThisHook = TRUE;
				}
				else
				{
					blockThisHook= FALSE;
				}
			}
			delete[] stringBuffer;
			delete[] dataBuffer;
		}
		// Apply the decision
		if (blockThisHook)
		{
			play_vkey(virtualKeyCode);
			swprintf_s (text, 128, L"Keyboard event: %X (%d) is being blocked!\n", virtualKeyCode, keyPressed);
			//OutputDebugString (text);
			return 1;
		}
		return 0;
	}

	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		// Parse the menu selections:
		switch (wmId)
		{
		case IDM_ABOUT:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
			break;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		EndPaint(hWnd, &ps);
		break;
	case WM_DESTROY:
		save_macros();
		UninstallHook();
		PostQuitMessage(0);
		break;
	case WM_CREATE:
		MessageBox(hWnd, L"Press ENTER to register the launchpad keyboard", L"Welcome", MB_OK);
		load_macros();
		paired_key(&hWnd);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

void play_vkey(USHORT vkey) {
	char c = MapVirtualKeyA(vkey, MAPVK_VK_TO_CHAR);
	if (c == 0)
		return;
	for (int i = 0; i < KEY_NUM; i++) {
		if (macros[i].ch == c) {
			wchar_t stop_command[MAX_PATH_LEN] = L"stop ";
			wchar_t start_command[MAX_PATH_LEN] = L"play ";
			wcsncat_s(stop_command, macros[i].file, wcslen(macros[i].file));
			wcsncat_s(start_command, macros[i].file, wcslen(macros[i].file));
			mciSendString(stop_command, NULL, 0, NULL);
			mciSendString(start_command, NULL, 0, NULL);
			break;
		}
	}
}

void paired_key(HWND* hwnd) {
	for (int i = 0; i<KEY_NUM; i++) {
		wchar_t s[2];
		s[0] = macros[i].ch; s[1] = '\0';
		macros[i].button = CreateWindow(L"button", s,
			WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
			25 + (i / 25) * 500, 25 + (i % 25) * 25,
			50, 20,
			*hwnd, (HMENU)1000+i,
			hInst, NULL);
		macros[i].textbox = CreateWindow(L"EDIT", macros[i].file, 
			WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL | ES_WANTRETURN, 
			100 + (i / 25) * 500, 25 + (i % 25) * 25,
			400, 20,
			*hwnd, (HMENU)1200+i,
			hInst, NULL);
	}
}

void load_macros() {
	char buf[MAX_PATH_LEN];
	FILE* f;
	fopen_s(&f, CONFIG_FILE, "r");
	// no config found
	const wchar_t* list = KEY_LIST;
	for (int i = 0; i < KEY_NUM; i++) {
		macros[i].ch = list[i];
		if (f == NULL)
			macros[i].file[0] = '\0';
		else {
			fgets(buf, sizeof buf, f);
			buf[strlen(buf) - 1] = '\0'; //remove \n
			swprintf(macros[i].file, MAX_PATH_LEN, L"%hs", buf);
		}
	}
	if (f != NULL)
		fclose(f);
}

void save_macros() {
	FILE* f;
	auto err = fopen_s(&f, CONFIG_FILE, "w+");
	if (err) {
		auto e = GetLastError();
		return;
	}
	for (int i = 0; i<KEY_NUM; i++) {
		char buf[MAX_PATH_LEN];
		sprintf_s(buf, "%ws", macros[i].file);
		fprintf_s(f, "%s\n", buf);
	}
	fclose(f);
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}
