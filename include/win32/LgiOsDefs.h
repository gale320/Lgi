//
//      FILE:           LgiOsDefs.h (Win32)
//      AUTHOR:         Matthew Allen
//      DATE:           24/9/99
//      DESCRIPTION:    Lgi Win32 OS defines
//
//      Copyright (C) 1999, Matthew Allen
//              fret@memecode.com
//

#ifndef __LGI_OS_DEFS_H
#define __LGI_OS_DEFS_H

#pragma warning(disable : 4251 4275)

#define WIN32GTK					0
#define WINNATIVE					1
#ifndef WINDOWS
	#error "Define WINDOWS in your project"
#endif
#ifdef _WIN64
	#ifndef WIN64
		#define WIN64				1
	#endif
#else
	#ifndef WIN32
		#define WIN32				1
	#endif
#endif

#include <string.h>
#include "LgiInc.h"
#include "LgiDefs.h"
#include "LgiClass.h"

//////////////////////////////////////////////////////////////////
// Includes
#define WIN32_LEAN_AND_MEAN
#include "winsock2.h"

#include "windows.h"
#include "SHELLAPI.H"
#include "COMMDLG.H"
#include "LgiInc.h"

//////////////////////////////////////////////////////////////////
// Typedefs
typedef HWND				OsWindow;
typedef HWND				OsView;
typedef HANDLE				OsThread;
typedef HANDLE				OsProcess;
typedef char16				OsChar;
typedef HBITMAP				OsBitmap;
typedef HDC					OsPainter;
typedef HFONT				OsFont;
#if _MSC_VER <= 1200 //vc6
typedef unsigned long		ULONG_PTR, *PULONG_PTR;
#define sprintf_s			_snprintf
#endif

typedef BOOL (__stdcall *pSHGetSpecialFolderPathA)(HWND hwndOwner, LPSTR lpszPath, int nFolder, BOOL fCreate);
typedef BOOL (__stdcall *pSHGetSpecialFolderPathW)(HWND hwndOwner, LPWSTR lpszPath, int nFolder, BOOL fCreate);
typedef int (__stdcall *pSHFileOperationA)(LPSHFILEOPSTRUCTA lpFileOp);
typedef int (__stdcall *pSHFileOperationW)(LPSHFILEOPSTRUCTW lpFileOp);
typedef int (__stdcall *p_vscprintf)(const char *format, va_list argptr);

class LgiClass GMessage
{
public:
	HWND hWnd;
	int m;
	WPARAM a;
	LPARAM b;
	
	typedef LPARAM Param;
	typedef LRESULT Result;

	GMessage()
	{
		hWnd = 0;
		m = 0;
		a = 0;
		b = 0;
	}

	GMessage(int M, WPARAM A = 0, LPARAM B = 0)
	{
		hWnd = 0;
		m = M;
		a = A;
		b = B;
	}
	
	int Msg() { return m; }
	WPARAM A() { return a; }
	LPARAM B() { return b; }
};

class LgiClass OsAppArguments
{
	GAutoWString CmdLine;

	void _Default();

public:
	HINSTANCE hInstance;
	DWORD Pid;
	char16 *lpCmdLine;
	int nCmdShow;

	OsAppArguments();
	OsAppArguments(int Args, char **Arg);

	OsAppArguments &operator =(OsAppArguments &p);

	void Set(char *Utf);
	void Set(int Args, char **Arg);
};

//////////////////////////////////////////////////////////////////
// Defines
#define MsgCode(msg)					((msg)->m)
#define MsgA(msg)						((msg)->a)
#define MsgB(msg)						((msg)->b)
#define CreateMsg(m, a, b)				GMessage(m, a, b)
#define IsWin9x							(GApp::Win9x)
#define DefaultOsView(t)				NULL

// Key redefs
#define VK_PAGEUP						VK_PRIOR
#define VK_PAGEDOWN						VK_NEXT
#define VK_BACKSPACE					VK_BACK

// Sleep the current thread
LgiFunc void LgiSleep(DWORD i);

// Process
typedef DWORD							OsProcessId;
LgiExtern HINSTANCE						_lgi_app_instance;
#define LgiProcessInst()				_lgi_app_instance
extern p_vscprintf						lgi_vscprintf;

// Threads
typedef DWORD							OsThreadId;
typedef CRITICAL_SECTION				OsSemaphore;
#define LgiGetCurrentThread()			GetCurrentThreadId()

// Socket/Network
#define ValidSocket(s)					((s) != INVALID_SOCKET)
typedef SOCKET							OsSocket;

// Run the message loop to process any pending messages
#define LgiYield()						GApp::ObjInstance()->Run(false)

#define LGI_GViewMagic					0x14412662
#define LGI_FileDropFormat				"CF_HDROP"
#define LGI_StreamDropFormat			CFSTR_FILEDESCRIPTORW
#define LGI_WideCharset					"ucs-2"
#define LGI_PrintfInt64					"%I64i"
#define LGI_PrintfHex64					"%I64x"
#define LGI_IllegalFileNameChars		"\t\r\n/\\:*?\"<>|"

#define MK_LEFT							MK_LBUTTON
#define MK_RIGHT						MK_RBUTTON
#define MK_MIDDLE						MK_MBUTTON
#define MK_CTRL							MK_CONTROL
	
// Stupid mouse wheel defines don't work. hmmm
#define WM_MOUSEWHEEL					0x020A
#define WHEEL_DELTA						120
#ifndef SPI_GETWHEELSCROLLLINES
#define SPI_GETWHEELSCROLLLINES			104
#endif

// Window flags
#define GWF_VISIBLE						WS_VISIBLE
#define GWF_DISABLED					WS_DISABLED
#define GWF_BORDER						WS_BORDER
#define GWF_TABSTOP						WS_TABSTOP
#define GWF_FOCUS						0x00000001
#define GWF_OVER						0x00000002
#define GWF_DROP_TARGET					0x00000004
#define GWF_SUNKEN						0x00000008
#define GWF_FLAT						0x00000010
#define GWF_RAISED						0x00000020
#define GWF_DIALOG						0x00000040
#define GWF_DESTRUCTOR					0x00000080
#define GWF_QUIT_WND					0x00000100
#define GWF_SYS_BORDER					0x00000200 // ask the system to draw the border

// Widgets
#define DialogToPixelX(i)				(((i)*Bx)/4)
#define DialogToPixelY(i)				(((i)*By)/8)
#define PixelToDialogX(i)				(((i)*4)/Bx)
#define PixelToDialogY(i)				(((i)*8)/By)

#define DIALOG_X						1.56
#define DIALOG_Y						1.85
#define CTRL_X							1.50
#define CTRL_Y							1.64

// Messages

// Quite a lot of windows stuff uses WM_USER+n where
// n < 0x1A0 or so... so stay out of their way.
	#define M_USER						WM_USER
	#define M_CUT						WM_CUT
	#define M_COPY						WM_COPY
	#define M_PASTE						WM_PASTE
	#define M_COMMAND					WM_COMMAND
	#define M_CLOSE						WM_CLOSE

	// wParam = bool Inside; // is the mouse inside the client area?
	// lParam = MAKELONG(x, y); // mouse location
	#define M_MOUSEENTER				(M_USER+0x1000)
	#define M_MOUSEEXIT					(M_USER+0x1001)

	// wParam = (GView*) Wnd;
	// lParam = (int) Flags;
	#define M_CHANGE					(M_USER+0x1002)

	// wParam = (GView*) Wnd;
	// lParam = (char*) Text; // description from window
	#define M_DESCRIBE					(M_USER+0x1003)

	// return (bool)
	#define M_WANT_DIALOG_PROC			(M_USER+0x1004)

	// wParam = void
	// lParam = (MSG*) Msg;
	#define M_TRANSLATE_ACCELERATOR		(M_USER+0x1005)

	// None
	#define M_TRAY_NOTIFY				(M_USER+0x1006)

	// lParam = Style
	#define M_SET_WND_STYLE				(M_USER+0x1007)

	// lParam = GScrollBar *Obj
	#define M_SCROLL_BAR_CHANGED		(M_USER+0x1008)

	// lParam = HWND of window under mouse
	// This is only sent for non-LGI window in our process
	// because we'd get WM_MOUSEMOVE's for our own windows
	#define M_HANDLEMOUSEMOVE			(M_USER+0x1009)

	// Calls SetWindowPlacement...
	#define M_SET_WINDOW_PLACEMENT		(M_USER+0x100a)

	// Generic delete selection msg
	#define M_DELETE					(M_USER+0x100b)

	// Log message back to GUI thread
	#define M_LOG_TEXT					(M_USER+0x100c)
	
	// Set the visibility of the window
	#define M_SET_VISIBLE				(M_USER+0x100d)

	/// GThreadWork object completed
	///
	/// MsgA = (GThreadOwner*) Owner;
	/// MsgB = (GThreadWork*) WorkUnit;
	// #define M_GTHREADWORK_COMPELTE		(M_USER+0x100c)

// Directories
#define DIR_CHAR						'\\'
#define DIR_STR							"\\"
#define EOL_SEQUENCE					"\r\n"

#define LGI_PATH_SEPARATOR				";"
#define LGI_ALL_FILES					"*.*"
#define LGI_LIBRARY_EXT					"dll"
#define LGI_EXECUTABLE_EXT				".exe"

/////////////////////////////////////////////////////////////////////////////////////
// Typedefs
typedef HWND OsView;

/////////////////////////////////////////////////////////////////////////////////////
// Externs
LgiFunc class GViewI *GWindowFromHandle(OsView hWnd);
LgiFunc int GetMouseWheelLines();
LgiFunc int WinPointToHeight(int Pt, HDC hDC = NULL);
LgiFunc int WinHeightToPoint(int Ht, HDC hDC = NULL);
LgiExtern class GString WinGetSpecialFolderPath(int Id);

/// Convert a string d'n'd format to an OS dependant integer.
LgiFunc int FormatToInt(char *s);
/// Convert a Os dependant integer d'n'd format to a string.
LgiFunc char *FormatToStr(int f);
extern bool LgiToWindowsCursor(LgiCursor Cursor);

#ifdef _MSC_VER
#define snprintf					_snprintf
//#define vsnprintf					_vsnprintf
#define vsnwprintf					_vsnwprintf
#endif
#define atoi64						_atoi64

#ifdef __GNUC__
// #define stricmp							strcasecmp
// #define strnicmp						strncasecmp
#define vsnprintf_s vsnprintf
#define swprintf_s snwprintf
#define vsprintf_s vsnprintf
#if !defined(_TRUNCATE)
#define _TRUNCATE ((size_t)-1)
#endif
#endif

#endif
