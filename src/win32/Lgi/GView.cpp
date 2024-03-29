/*hdr
**      FILE:           GView.cpp
**      AUTHOR:         Matthew Allen
**      DATE:           23/4/98
**      DESCRIPTION:    Win32 GView Implementation
**
**      Copyright (C) 1998-2003, Matthew Allen
**              fret@memecode.com
*/

#include <stdio.h>
#include <time.h>


#include "Lgi.h"
#include "Base64.h"
#include "GCom.h"
#include "GDragAndDrop.h"
#include "GDropFiles.h"
#include "GdiLeak.h"
#include "GViewPriv.h"
#include "GCss.h"

#define DEBUG_OVER				0
#define OLD_WM_CHAR_MODE		1
#define GWL_LGI_MAGIC			8
#define GWL_EXTRA_BYTES			12

////////////////////////////////////////////////////////////////////////////////////////////////////
bool In_SetWindowPos = false;
HWND GViewPrivate::hPrevCapture = 0;

GViewPrivate::GViewPrivate()
{
	Font = 0;
	FontOwnType = GV_FontPtr;
	CtrlId = -1;
	WndStyle = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN;
	WndExStyle = 0;
	WndClass = 0;
	WndDlgCode = 0;
	TimerId = 0;
	DropTarget = NULL;
	DropSource = NULL;
	Parent = 0;
	ParentI = 0;
	Notify = 0;
	hTheme = NULL;
	IsThemed = true;
}

GViewPrivate::~GViewPrivate()
{
	if (hTheme)
	{
		CloseThemeData(hTheme);
		hTheme = NULL;
	}
	if (FontOwnType == GV_FontOwned)
	{
		DeleteObj(Font);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper Stuff
#include "zmouse.h"

int MouseRollMsg = 0;

#ifdef __GNUC__
#define MSH_WHEELMODULE_CLASS	"MouseZ"
#define MSH_WHEELMODULE_TITLE	"Magellan MSWHEEL"
#define MSH_SCROLL_LINES		"MSH_SCROLL_LINES_MSG" 
#endif

int _lgi_mouse_wheel_lines()
{
	OSVERSIONINFO Info;
	ZeroObj(Info);
	Info.dwOSVersionInfoSize = sizeof(Info);
	if (GetVersionEx(&Info) &&
		Info.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS &&
		Info.dwMajorVersion == 4 &&
		Info.dwMinorVersion == 0)
	{
       HWND hdlMSHWheel=NULL;
       UINT msgMSHWheelGetScrollLines=NULL;
       UINT uiMsh_WheelScrollLines;

       msgMSHWheelGetScrollLines = 
               RegisterWindowMessage(MSH_SCROLL_LINES);
       hdlMSHWheel = FindWindow(MSH_WHEELMODULE_CLASS, 
                                MSH_WHEELMODULE_TITLE);
       if (hdlMSHWheel && msgMSHWheelGetScrollLines)
       {
			return SendMessage(hdlMSHWheel, msgMSHWheelGetScrollLines, 0, 0);
       }
	}
	else
	{
		UINT nScrollLines;
		if (SystemParametersInfo(	SPI_GETWHEELSCROLLLINES, 
									0, 
									(PVOID) &nScrollLines, 
									0))
		{
			return nScrollLines;
		}
	}

	return 3;
}

#define SetKeyFlag(v, k, f)		if (GetKeyState(k)&0xFF00) { v |= f; }

int _lgi_get_key_flags()
{
	int Flags = 0;
	
	if (LgiGetOs() == LGI_OS_WIN9X)
	{
		SetKeyFlag(Flags, VK_MENU, LGI_EF_ALT);

		SetKeyFlag(Flags, VK_SHIFT, LGI_EF_SHIFT);

		SetKeyFlag(Flags, VK_CONTROL, LGI_EF_CTRL);
	}
	else // is NT/2K/XP
	{
		SetKeyFlag(Flags, VK_LMENU, LGI_EF_LALT);
		SetKeyFlag(Flags, VK_RMENU, LGI_EF_RALT);

		SetKeyFlag(Flags, VK_LSHIFT, LGI_EF_LSHIFT);
		SetKeyFlag(Flags, VK_RSHIFT, LGI_EF_RSHIFT);

		SetKeyFlag(Flags, VK_LCONTROL, LGI_EF_LCTRL);
		SetKeyFlag(Flags, VK_RCONTROL, LGI_EF_RCTRL);
	}

	if (GetKeyState(VK_CAPITAL))
		SetFlag(Flags, LGI_EF_CAPS_LOCK);

	return Flags;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
int GetInputACP()
{
	char Str[16];
	LCID Lcid = (NativeInt)GetKeyboardLayout(LgiGetCurrentThread()) & 0xffff;
	GetLocaleInfo(Lcid, LOCALE_IDEFAULTANSICODEPAGE , Str, sizeof(Str));
	return atoi(Str);
}

GKey::GKey(int v, int flags)
{
	const char *Cp = 0;

	vkey = v;
	c16 = 0;

	#if OLD_WM_CHAR_MODE
	
	c16 = vkey;
	
	#else

	typedef int (WINAPI *p_ToUnicode)(UINT, UINT, PBYTE, LPWSTR, int, UINT);

	static bool First = true;
	static p_ToUnicode ToUnicode = 0;

	if (First)
	{
		ToUnicode = (p_ToUnicode) GetProcAddress(LoadLibrary("User32.dll"), "ToUnicode");
		First = false;
	}

	if (ToUnicode)
	{
		BYTE state[256];
		GetKeyboardState(state);
		char16 w[4];
		int r = ToUnicode(vkey, flags & 0x7f, state, w, CountOf(w), 0);
		if (r == 1)
		{
			c16 = w[0];
		}
	}

	#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////
LRESULT CALLBACK GWin32Class::Redir(HWND hWnd, UINT m, WPARAM a, LPARAM b)
{
	if (m == WM_NCCREATE)
	{
		LPCREATESTRUCT Info = (LPCREATESTRUCT) b;
		
		GViewI *ViewI = (GViewI*) Info->lpCreateParams;
		if (ViewI)
		{
			GView *View = ViewI->GetGView();
			if (View) View->_View = hWnd;

			#if _MSC_VER >= 1400
			SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)ViewI);
			#else
			SetWindowLong(hWnd, GWL_USERDATA, (LONG)ViewI);
			#endif
			SetWindowLong(hWnd, GWL_LGI_MAGIC, LGI_GViewMagic);
		}
	}

	GViewI *Wnd = (GViewI*)
		#if _MSC_VER >= 1400
		GetWindowLongPtr(hWnd, GWLP_USERDATA);
		#else
		GetWindowLong(hWnd, GWL_USERDATA);
		#endif
	if (Wnd)
	{
		GMessage Msg(m, a, b);
		Msg.hWnd = hWnd;
		return Wnd->OnEvent(&Msg);
	}

	return DefWindowProcW(hWnd, m, a, b);
}

LRESULT CALLBACK GWin32Class::SubClassRedir(HWND hWnd, UINT m, WPARAM a, LPARAM b)
{
	if (m == WM_NCCREATE)
	{
		LPCREATESTRUCT Info = (LPCREATESTRUCT) b;
		GViewI *ViewI = 0;
		if (Info->lpCreateParams)
		{
			if (ViewI = (GViewI*) Info->lpCreateParams)
			{
				GView *View = ViewI->GetGView();
				if (View)
					View->_View = hWnd;
			}
		}
		#if _MSC_VER >= 1400
		SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR) ViewI);
		#else
		SetWindowLong(hWnd, GWL_USERDATA, (LONG) ViewI);
		#endif
		SetLastError(0);
		SetWindowLong(hWnd, GWL_LGI_MAGIC, LGI_GViewMagic);
		DWORD err = GetLastError();
		LgiAssert(!err);

	}

	GViewI *Wnd = (GViewI*)
		#if _MSC_VER >= 1400
		GetWindowLongPtr(hWnd, GWLP_USERDATA);
		#else
		GetWindowLong(hWnd, GWL_USERDATA);
		#endif
	if (Wnd)
	{
		GMessage Msg(m, a, b);
		Msg.hWnd = hWnd;
		GMessage::Result Status = Wnd->OnEvent(&Msg);
		return Status;
	}

	return DefWindowProcW(hWnd, m, a, b);
}

GWin32Class::GWin32Class(const char *name)
{
	Name(name);

	ZeroObj(Class);

	Class.lpfnWndProc = (WNDPROC) Redir;
	Class.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
	Class.cbWndExtra = GWL_EXTRA_BYTES;
	Class.cbSize = sizeof(Class);

	ParentProc = 0;
}

GWin32Class::~GWin32Class()
{
	UnregisterClassW(NameW(), LgiProcessInst());
	Class.lpszClassName = NULL;
}

GWin32Class *GWin32Class::Create(const char *ClassName)
{
	GWin32Class *c = 0;

	if (LgiApp)
	{
		List<GWin32Class> *Classes = LgiApp->GetClasses();
		if (Classes)
		{
			for (c = Classes->First(); c; c = Classes->Next())
			{
				if (c->Name() &&
					ClassName &&
					stricmp(c->Name(), ClassName) == 0)
				{
					break;
				}
			}

			if (!c)
			{
				c = new GWin32Class(ClassName);
				if (c)
				{
					Classes->Insert(c);
				}
			}
		}
	}

	return c;
}

bool GWin32Class::Register()
{
	bool Status = false;
	if (!Class.lpszClassName)
	{
		Class.hInstance = LgiProcessInst();
		Class.lpszClassName = NameW();
		Status = RegisterClassExW(&Class) != 0;
		LgiAssert(Status);
	}

	return Status;
}

bool GWin32Class::SubClass(char *Parent)
{
	bool Status = false;

	if (!Class.lpszClassName)
	{
		HBRUSH hBr = Class.hbrBackground;
		GAutoWString p(LgiNewUtf8To16(Parent));
		if (p)
		{
			if (GetClassInfoExW(LgiProcessInst(), p, &Class))
			{
				ParentProc = Class.lpfnWndProc;
				if (hBr)
				{
					Class.hbrBackground = hBr;
				}

				Class.cbWndExtra = max(Class.cbWndExtra, GWL_EXTRA_BYTES);
				Class.hInstance = LgiProcessInst();
				Class.lpfnWndProc = (WNDPROC) SubClassRedir;

				Class.lpszClassName = NameW();
				Status = RegisterClassExW(&Class) != 0;
				LgiAssert(Status);
			}
		}
	}
	else Status = true;

	return Status;
}

LRESULT CALLBACK GWin32Class::CallParent(HWND hWnd, UINT m, WPARAM a, LPARAM b)
{
	if (!ParentProc)
		return 0;

	if (IsWindowUnicode(hWnd))
	{
		return CallWindowProcW(ParentProc, hWnd, m, a, b);
	}
	else
	{
		#if _MSC_VER == 1100
		return CallWindowProcA((FARPROC) ParentProc, hWnd, m, a, b);
		#else
		return CallWindowProcA(ParentProc, hWnd, m, a, b);
		#endif
	}
}

//////////////////////////////////////////////////////////////////////////////
GViewI *GWindowFromHandle(HWND hWnd)
{
	if (hWnd)
	{
		SetLastError(0);
		int32 m = GetWindowLong(hWnd, GWL_LGI_MAGIC);
		#if 0 //def _DEBUG
		DWORD err = GetLastError();
		if (err == 1413)
		{
			TCHAR name[256];
			if (GetClassName(hWnd, name, sizeof(name)))
			{
				WNDCLASSEX cls;
				ZeroObj(cls);
				cls.cbSize = sizeof(WNDCLASSEX);
				if (GetClassInfoEx(LgiApp->GetInstance(), name, &cls))
				{
					if (cls.cbWndExtra >= 8)
					{
						LgiAssert(!"Really?");
					}
				}
			}
		}
		#endif

		if (m == LGI_GViewMagic)
		{
			return (GViewI*)
				#if _MSC_VER >= 1400
				GetWindowLongPtr(hWnd, GWLP_USERDATA);
				#else
				GetWindowLong(hWnd, GWL_USERDATA);
				#endif
		}
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////////
void GView::_Delete()
{
	if (_View && d->DropTarget)
	{
		RevokeDragDrop(_View);
	}

	GViewI *c;
	#ifdef _DEBUG
	// Sanity check..
	// GArray<GViewI*> HasView;
	for (c = Children.First(); c; c = Children.Next())
	{
		// LgiAssert(!HasView.HasItem(c));
		// HasView.Add(c);
		LgiAssert(((GViewI*)c->GetParent()) == this || c->GetParent() == 0);
	}
	#endif

	// Delete myself out of my parent's list
	if (d->Parent)
	{
		d->Parent->OnChildrenChanged(this, false);
		d->Parent->DelView(this);
		d->Parent = 0;
		d->ParentI = 0;
	}

	// Delete all children
	while (c = Children.First())
	{
		// If it has no parent, remove the pointer from the child list
		if (c->GetParent() == 0)
			Children.Delete(c);

		// Delete the child view
		DeleteObj(c);
	}

	// Delete the OS representation of myself
	if (_View && IsWindow(_View))
	{
		WndFlags |= GWF_DESTRUCTOR;
		BOOL Status = DestroyWindow(_View);
		LgiAssert(Status);
	}
	
	// NULL my handles and flags
	_View = 0;
	WndFlags = 0;

	// Remove static references to myself
	if (_Over == this) _Over = 0;
	if (_Capturing == this) _Capturing = 0;

	if (LgiApp && LgiApp->AppWnd == this)
	{
		LgiApp->AppWnd = 0;
	}

	GWindow *Wnd = GetWindow();
	if (Wnd)
	{
		Wnd->SetFocus(this, GWindow::ViewDelete);
	}

	// this should only exist in an ex-GWindow, due to the way
	// C++ deletes objects it needs to be here.
	DeleteObj(_Lock);
}

void GView::Quit(bool DontDelete)
{
	if (_View)
	{
		if (!DontDelete)
		{
			WndFlags |= GWF_QUIT_WND;
		}

		DestroyWindow(_View);
	}
}

uint32 GView::GetDlgCode()
{
	return d->WndDlgCode;
}

void GView::SetDlgCode(uint32 i)
{
	d->WndDlgCode = i;
}

uint32 GView::GetStyle()
{
	return d->WndStyle;
}

void GView::SetStyle(uint32 i)
{
	d->WndStyle = i;
}

uint32 GView::GetExStyle()
{
	return d->WndExStyle;
}

void GView::SetExStyle(uint32 i)
{
	d->WndExStyle = i;
}

const char *GView::GetClassW32()
{
	return d->WndClass;
}

void GView::SetClassW32(const char *c)
{
	d->WndClass = c;
}

GWin32Class *GView::CreateClassW32(const char *Class, HICON Icon, int AddStyles)
{
	if (Class)
	{
		SetClassW32(Class);
	}

	if (GetClassW32())
	{
		GWin32Class *c = GWin32Class::Create(GetClassW32());
		if (c)
		{
			if (Icon)
			{
				c->Class.hIcon = Icon;
			}

			if (AddStyles)
			{
				c->Class.style |= AddStyles;
			}

			c->Register();
			return c;
		}
	}

	return 0;
}

bool GView::IsAttached()
{
	return _View && IsWindow(_View);
}

bool GView::Attach(GViewI *p)
{
	bool Status = false;

	SetParent(p);
	GView *Parent = d->GetParent();
	if (Parent && !_Window)
		_Window = Parent->_Window;

    const char *ClsName = GetClassW32();
    if (!ClsName)
        ClsName = GetClass();
	if (ClsName)
	{
		// Real window with HWND
		bool Enab = Enabled();

        // Check the class is created
        GWin32Class *Cls = GWin32Class::Create(ClsName);
        if (Cls)
            Cls->Register();
        else
            return false;

		LgiAssert(!Parent || Parent->Handle() != 0);

		DWORD Style	  = GetStyle();
		DWORD ExStyle = GetExStyle() & ~WS_EX_CONTROLPARENT;
		if (!TestFlag(WndFlags, GWF_SYS_BORDER))
			ExStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE);
							
		char16 *Text = GBase::NameW();

		_View = CreateWindowExW(ExStyle,
								Cls->NameW(),
								Text,
								Style,
								Pos.x1, Pos.y1,
								Pos.X(), Pos.Y(),
								Parent ? Parent->Handle() : 0,
								NULL,
								LgiProcessInst(),
								(GViewI*) this);

		#if 1 // def _DEBUG
		if (!_View)
		{
			DWORD e = GetLastError();
			LgiTrace("%s:%i - CreateWindowExW failed with 0x%x\n", _FL, e);
			LgiAssert(!"CreateWindowEx failed");
		}
		#endif

		if (_View)
		{
			Status = (_View != NULL);

			if (d->Font)
				SendMessage(_View, WM_SETFONT, (WPARAM) d->Font->Handle(), 0);

			if (d->DropTarget)
				RegisterDragDrop(_View, d->DropTarget);
			
			if (TestFlag(WndFlags, GWF_FOCUS))
				SetFocus(_View);
		}

		OnAttach();

	}
	else
	{
		// Virtual window (no HWND)
		Status = true;
	}

	if (Status && d->Parent)
	{
		if (!d->Parent->HasView(this))
		{
			d->Parent->AddView(this);
		}
		d->Parent->OnChildrenChanged(this, true);
	}

	return Status;
}

bool GView::Detach()
{
	bool Status = false;
	if (d->Parent)
	{
		Visible(false);
		d->Parent->DelView(this);
		d->Parent->OnChildrenChanged(this, false);
		d->Parent = 0;
		d->ParentI = 0;
		Status = true;
		WndFlags &= ~GWF_FOCUS;

		if (_Capturing == this)
		{
			if (_View)
				ReleaseCapture();
			_Capturing = 0;
		}
		if (_View)
		{
			WndFlags &= ~GWF_QUIT_WND;
			BOOL Status = DestroyWindow(_View);
			LgiAssert(Status);
		}
	}
	return Status;
}

GRect &GView::GetClient(bool InClientSpace)
{
	static GRect Client;

	if (_View)
	{
		RECT rc;
		GetClientRect(_View, &rc);
		Client = rc;
	}
	else
	{
		Client.Set(0, 0, Pos.X()-1, Pos.Y()-1);

		if (dynamic_cast<GWindow*>(this) ||
			dynamic_cast<GDialog*>(this))
		{
			Client.x1 += GetSystemMetrics(SM_CXFRAME);
			Client.x2 -= GetSystemMetrics(SM_CXFRAME);
			
			Client.y1 += GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CYCAPTION);
			Client.y2 -= GetSystemMetrics(SM_CYFRAME);
		}
		else if (Sunken() || Raised())
		{
			Client.Size(_BorderSize, _BorderSize);
		}
	}

	if (InClientSpace)
		Client.Offset(-Client.x1, -Client.y1);

	return Client;
}

LgiCursor GView::GetCursor(int x, int y)
{
	return LCUR_Normal;
}

bool LgiToWindowsCursor(LgiCursor Cursor)
{
	char *Set = 0;
	switch (Cursor)
	{
		case LCUR_UpArrow:
			Set = IDC_UPARROW;
			break;
		case LCUR_Cross:
			Set = IDC_CROSS;
			break;
		case LCUR_Wait:
			Set = IDC_WAIT;
			break;
		case LCUR_Ibeam:
			Set = IDC_IBEAM;
			break;
		case LCUR_SizeVer:
			Set = IDC_SIZENS;
			break;
		case LCUR_SizeHor:
			Set = IDC_SIZEWE;
			break;
		case LCUR_SizeBDiag:
			Set = IDC_SIZENESW;
			break;
		case LCUR_SizeFDiag:
			Set = IDC_SIZENWSE;
			break;
		case LCUR_SizeAll:
			Set = IDC_SIZEALL;
			break;
		case LCUR_PointingHand:
		{
			GArray<int> Ver;
			int Os = LgiGetOs(&Ver);
			if
			(
				(
					Os == LGI_OS_WIN32
					||
					Os == LGI_OS_WIN64
				)
				&&
				Ver[0] >= 5)
			{
				#ifndef IDC_HAND
				#define IDC_HAND MAKEINTRESOURCE(32649)
				#endif
				Set = IDC_HAND;
			}
			// else not supported
			break;
		}
		case LCUR_Forbidden:
			Set = IDC_NO;
			break;

		// Not impl
		case LCUR_SplitV:
			break;
		case LCUR_SplitH:
			break;
		case LCUR_Blank:
			break;
	}

	SetCursor(LoadCursor(0, MAKEINTRESOURCE(Set?Set:IDC_ARROW)));

	return true;
}

void GView::PointToScreen(GdcPt2 &p)
{
	POINT pt = {p.x, p.y};
	GViewI *t = this;
	while (	t &&
			t->GetParent() &&
			!t->Handle())
	{
		pt.x += t->GetPos().x1;
		pt.y += t->GetPos().y1;
		t = t->GetParent();
	}
	ClientToScreen(t->Handle(), &pt);
	p.x = pt.x;
	p.y = pt.y;
}

void GView::PointToView(GdcPt2 &p)
{
	POINT pt = {p.x, p.y};
	GViewI *t = this;
	while (	t &&
			t->GetParent() &&
			!t->Handle())
	{
		pt.x -= t->GetPos().x1;
		pt.y -= t->GetPos().y1;
		t = t->GetParent();
	}
	ScreenToClient(t->Handle(), &pt);
	p.x = pt.x;
	p.y = pt.y;
}

bool GView::GetMouse(GMouse &m, bool ScreenCoords)
{
	// position
	POINT p;
	GetCursorPos(&p);
	if (!ScreenCoords)
	{
		ScreenToClient(_View, &p);
	}
	m.x = p.x;
	m.y = p.y;
	m.Target = this;

	// buttons
	m.Flags =	((GetAsyncKeyState(VK_LBUTTON)&0x8000) ? LGI_EF_LEFT : 0) |
				((GetAsyncKeyState(VK_MBUTTON)&0x8000) ? LGI_EF_MIDDLE : 0) |
				((GetAsyncKeyState(VK_RBUTTON)&0x8000) ? LGI_EF_RIGHT : 0) |
				((GetAsyncKeyState(VK_CONTROL)&0x8000) ? LGI_EF_CTRL : 0) |
				((GetAsyncKeyState(VK_MENU)&0x8000) ? LGI_EF_ALT : 0) |
				((GetAsyncKeyState(VK_SHIFT)&0x8000) ? LGI_EF_SHIFT : 0);

	if (m.Flags & (LGI_EF_LEFT | LGI_EF_MIDDLE | LGI_EF_RIGHT))
	{
		m.Flags |= LGI_EF_DOWN;
	}

	return true;
}

bool GView::SetPos(GRect &p, bool Repaint)
{
	bool Status = true;
	GRect OldPos = Pos;

	if (Pos != p)
	{
		Pos = p;
		if (_View)
		{
			HWND hOld = GetFocus();
			bool WasVis = IsWindowVisible(_View);

			In_SetWindowPos = true;
			Status = SetWindowPos(	_View,
									NULL,
									Pos.x1,
									Pos.y1,
									Pos.X(),
									Pos.Y(),
									// ((Repaint) ? 0 : SWP_NOREDRAW) |
									SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
			In_SetWindowPos = false;

			HWND hNew = GetFocus();
			if (hNew != hOld)
			{
				bool IsVis = IsWindowVisible(_View);

				LgiTrace("%s:%i - SetWindowPos changed the focus!!!!!! Old=%p New=%p Vis=%i->%i _View=%s %s->%s\\n",
					_FL, hOld, hNew, WasVis, IsVis, GetClass(), OldPos.GetStr(), Pos.GetStr());
				
				// Oh f#$% off windows.
				// SetFocus(hOld);
			}
		}
		else if (GetParent())
		{
			OnPosChange();
		}
		
		if (Repaint)
		{
			Invalidate();
		}
	}

	return Status;
}

bool GView::Invalidate(GRect *r, bool Repaint, bool Frame)
{
	if (_View)
	{
		bool Status = false;

		if (Frame)
		{
			RedrawWindow(	_View,
							NULL,
							NULL,
							RDW_FRAME |
							RDW_INVALIDATE |
							RDW_ALLCHILDREN |
							((Repaint) ? RDW_UPDATENOW : 0));
		}
		else
		{
			if (r)
			{
				Status = InvalidateRect(_View, &((RECT)*r), false);
			}
			else
			{
				RECT c = GetClient();
				Status = InvalidateRect(_View, &c, false);
			}
		}

		if (Repaint)
		{
			UpdateWindow(_View);
		}

		return Status;
	}
	else
	{
		GRect Up;
		GViewI *p = this;

		if (r)
		{
			Up = *r;
		}
		else
		{
			Up.Set(0, 0, Pos.X()-1, Pos.Y()-1);
		}

		if (dynamic_cast<GWindow*>(this))
			return true;

		while (p && !p->Handle())
		{
			GViewI *Par = p->GetParent();
			GView *VPar = Par?Par->GetGView():0;
			GRect w = p->GetPos();
			GRect c = p->GetClient(false);
			if (Frame && p == this)
				Up.Offset(w.x1, w.y1);
			else				
				Up.Offset(w.x1 + c.x1, w.y1 + c.y1);
			p = Par;
		}

		if (p && p->Handle())
		{
			return p->Invalidate(&Up, Repaint);
		}
	}

	return false;
}

void
CALLBACK
GView::TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, uint32 dwTime)
{
	GView *View = (GView*) idEvent;
	if (View)
	{
		View->OnPulse();
	}
}

void GView::SetPulse(int Length)
{
	if (_View)
	{
		if (Length > 0)
		{
            d->TimerId = SetTimer(_View, (UINT_PTR) this, Length, (TIMERPROC) TimerProc);
		}
		else
		{
			KillTimer(_View, d->TimerId);
			d->TimerId = 0;
		}
	}
}

static int ConsumeTabKey = 0;

bool SysOnKey(GView *w, GMessage *m)
{
	if (m->a == VK_TAB &&
		(m->m == WM_KEYDOWN ||
		m->m == WM_SYSKEYDOWN) )
	{
		if (!TestFlag(w->d->WndDlgCode, DLGC_WANTTAB) &&
			!TestFlag(w->d->WndDlgCode, DLGC_WANTALLKEYS))
		{
			// push the focus to the next control
			bool Shifted = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
			GViewI *Wnd = GetNextTabStop(w, Shifted);
			if (Wnd)
			{
				if (In_SetWindowPos)
				{
					assert(0);
					LgiTrace("%s:%i - SetFocus(%p)\\n", _FL, Wnd->Handle());
				}

				ConsumeTabKey = 2;
				::SetFocus(Wnd->Handle());
				return true;
			}
		}
	}

	return false;
}

#ifdef _MSC_VER
#include "vsstyle.h"

void GView::DrawThemeBorder(GSurface *pDC, GRect &r)
{
	if (!d->hTheme)
		d->hTheme = OpenThemeData(_View, VSCLASS_EDIT);

	if (d->hTheme)
	{
		RECT rc = r;
		
		int StateId;
		if (!Enabled())
			StateId = EPSN_DISABLED;
		else if (GetFocus() == _View)
			StateId = EPSN_FOCUSED;
		else
			StateId = EPSN_NORMAL;
		
		// LgiTrace("ThemeDraw %s: %i\n", GetClass(), StateId);
		
		RECT clip[4];
		clip[0] = GRect(r.x1, r.y1, r.x1 + 1, r.y2); // left
		clip[1] = GRect(r.x1 + 2, r.y1, r.x2 - 2, r.y1 + 1); // top
		clip[2] = GRect(r.x2 - 1, r.y1, r.x2, r.y2);  // right
		clip[3] = GRect(r.x1 + 2, r.y2 - 1, r.x2 - 2, r.y2); // bottom
		
		GColour cols[4] = 
		{
			GColour(255, 0, 0),
			GColour(0, 255, 0),
			GColour(0, 0, 255),
			GColour(255, 255, 0)
		};
		
		for (int i=0; i<CountOf(clip); i++)
		{
			#if 0
			GRect tmp = clip[i];
			pDC->Colour(cols[i]);
			pDC->Rectangle(&tmp);
			#else
			DrawThemeBackground(d->hTheme,
								pDC->Handle(),
								EP_EDITBORDER_NOSCROLL,
								StateId,
								&rc,
								&clip[i]);
			#endif
		}
		
		pDC->Colour(LC_MED, 24);
		pDC->Set(r.x1, r.y1);
		pDC->Set(r.x2, r.y1);
		pDC->Set(r.x1, r.y2);
		pDC->Set(r.x2, r.y2);
		
		r.Size(2, 2);
	}
	else
	{
		LgiWideBorder(pDC, r, Sunken() ? DefaultSunkenEdge : DefaultRaisedEdge);
		d->IsThemed = false;
	}
}
#else
void GView::DrawThemeBorder(GSurface *pDC, GRect &r)
{
	LgiWideBorder(pDC, r, DefaultSunkenEdge);
}
#endif

bool IsKeyChar(GKey &k, int vk)
{
	if (k.Ctrl() || k.Alt() || k.System())
		return false;

	switch (vk)
	{
		case VK_BACK:
		case VK_TAB:
		case VK_RETURN:
		case VK_SPACE:

		case 0xba: // ;
		case 0xbb: // =
		case 0xbc: // ,
		case 0xbd: // -
		case 0xbe: // .
		case 0xbf: // /

		case 0xc0: // `

		case 0xdb: // [
		case 0xdc: // |
		case 0xdd: // ]
		case 0xde: // '
			return true;
	}

	if (vk >= VK_NUMPAD0 && vk <= VK_DIVIDE)
		return true;

	if (vk >= '0' && vk <= '9')
		return true;

	if (vk >= 'A' && vk <= 'Z')
		return true;

	return false;
}

#define KEY_FLAGS		(~(MK_LBUTTON | MK_MBUTTON | MK_RBUTTON))

GMessage::Result GView::OnEvent(GMessage *Msg)
{
	int Status = 0;

	if (MsgCode(Msg) == MouseRollMsg)
	{
		HWND hFocus = GetFocus();
		if (_View)
		{
			int Flags = ((GetKeyState(VK_SHIFT)&0xF000) ? VK_SHIFT : 0) | 
						((GetKeyState(VK_CONTROL)&0xF000) ? VK_CONTROL : 0);
			
			PostMessage(hFocus, WM_MOUSEWHEEL, MAKELONG(Flags, (short)Msg->a), Msg->b);
		}
		return 0;
	}

	if (_View)
	{
		switch (Msg->m)
		{
			case WM_CTLCOLOREDIT:
			case WM_CTLCOLORSTATIC:
			{
				HDC hdc = (HDC)MsgA(Msg);
				HWND hwnd = (HWND)MsgB(Msg);

				GViewI *v = FindControl(hwnd);
				if (v)
				{
					if (v->GetCss())
					{
						GCss::ColorDef f, b;
						
						f = v->GetCss()->Color();
						b = v->GetCss()->BackgroundColor();
						
						if (f.Type == GCss::ColorRgb)
						{
							COLORREF c = RGB(R32(f.Rgb32), G32(f.Rgb32), B32(f.Rgb32));
							SetTextColor(hdc, c);
							// SetDCBrushColor(hdc, c);
						}
							
						if (b.Type == GCss::ColorRgb)
						{
							COLORREF c = RGB(R32(b.Rgb32), G32(b.Rgb32), B32(b.Rgb32));
							SetBkColor(hdc, c);
						}

						#if !defined(DC_BRUSH)
						#define DC_BRUSH            18
						#endif
						return (LRESULT) GetStockObject(DC_BRUSH);
					}
				}

				goto ReturnDefaultProc;
				return 0;
			}
			case 5700:
			{
				// I forget what this is for...
				break;
			}
			case WM_ERASEBKGND:
			{
				return 1;
			}
			case WM_GETFONT:
			{
				GFont *f = GetFont();
				return (GMessage::Result) (f ? f->Handle() : SysFont->Handle());
				break;
			}
			case WM_MENUCHAR:
			case WM_MEASUREITEM:
			case WM_DRAWITEM:
			{
				return GMenu::_OnEvent(Msg);
				break;
			}
			case WM_ENABLE:
			{
				Invalidate(&Pos);
				break;
			}
			case WM_HSCROLL:
			case WM_VSCROLL:
			{
				GViewI *Wnd = FindControl((HWND) Msg->b);
				if (Wnd)
				{
					Wnd->OnEvent(Msg);
				}
				break;
			}
			case WM_GETDLGCODE:
			{
				// we handle all tab control stuff
				return DLGC_WANTALLKEYS; // d->WndDlgCode | DLGC_WANTTAB;
			}
			case WM_MOUSEWHEEL:
			{
				short fwKeys = LOWORD(Msg->a);			// key flags
				short zDelta = (short) HIWORD(Msg->a);	// wheel rotation
				short xPos = (short) LOWORD(Msg->b);	// horizontal position of pointer
				short yPos = (short) HIWORD(Msg->b);	// vertical position of pointer
				int nScrollLines = - _lgi_mouse_wheel_lines();
				double Lines = ((double)zDelta * (double)nScrollLines) / WHEEL_DELTA;
				
				// Try giving the event to the current window...
				if (!OnMouseWheel(Lines))
				{
					// Find the window under the cursor... and try giving it the mouse wheel event
					POINT Point = {xPos, yPos};
					HWND hUnder = ::WindowFromPoint(Point);
					if (hUnder)
					{
						GViewI *UnderWnd = GWindowFromHandle(hUnder);
						if (UnderWnd)
						{
							UnderWnd->OnMouseWheel(Lines);
						}
					}
				}
				return 0;
			}
			case M_CHANGE:
			{
				GWindow *w = GetWindow();
				GViewI *Ctrl = w ? w->FindControl(Msg->a) : 0;
				if (Ctrl)
				{
					return OnNotify(Ctrl, Msg->b);
				}
				else
				{
					LgiTrace("Ctrl %i not found.\n", Msg->a);
				}
				break;
			}
			case M_COMMAND:
			{
				GViewI *Ci = FindControl((HWND) Msg->b);
				GView *Ctrl = Ci ? Ci->GetGView() : 0;
				if (Ctrl)
				{
					short Code = HIWORD(Msg->a);
					switch (Code)
					{
						/*
						case BN_CLICKED: // BUTTON
						{
							OnNotify(Ctrl, 0);
							break;
						}
						*/
						case EN_CHANGE: // EDIT
						{
							Ctrl->SysOnNotify(Code);
							break;
						}
						case CBN_CLOSEUP:
						{
							PostMessage(_View, WM_COMMAND, MAKELONG(Ctrl->GetId(), CBN_EDITCHANGE), Msg->b);
							break;
						}
						case CBN_EDITCHANGE: // COMBO
						{
							Ctrl->SysOnNotify(Code);
							OnNotify(Ctrl, 0);
							break;
						}
					}
				}
				break;
			}
			case WM_NCDESTROY:
			{
                #if _MSC_VER >= 1400
                SetWindowLongPtr(_View, GWLP_USERDATA, 0);
                #else
				SetWindowLong(_View, GWL_USERDATA, 0);
				#endif
				_View = 0;
				if (WndFlags & GWF_QUIT_WND)
				{
					delete this;
				}
				break;
			}
	 		case WM_CLOSE:
			{
				if (OnRequestClose(false))
				{
					Quit();
				}
				break;
			}
			case WM_DESTROY:
			{
				OnDestroy();
				break;
			}
			case WM_CREATE:
			{
				SetId(d->CtrlId);

				GWindow *w = GetWindow();
				if (w && w->GetFocus() == this)
				{
					HWND hCur = GetFocus();
					if (hCur != _View)
					{
						if (In_SetWindowPos)
						{
							assert(0);
							LgiTrace("%s:%i - SetFocus(%p) (%s)\\n", __FILE__, __LINE__, Handle(), GetClass());
						}

						SetFocus(_View);
					}
				}

				OnCreate();
				break;
			}
			case WM_SETFOCUS:
			{
				GWindow *w = GetWindow();
				if (w)
				{
					w->SetFocus(this, GWindow::GainFocus);
				}
				else
				{
					// This can happen in popup sub-trees of views. Where the focus
					// is tracked separately from the main GWindow.
					OnFocus(true);
					Invalidate((GRect*)NULL, false, true);
				}
				break;
			}
			case WM_KILLFOCUS:
			{
				GWindow *w = GetWindow();
				if (w)
				{
					w->SetFocus(this, GWindow::LoseFocus);
				}
				else
				{
					// This can happen when the GWindow is being destroyed
					Invalidate((GRect*)NULL, false, true);
					OnFocus(false);
				}
				break;
			}
			case WM_WINDOWPOSCHANGED:
			{
				if (!IsIconic(_View))
				{
					WINDOWPOS *Info = (LPWINDOWPOS) Msg->b;
					if (Info)
					{
						if (Info->x == -32000 &&
							Info->y == -32000)
						{
							#if 0
							LgiTrace("WM_WINDOWPOSCHANGED %i,%i,%i,%i (icon=%i)\\n",
								Info->x,
								Info->y,
								Info->cx,
								Info->cy,
								IsIconic(Handle()));
							#endif
						}
						else
						{
							GRect r;
							r.ZOff(Info->cx-1, Info->cy-1);
							r.Offset(Info->x, Info->y);
							if (r.Valid() && r != Pos)
							{
								Pos = r;
							}
						}
					}

					OnPosChange();
				}

				if (!(WndFlags & GWF_DIALOG))
				{
					goto ReturnDefaultProc;
				}
				break;
			}
			case WM_CAPTURECHANGED:
			{
				_Capturing = 0;
				break;
			}
			case M_MOUSEENTER:
			{
				GMouse Ms;
				Ms.Target = this;
				Ms.x = (short) (Msg->b&0xFFFF);
				Ms.y = (short) (Msg->b>>16);
				Ms.Flags = 0;

				GViewI *MouseOver = WindowFromPoint(Ms.x, Ms.y);
				if (MouseOver &&
					_Over != MouseOver &&
					!(MouseOver == this || MouseOver->Handle() == 0))
				{
					if (_Capturing)
					{
						if (MouseOver == _Capturing)
						{
							Ms = lgi_adjust_click(Ms, _Capturing);
							_Capturing->OnMouseEnter(Ms);
						}
					}
					else
					{
						if (_Over)
						{
							GMouse m = lgi_adjust_click(Ms, _Over);
							_Over->OnMouseExit(m);

							#if DEBUG_OVER
							LgiTrace("LoseOver=%p '%-20s'\\n", _Over, _Over->Name());
							#endif
						}

						_Over = MouseOver;

						if (_Over)
						{
							#if DEBUG_OVER
							LgiTrace("GetOver=%p '%-20s'\\n", _Over, _Over->Name());
							#endif

							GMouse m = lgi_adjust_click(Ms, _Over);
							_Over->OnMouseEnter(m);
						}
					}
				}
				break;
			}
			case M_MOUSEEXIT:
			{
				if (_Over)
				{
					GMouse Ms;
					Ms.Target = this;
					Ms.x = (short) (Msg->b&0xFFFF);
					Ms.y = (short) (Msg->b>>16);
					Ms.Flags = 0;

					bool Mine = false;
					if (_Over->Handle())
					{
						Mine = _Over == this;
					}
					else
					{
						for (GViewI *o = _Capturing ? _Capturing : _Over; o; o = o->GetParent())
						{
							if (o == this)
							{
								Mine = true;
								break;
							}
						}
					}

					if (Mine)
					{
						if (_Capturing)
						{
							GMouse m = lgi_adjust_click(Ms, _Capturing);
							_Capturing->OnMouseExit(m);
						}
						else
						{
							#if DEBUG_OVER
							LgiTrace("LoseOver=%p '%-20s'\\n", _Over, _Over->Name());
							#endif

							_Over->OnMouseExit(Ms);
							_Over = 0;
						}
					}
				}
				break;
			}
			case WM_MOUSEMOVE:
			{
				GMouse Ms;
				Ms.Target = this;
				Ms.x = (short) (Msg->b&0xFFFF);
				Ms.y = (short) (Msg->b>>16);
				Ms.Flags = _lgi_get_key_flags();
				Ms.IsMove(true);
				if (TestFlag(Msg->a, MK_LBUTTON)) SetFlag(Ms.Flags, LGI_EF_LEFT);
				if (TestFlag(Msg->a, MK_RBUTTON)) SetFlag(Ms.Flags, LGI_EF_RIGHT);
				if (TestFlag(Msg->a, MK_MBUTTON)) SetFlag(Ms.Flags, LGI_EF_MIDDLE);
				
				SetKeyFlag(Ms.Flags, VK_MENU, MK_ALT);
				Ms.Down((Msg->a & (MK_LBUTTON|MK_MBUTTON|MK_RBUTTON)) != 0);

				int CurX = Ms.x, CurY = Ms.y;
				LgiCursor Cursor = LCUR_Normal;
				for (GViewI *c = this; Cursor == LCUR_Normal && c->GetParent(); c = c->GetParent())
				{
					GRect CPos = c->GetPos();
					Cursor = c->GetCursor(CurX, CurY);
					CurX += CPos.x1;
					CurY += CPos.y1;
				}
				LgiToWindowsCursor(Cursor);

				GViewI *MouseOver = WindowFromPoint(Ms.x, Ms.y);
				if (_Over != MouseOver)
				{
					if (_Over)
					{
						#if DEBUG_OVER
						LgiTrace("LoseOver=%p '%-20s'\\n", _Over, _Over->Name());
						#endif

						GMouse m = lgi_adjust_click(Ms, _Over);
						_Over->OnMouseExit(m);
					}

					_Over = MouseOver;

					if (_Over)
					{
						GMouse m = lgi_adjust_click(Ms, _Over);
						_Over->OnMouseEnter(m);

						#if DEBUG_OVER
						LgiTrace("GetOver=%p '%-20s'\\n", _Over, _Over->Name());
						#endif
					}
				}

				#if 0
				LgiTrace("WM_MOUSEMOVE %i,%i target=%p/%s, over=%p/%s, cap=%p/%s\n",
					Ms.x, Ms.y,
					Ms.Target, Ms.Target?Ms.Target->GetClass():0,
					_Over, _Over?_Over->GetClass():0,
					_Capturing, _Capturing?_Capturing->GetClass():0);
				#endif

				if (_Capturing)
					Ms = lgi_adjust_click(Ms, _Capturing, true);
				else if (_Over)
					Ms = lgi_adjust_click(Ms, _Over);
				else
					return 0;

				GWindow *Wnd = GetWindow();
				if (!Wnd || Wnd->HandleViewMouse(dynamic_cast<GView*>(Ms.Target), Ms))
				{
					Ms.Target->OnMouseMove(Ms);
				}
				break;
			}
			case WM_NCHITTEST:
			{
				POINT Pt = { LOWORD(Msg->b), HIWORD(Msg->b) };
				ScreenToClient(_View, &Pt);
				int Hit = OnHitTest(Pt.x, Pt.y);
				if (Hit >= 0)
				{
					return Hit;
				}
				if (!(WndFlags & GWF_DIALOG))
				{
					goto ReturnDefaultProc;
				}
				break;
			}
			case WM_LBUTTONDBLCLK:
			case WM_LBUTTONDOWN:
			case WM_LBUTTONUP:
			{
				GMouse Ms;
				Ms.x = (short) (Msg->b&0xFFFF);
				Ms.y = (short) (Msg->b>>16);
				Ms.Flags = _lgi_get_key_flags() | LGI_EF_LEFT;
				Ms.Down(Msg->m != WM_LBUTTONUP);
				Ms.Double(Msg->m == WM_LBUTTONDBLCLK);

				if (_Capturing)
					Ms = lgi_adjust_click(Ms, _Capturing, true);
				else if (_Over)
					Ms = lgi_adjust_click(Ms, _Over);
				else
					Ms.Target = this;

				GWindow *Wnd = GetWindow();
				if (!Wnd || Wnd->HandleViewMouse(dynamic_cast<GView*>(Ms.Target), Ms))
					Ms.Target->OnMouseClick(Ms);
				break;
			}
			case WM_RBUTTONDBLCLK:
			case WM_RBUTTONDOWN:
			case WM_RBUTTONUP:
			{
				GMouse Ms;
				Ms.x = (short) (Msg->b&0xFFFF);
				Ms.y = (short) (Msg->b>>16);
				Ms.Flags = _lgi_get_key_flags() | LGI_EF_RIGHT;
				Ms.Down(Msg->m != WM_RBUTTONUP);
				Ms.Double(Msg->m == WM_RBUTTONDBLCLK);

				if (_Capturing)
					Ms = lgi_adjust_click(Ms, _Capturing, true);
				else if (_Over)
					Ms = lgi_adjust_click(Ms, _Over);
				else
					Ms.Target = this;

				GWindow *Wnd = GetWindow();
				if (!Wnd || Wnd->HandleViewMouse(dynamic_cast<GView*>(Ms.Target), Ms))
					Ms.Target->OnMouseClick(Ms);
				break;
			}
			case WM_MBUTTONDBLCLK:
			case WM_MBUTTONDOWN:
			case WM_MBUTTONUP:
			{
				GMouse Ms;
				Ms.x = (short) (Msg->b&0xFFFF);
				Ms.y = (short) (Msg->b>>16);
				Ms.Flags = _lgi_get_key_flags() | LGI_EF_MIDDLE;
 				Ms.Down(Msg->m != WM_MBUTTONUP);
				Ms.Double(Msg->m == WM_MBUTTONDBLCLK);

				if (_Capturing)
					Ms = lgi_adjust_click(Ms, _Capturing, true);
				else if (_Over)
					Ms = lgi_adjust_click(Ms, _Over);
				else
					Ms.Target = this;

				GWindow *Wnd = GetWindow();
				if (!Wnd || Wnd->HandleViewMouse(dynamic_cast<GView*>(Ms.Target), Ms))
					Ms.Target->OnMouseClick(Ms);
				break;
			}
			case WM_SYSKEYUP:
			case WM_SYSKEYDOWN:
			case WM_KEYDOWN:
			case WM_KEYUP:
			{
				static char AltCode[32];
				bool IsDialog = TestFlag(WndFlags, GWF_DIALOG);
				bool IsDown = Msg->m == WM_KEYDOWN || Msg->m == WM_SYSKEYDOWN;
				int KeyFlags = _lgi_get_key_flags();
				HWND hwnd = _View;

				if (SysOnKey(this, Msg))
				{
					LgiTrace("SysOnKey true, Msg=0x%x %x,%x\n", Msg->m, Msg->a, Msg->b);
					return 0;
				}
				else
				{
					// Key
					GKey Key(Msg->a, Msg->b);

					Key.Flags = KeyFlags;
					Key.Data = Msg->b;
					Key.Down(IsDown);
					Key.IsChar = false;

					if (Key.Ctrl())
					{
						Key.c16 = Msg->a;
					}

					if (Key.c16 == VK_TAB && ConsumeTabKey)
					{
						ConsumeTabKey--;
					}
					else
					{
						GWindow *Wnd = GetWindow();
						if (Wnd)
						{
							if (Key.Alt() ||
								Key.Ctrl() ||
								(Key.c16 < 'A' || Key.c16 > 'Z'))
							{
								Wnd->HandleViewKey(this, Key);
							}
						}
						else
						{
							OnKey(Key);
						}
					}

					if (Msg->m == WM_SYSKEYUP || Msg->m == WM_SYSKEYDOWN)
					{
						if (Key.vkey >= VK_F1 &&
							Key.vkey <= VK_F12 &&
							Key.Alt() == false)
						{
							// So in LgiIde if you press F10 (debug next) you get a hang
							// sometimes in DefWindowProc. Until I figure out what's going
							// on this code exits before calling DefWindowProc without
							// breaking other WM_SYSKEY* functionality (esp Alt+F4).
							return 0;
						}
					}
				}

				if (!IsDialog)
				{
					// required for Alt-Key function (eg Alt-F4 closes window)
					goto ReturnDefaultProc;
				}
				break;
			}
			#if OLD_WM_CHAR_MODE
			case WM_CHAR:
			{
				GKey Key(Msg->a, Msg->b);
				Key.Flags = _lgi_get_key_flags();
				Key.Data = Msg->b;
				Key.Down(true);
				Key.IsChar = true;

				bool Shift = Key.Shift();
				bool Caps = TestFlag(Key.Flags, LGI_EF_CAPS_LOCK);
				if (!(Shift ^ Caps))
				{
					Key.c16 = ToLower(Key.c16);
				}
				else
				{
					Key.c16 = ToUpper(Key.c16);
				}

				if (Key.c16 == VK_TAB && ConsumeTabKey)
				{
					ConsumeTabKey--;
				}
				else
				{
					GWindow *Wnd = GetWindow();
					if (Wnd)
					{
						Wnd->HandleViewKey(this, Key);
					}
					else
					{
						OnKey(Key);
					}
				}
				break;
			}
			#endif
			case M_SET_WND_STYLE:
			{
				SetWindowLong(Handle(), GWL_STYLE, Msg->b);
				SetWindowPos(	Handle(),
								0, 0, 0, 0, 0,
								SWP_NOMOVE | SWP_NOZORDER | SWP_NOSIZE | SWP_FRAMECHANGED);
				break;
			}
			case WM_PAINT:
			{
				_Paint();
				break;
			}
			case WM_NCPAINT:
			{
				if (GetWindow() != this &&
					!TestFlag(WndFlags, GWF_SYS_BORDER))
				{
					HDC hDC = GetWindowDC(_View);
					GScreenDC Dc(hDC, _View, true);
					GRect p(0, 0, Dc.X()-1, Dc.Y()-1);
					OnNcPaint(&Dc, p);
				}

				goto ReturnDefaultProc;
				break;
			}
			case WM_NCCALCSIZE:
			{
				GMessage::Param Status = 0;
				int Edge = (Sunken() || Raised()) ? _BorderSize : 0;
				RECT *rc = NULL;
				if (Msg->a)
				{
					NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS*) Msg->b;
					rc = p->rgrc;
				}
				else
				{
					rc = (RECT*)Msg->b;
				}
				
				if (!(WndFlags & GWF_DIALOG))
				{
					Status = DefWindowProcW(_View, Msg->m, Msg->a, Msg->b);
				}

				if (Edge && rc && !TestFlag(WndFlags, GWF_SYS_BORDER))
				{
					rc->left += Edge;
					rc->top += Edge;
					rc->right -= Edge;
					rc->bottom -= Edge;
					return 0;
				}
				
				return Status;
			}
			default:
			{
				if (!(WndFlags & GWF_DIALOG))
					goto ReturnDefaultProc;
				break;
			}
		}
	}

	return 0;

ReturnDefaultProc:
	#ifdef _DEBUG
	uint64 start = LgiCurrentTime();
	#endif
	LRESULT r = DefWindowProcW(_View, Msg->m, Msg->a, Msg->b);
	#ifdef _DEBUG
	uint64 now = LgiCurrentTime();
	if (now - start > 1000)
	{
		LgiTrace("DefWindowProc(0x%.4x, %i, %i) took %ims\n",
			Msg->m, Msg->a, Msg->b, (int)(now - start));
	}
	#endif
	return r;
}

GViewI *GView::FindControl(OsView hCtrl)
{
	if (_View == hCtrl)
	{
		return this;
	}

	;
	for (List<GViewI>::I i = Children.Start(); i.In(); i++)
	{
		GViewI *Ctrl = (*i)->FindControl(hCtrl);
		if (Ctrl)
		{
			return Ctrl;
		}
	}
	return 0;
}
