/// \file
/// \author Matthew Allen <fret@memecode.com>
#ifdef LINUX
#include <unistd.h>
#endif

#include "Lgi.h"
#include "GViewPriv.h"
#include "GDragAndDrop.h"
#include "GTableLayout.h"
#include "GButton.h"
#include "GCss.h"

#define DEBUG_CAPTURE	0

#if WINNATIVE
#define GViewFlags d->WndStyle
#else
#define GViewFlags WndFlags
#endif

//////////////////////////////////////////////////////////////////////////////////////
// Helper
GdcPt2 lgi_view_offset(GViewI *v, bool Debug = false)
{
	GdcPt2 Offset;
	
	for (GViewI *p = v; p; p = p->GetParent())
	{
		if (dynamic_cast<GWindow*>(p))
			break;
		
		GRect pos = p->GetPos();
		GRect cli = p->GetClient(false);

		if (Debug)
		{
			const char *cls = p->GetClass();
			LgiTrace("	Off[%s] += %i,%i = %i,%i (%s)\n",
					cls,
					pos.x1, pos.y1,
					Offset.x + pos.x1, Offset.y + pos.y1,
					cli.GetStr());
		}
		
		Offset.x += pos.x1 + cli.x1;
		Offset.y += pos.y1 + cli.y1;
	}
	
	return Offset;
}

GMouse &lgi_adjust_click(GMouse &Info, GViewI *Wnd, bool Capturing, bool Debug)
{
	static GMouse Temp;
	 
	Temp = Info;
	if (Wnd)
	{
		if (Debug
			#if 0
			|| Capturing
			#endif
			)
			LgiTrace("AdjustClick Target=%s -> Wnd=%s, Info=%i,%i\n",
				Info.Target?Info.Target->GetClass():"",
				Wnd?Wnd->GetClass():"",
				Info.x, Info.y);

		if (Temp.Target != Wnd)
		{
			if (Temp.Target)
			{
				GWindow *TargetWnd = Temp.Target->GetWindow();
				GWindow *WndWnd = Wnd->GetWindow();
				if (TargetWnd == WndWnd)
				{
					GdcPt2 TargetOff = lgi_view_offset(Temp.Target, Debug);
					if (Debug)
						LgiTrace("	WndOffset:\n");
					GdcPt2 WndOffset = lgi_view_offset(Wnd, Debug);

					Temp.x += TargetOff.x - WndOffset.x;
					Temp.y += TargetOff.y - WndOffset.y;

					#if 0
					GRect c = Wnd->GetClient(false);
					Temp.x -= c.x1;
					Temp.y -= c.y1;
					if (Debug)
						LgiTrace("	CliOff -= %i,%i\n", c.x1, c.y1);
					#endif

					Temp.Target = Wnd;
				}
			}
			else
			{
				GdcPt2 Offset;
				Temp.Target = Wnd;
				if (Wnd->WindowVirtualOffset(&Offset))
				{
					GRect c = Wnd->GetClient(false);
					Temp.x -= Offset.x + c.x1;
					Temp.y -= Offset.y + c.y1;
				}
			}
		}
	}
	
	LgiAssert(Temp.Target != NULL);
	
	return Temp;
}

//////////////////////////////////////////////////////////////////////////////////////
// Iterator
GViewIter::GViewIter(GView *view) : i(view->Children.Start())
{
	v = view;
}

GViewI *GViewIter::First()
{
	i = v->Children.Start();
	return *i;
}

GViewI *GViewIter::Last()
{
	i = v->Children.End();
	return *i;
}

GViewI *GViewIter::Next()
{
	i++;
	return *i;
}

GViewI *GViewIter::Prev()
{
	i--;
	return *i;
}

int GViewIter::Length()
{
	return v->Children.Length();
}

int GViewIter::IndexOf(GViewI *view)
{
	i = v->Children.Start();
	return i.IndexOf(view);
}

GViewI *GViewIter::operator [](int Idx)
{
	return v->Children[Idx];
}

//////////////////////////////////////////////////////////////////////////////////////
// GView class methods
GViewI *GView::_Capturing = 0;
GViewI *GView::_Over = 0;

GView::GView(OsView view)
{
	#ifdef _DEBUG
    _Debug = false;
	#endif

	d = new GViewPrivate;
	_View = view;
	_Window = 0;
	_Lock = 0;
	_InLock = 0;
	_BorderSize = 0;
	_IsToolBar = false;
	Script = 0;
	Pos.ZOff(-1, -1);
	WndFlags = GWF_VISIBLE;
}

GView::~GView()
{
	_Delete();
	DeleteObj(d);
}

#ifdef _DEBUG
void GView::Debug()
{
    _Debug = true;
}
#endif

GViewIterator *GView::IterateViews()
{
	return new GViewIter(this);
}

bool GView::AddView(GViewI *v, int Where)
{
	LgiAssert(!Children.HasItem(v));
	bool Add = Children.Insert(v, Where);
	if (Add)
	{
		GView *gv = v->GetGView();
		if (gv && gv->_Window != _Window)
		{
			LgiAssert(!_InLock);
			gv->_Window = _Window;
		}
		v->SetParent(this);
	}
	return Add;
}

bool GView::DelView(GViewI *v)
{
	return Children.Delete(v);
}

bool GView::HasView(GViewI *v)
{
	return Children.HasItem(v);
}

OsWindow GView::WindowHandle()
{
	GWindow *w = GetWindow();
	return (w) ? w->WindowHandle() : 0;
}

GWindow *GView::GetWindow()
{
	if (!_Window)
	{
		// Walk up parent list and find someone who has a window
		for (GView *w = d->GetParent(); w; w = w->d ? w->d->GetParent() : NULL)
		{
			if (w->_Window)
			{
				LgiAssert(!_InLock);
				_Window = w->_Window;
				break;
			}
		}
	}
		
	return dynamic_cast<GWindow*>(_Window);
}

bool GView::Lock(const char *file, int line, int TimeOut)
{
	if (!_Window)
		GetWindow();

	_InLock++;
	// LgiTrace("%s::%p Lock._InLock=%i %s:%i\n", GetClass(), this, _InLock, file, line);
	if (_Window && _Window->_Lock)
	{
		if (TimeOut < 0)
		{
			return _Window->_Lock->Lock(file, line);
		}
		else
		{
			return _Window->_Lock->LockWithTimeout(TimeOut, file, line);
		}
	}

	return true;
}

void GView::Unlock()
{
	if (_Window &&
		_Window->_Lock)
	{
		_Window->_Lock->Unlock();
	}
	_InLock--;
	// LgiTrace("%s::%p Unlock._InLock=%i\n", GetClass(), this, _InLock);
}

void GView::OnMouseClick(GMouse &m)
{
	if (Script && Script->OnScriptEvent(this))
	{
		Script->OnMouseClick(m);
	}
}

void GView::OnMouseEnter(GMouse &m)
{
	if (Script && Script->OnScriptEvent(this))
	{
		Script->OnMouseEnter(m);
	}
}

void GView::OnMouseExit(GMouse &m)
{
	if (Script && Script->OnScriptEvent(this))
	{
		Script->OnMouseExit(m);
	}
}

void GView::OnMouseMove(GMouse &m)
{
	if (Script && Script->OnScriptEvent(this))
	{
		Script->OnMouseMove(m);
	}
}

bool GView::OnMouseWheel(double Lines)
{
	if (Script && Script->OnScriptEvent(this))
		return Script->OnMouseWheel(Lines);
	
	return false;
}

bool GView::OnKey(GKey &k)
{
	if (Script && Script->OnScriptEvent(this))
	{
		Script->OnKey(k);
	}

	return false;
}

void GView::OnAttach()
{
	List<GViewI>::I it = Children.Start();
	for (GViewI *v = *it; v; v = *++it)
	{
		if (!v->GetParent())
			v->SetParent(this);
	}
}

void GView::OnCreate()
{
	if (Script && Script->OnScriptEvent(this))
	{
		Script->OnCreate();
	}
}

void GView::OnDestroy()
{
	if (Script && Script->OnScriptEvent(this))
	{
		Script->OnDestroy();
	}
}

void GView::OnFocus(bool f)
{
	if (Script && Script->OnScriptEvent(this))
	{
		Script->OnFocus(f);
	}
}

void GView::OnPulse()
{
	if (Script && Script->OnScriptEvent(this))
	{
		Script->OnPulse();
	}
}

void GView::OnPosChange()
{
	if (Script && Script->OnScriptEvent(this))
	{
		Script->OnPosChange();
	}
}

bool GView::OnRequestClose(bool OsShuttingDown)
{
	if (Script && Script->OnScriptEvent(this))
	{
		Script->OnRequestClose(OsShuttingDown);
	}

	return true;
}

int GView::OnHitTest(int x, int y)
{
	if (Script && Script->OnScriptEvent(this))
	{
		Script->OnHitTest(x, y);
	}

	return -1;
}

void GView::OnChildrenChanged(GViewI *Wnd, bool Attaching)
{
	if (Script && Script->OnScriptEvent(this))
	{
		Script->OnChildrenChanged(Wnd, Attaching);
	}
}

void GView::OnPaint(GSurface *pDC)
{
	if (Script && Script->OnScriptEvent(this))
	{
		Script->OnPaint(pDC);
	}
}

int GView::OnNotify(GViewI *Ctrl, int Flags)
{
	if (Script && Script->OnScriptEvent(this))
	{
		Script->OnNotify(Ctrl, Flags);
	}
	else if (Ctrl && d && d->Parent)
	{
		// default behaviour is just to pass the 
		// notification up to the parent
		return d->Parent->OnNotify(Ctrl, Flags);
	}

	return 0;
}

int GView::OnCommand(int Cmd, int Event, OsView Wnd)
{
	if (Script && Script->OnScriptEvent(this))
	{
		Script->OnCommand(Cmd, Event, Wnd);
	}

	return 0;
}

void GView::OnNcPaint(GSurface *pDC, GRect &r)
{
	int Border = Sunken() || Raised() ? _BorderSize : 0;
	if (
		#if 0 // WINNATIVE
		!_View &&
		#endif	
		Border == 2)
	{
		LgiEdge e;
		if (Sunken())
			e = Focus() ? EdgeWin7FocusSunken : DefaultSunkenEdge;
		else
			e = DefaultRaisedEdge;

		#if WINNATIVE
		if (d->IsThemed)
			DrawThemeBorder(pDC, r);
		else
		#endif
			LgiWideBorder(pDC, r, e);
	}
	else if (Border == 1)
	{
		LgiThinBorder(pDC, r, Sunken() ? DefaultSunkenEdge : DefaultRaisedEdge);
	}
}

void GView::_Paint(GSurface *pDC, int Ox, int Oy)
{
	#if defined __GTK_H__
	d->InPaint = true;
	#endif

	// Create temp DC if needed...
	GAutoPtr<GSurface> Local;
	if (!pDC)
	{
		Local.Reset(new GScreenDC(this));
		pDC = Local;
	}
	if (!pDC)
	{
		printf("%s:%i - No context to draw in.\n", _FL);
		return;
	}

	#if 0
	// This is useful for coverage checking
	pDC->Colour(Rgb24(255, 0, 255), 24);
	pDC->Rectangle();
	#endif

	bool HasClient = false;
	GRect r(0, 0, Pos.X()-1, Pos.Y()-1);

	#if WINNATIVE
	if (!_View)
	#endif
	{
		// Non-Client drawing
		GRect Client = r;
		OnNcPaint(pDC, Client);
		HasClient = GetParent() && (Client != r);
		if (HasClient)
		{
			Client.Offset(Ox, Oy);
			pDC->SetClient(&Client);
		}
	}

	r.Offset(Ox, Oy);

	// Paint this view's contents
	OnPaint(pDC);

	#if PAINT_VIRTUAL_CHILDREN
	// Paint any virtual children
	List<GViewI>::I it = Children.Start(); // just in case the child access the child list
	while (it.Each())
	{
		GViewI *i = *it;
		GView *w = i->GetGView();
		if (w &&
			!w->Handle() &&
			w->Visible())
		{
			GRect p = w->GetPos();
			#ifdef __GTK_H__
			p.Offset(_BorderSize, _BorderSize);
			#endif
			p.Offset(Ox, Oy);
			pDC->SetClient(&p);
			w->_Paint(pDC, p.x1, p.y1);
			pDC->SetClient(0);
		}
	}
	#endif

	if (HasClient)
		pDC->SetClient(0);

	#if defined __GTK_H__
	d->InPaint = false;
	#endif
}

GViewI *GView::GetParent()
{
	return d->Parent;
}

void GView::SetParent(GViewI *p)
{
	d->Parent = p ? p->GetGView() : NULL;
	d->ParentI = p;
}

void GView::SendNotify(int Data)
{
	GViewI *n = d->Notify ? d->Notify : d->Parent;
	if (n)
	{
		if (!_View || InThread())
		{
			n->OnNotify(this, Data);
		}
		else
		{
			// We are not in the same thread as the target window. So we post a message
			// across to the view.
			GViewI *Ptr = this;
			if (GetId() <= 0)
			{
				// We are going to generate a control Id to help the receiver of the
				// M_CHANGE message find out view later on. The reason for doing this
				// instead of sending a pointer to the object, is that the object 
				// _could_ be deleted between the message being sent and being received.
				// Which would result in an invalid memory access on that object.
				GViewI *p = GetWindow();
				if (!p)
				{
					// No window? Find the top most parent we can...
					p = this;
					while (p->GetParent())
						p = p->GetParent();
				}
				if (p)
				{
					// Give the control a valid ID
					for (int i=10; i<1000; i++)
					{
						if (!p->FindControl(i))
						{
							printf("Giving the ctrl '%s' the id '%i' for SendNotify\n",
								GetClass(),
								i);
							SetId(i);
							break;
						}
					}
				}
				else
				{
					// Ok this is really bad... go random (better than nothing)
					SetId(5000 + LgiRand(2000));
				}
			}
			
			#if 0 // def BEOS
			// This is for debugging only
			static int ChangeId = 1;
			int Cid = ChangeId++;
			// printf("**** M_CHANGE sending  **** Ctrl=%i %s, Data=%i, Cid=%i\n", Ptr->GetId(), Ptr->Name(), Data, Cid);
			
			BMessage Msg(M_CHANGE);
			Msg.AddInt32("a", GetId());
			Msg.AddInt32("b", Data);
			Msg.AddInt32("cid", Cid);
	
			BMessenger m(n->Handle());
			m.SendMessage(&Msg) == B_OK;			
			#else
            LgiAssert(GetId() > 0); // We must have a valid ctrl ID at this point, otherwise
									// the receiver will never be able to find our object.
            
            // printf("Post M_CHANGE %i %i\n", GetId(), Data);
            n->PostEvent(M_CHANGE, (GMessage::Param) GetId(), (GMessage::Param) Data);
            #endif
		}
	}
}

GViewI *GView::GetNotify()
{
	return d->Notify;
}

void GView::SetNotify(GViewI *p)
{
	d->Notify = p;
}

#define ADJ_LEFT			1
#define ADJ_RIGHT			2
#define ADJ_UP				3
#define ADJ_DOWN			4

int IsAdjacent(GRect &a, GRect &b)
{
	if ( (a.x1 == b.x2 + 1) &&
		!(a.y1 > b.y2 || a.y2 < b.y1))
	{
		return ADJ_LEFT;
	}

	if ( (a.x2 == b.x1 - 1) &&
		!(a.y1 > b.y2 || a.y2 < b.y1))
	{
		return ADJ_RIGHT;
	}

	if ( (a.y1 == b.y2 + 1) &&
		!(a.x1 > b.x2 || a.x2 < b.x1))
	{
		return ADJ_UP;
	}

	if ( (a.y2 == b.y1 - 1) &&
		!(a.x1 > b.x2 || a.x2 < b.x1))
	{
		return ADJ_DOWN;
	}

	return 0;
}

GRect JoinAdjacent(GRect &a, GRect &b, int Adj)
{
	GRect t;

	switch (Adj)
	{
		case ADJ_LEFT:
		case ADJ_RIGHT:
		{
			t.y1 = max(a.y1, b.y1);
			t.y2 = min(a.y2, b.y2);
			t.x1 = min(a.x1, b.x1);
			t.x2 = max(a.x2, b.x2);
			break;
		}
		case ADJ_UP:
		case ADJ_DOWN:
		{
			t.y1 = min(a.y1, b.y1);
			t.y2 = max(a.y2, b.y2);
			t.x1 = max(a.x1, b.x1);
			t.x2 = min(a.x2, b.x2);
			break;
		}
	}

	return t;
}

GRect *GView::FindLargest(GRegion &r)
{
	int Pixels = 0;
	GRect *Best = 0;
	static GRect Final;

	// do initial run through the list to find largest single region
	for (GRect *i = r.First(); i; i = r.Next())
	{
		int Pix = i->X() * i->Y();
		if (Pix > Pixels)
		{
			Pixels = Pix;
			Best = i;
		}
	}

	if (Best)
	{
		Final = *Best;
		Pixels = Final.X() * Final.Y();

		int LastPixels = Pixels;
		GRect LastRgn = Final;
		int ThisPixels = Pixels;
		GRect ThisRgn = Final;

		GRegion TempList;
		for (GRect *i = r.First(); i; i = r.Next())
		{
			TempList.Union(i);
		}
		TempList.Subtract(Best);

		do
		{
			LastPixels = ThisPixels;
			LastRgn = ThisRgn;

			// search for adjoining rectangles that maybe we can add
			for (GRect *i = TempList.First(); i; i = TempList.Next())
			{
				int Adj = IsAdjacent(ThisRgn, *i);
				if (Adj)
				{
					GRect t = JoinAdjacent(ThisRgn, *i, Adj);
					int Pix = t.X() * t.Y();
					if (Pix > ThisPixels)
					{
						ThisPixels = Pix;
						ThisRgn = t;

						TempList.Subtract(i);
					}
				}
			}
		} while (LastPixels < ThisPixels);

		Final = ThisRgn;
	}
	else return 0;

	return &Final;
}

GRect *GView::FindSmallestFit(GRegion &r, int Sx, int Sy)
{
	int X = 1000000;
	int Y = 1000000;
	GRect *Best = 0;

	for (GRect *i = r.First(); i; i = r.Next())
	{
		if ((i->X() >= Sx && i->Y() >= Sy) &&
			(i->X() < X || i->Y() < Y))
		{
			X = i->X();
			Y = i->Y();
			Best = i;
		}
	}

	return Best;
}

GRect *GView::FindLargestEdge(GRegion &r, int Edge)
{
	GRect *Best = 0;

	for (GRect *i = r.First(); i; i = r.Next())
	{
		if (!Best)
		{
			Best = i;
		}

		if
		(
			((Edge & GV_EDGE_TOP) && (i->y1 < Best->y1))
			||
			((Edge & GV_EDGE_RIGHT) && (i->x2 > Best->x2))
			||
			((Edge & GV_EDGE_BOTTOM) && (i->y2 > Best->y2))
			||
			((Edge & GV_EDGE_LEFT) && (i->x1 < Best->x1))
		)
		{
			Best = i;
		}

		if
		(
			(
				((Edge & GV_EDGE_TOP) && (i->y1 == Best->y1))
				||
				((Edge & GV_EDGE_BOTTOM) && (i->y2 == Best->y2))
			)
			&&
			(
				i->X() > Best->X()
			)
		)
		{
			Best = i;
		}

		if
		(
			(
				((Edge & GV_EDGE_RIGHT) && (i->x2 == Best->x2))
				||
				((Edge & GV_EDGE_LEFT) && (i->x1 == Best->x1))
			)
			&&
			(
				i->Y() > Best->Y()
			)
		)
		{
			Best = i;
		}
	}

	return Best;
}

GViewI *GView::FindReal(GdcPt2 *Offset)
{
	if (Offset)
	{
		Offset->x = 0;
		Offset->y = 0;
	}

	GViewI *p = d->Parent;
	while (p && !p->Handle())
	{
		if (Offset)
		{
			Offset->x += Pos.x1;
			Offset->y += Pos.y1;
		}

		p = p->GetParent();
	}
	
	if (p && p->Handle())
	{
		return p;
	}

	return NULL;
}

bool GView::HandleCapture(GView *Wnd, bool c)
{
	if (c)
	{
		_Capturing = Wnd;
		
		#if WINNATIVE
			GdcPt2 Offset;
			GViewI *v = _Capturing->Handle() ? _Capturing : FindReal(&Offset);
			HWND h = v ? v->Handle() : NULL;
			if (h)
				SetCapture(h);
			else
				LgiAssert(0);
		#elif defined(LGI_SDL)
			#if SDL_VERSION_ATLEAST(2, 0, 4)
			SDL_CaptureMouse(SDL_TRUE);
			#else
			LgiApp->CaptureMouse(true);
			#endif
		#endif
	}
	else
	{
		_Capturing = NULL;

		#if WINNATIVE
			ReleaseCapture();
		#elif defined(LGI_SDL)
			#if SDL_VERSION_ATLEAST(2, 0, 4)
			SDL_CaptureMouse(SDL_FALSE);
			#else
			LgiApp->CaptureMouse(false);
			#endif
		#endif
	}

	return true;
}

bool GView::IsCapturing()
{
	return _Capturing == this;
}

bool GView::Capture(bool c)
{
	return HandleCapture(this, c);
}

bool GView::Enabled()
{
	#if WINNATIVE
	if (_View)
		return IsWindowEnabled(_View);
	#else
	#endif
	return !TestFlag(GViewFlags, GWF_DISABLED);
}

void GView::Enabled(bool i)
{
	if (!i) SetFlag(GViewFlags, GWF_DISABLED);
	else ClearFlag(GViewFlags, GWF_DISABLED);

	if (_View)
	{
		#if WINNATIVE
		EnableWindow(_View, i);
		#elif defined MAC && !defined COCOA && !defined(LGI_SDL)
		if (i)
		{
			OSStatus e = EnableControl(_View);
			if (e) printf("%s:%i - Error enabling control (%i)\n", _FL, (int)e);
		}
		else
		{
			OSStatus e = DisableControl(_View);
			if (e) printf("%s:%i - Error disabling control (%i)\n", _FL, (int)e);
		}
		#endif
	}

	Invalidate();
}

bool GView::Visible()
{
	#if WINNATIVE

	if (_View)

	/*	This takes into account all the parent windows as well...
		Which is kinda not what I want. I want this to reflect just
		this window.
		
		return IsWindowVisible(_View);
	*/
		return (GetWindowLong(_View, GWL_STYLE) & WS_VISIBLE) != 0;	
	
	#endif

	return TestFlag(GViewFlags, GWF_VISIBLE);
}

void GView::Visible(bool v)
{
	if (v) SetFlag(GViewFlags, GWF_VISIBLE);
	else ClearFlag(GViewFlags, GWF_VISIBLE);

	if (_View)
	{
		#if WINNATIVE
		ShowWindow(_View, (v) ? SW_SHOWNORMAL : SW_HIDE);
		#elif defined(BEOS)
		if (v)
			_View->Show();
		else
			_View->Hide();
		// printf("\t\t%s::Vis(%i)\n", GetClass(), v);
		#elif defined __GTK_H__
		ThreadCheck();
		if (v)
			Gtk::gtk_widget_show(_View);
		else
			Gtk::gtk_widget_hide(_View);

		#elif defined MAC && !defined COCOA && !defined(LGI_SDL)
		OSErr e = SetControlVisibility(_View, v, true);
		if (e) printf("%s:%i - SetControlVisibility(%p,%i,1) failed with %i (class=%s)\n",
						_FL, _View, v, e, GetClass());
		#endif
	}
	else
	{
		Invalidate();
		// printf("\t\t%s::Vis(%i) virtual\n", GetClass(), v);
	}
}

bool GView::Focus()
{
	bool Has = false;
	GWindow *w = GetWindow();
   
	#if defined(__GTK_H__) || defined(BEOS)
	if (w)
	{
		bool Active = w->IsActive();
		if (Active)
		{
			Has = w->GetFocus() == static_cast<GViewI*>(this);
			/*
			printf("%s::Focus() active: %p == %p = %i\n",
				GetClass(),
				w->GetFocus(),
				static_cast<GViewI*>(this),
				Has);
			*/
		}
		else
		{
			// printf("%s::Focus() not active\n", GetClass());
		}
	}
	#elif defined(WINNATIVE)
	if (_View)
	{
		HWND hFocus = GetFocus();
		Has = hFocus == _View;
	}
	#elif defined(MAC) && !defined(LGI_SDL)
	if (w)
	{
		ControlRef Cur;
		OSErr e = GetKeyboardFocus(w->WindowHandle(), &Cur);
		if (e)
			LgiTrace("%s:%i - GetKeyboardFocus failed with %i\n", _FL, e);
		else
			Has = (Cur == _View);
	}
  	#endif

	if (Has)
		SetFlag(WndFlags, GWF_FOCUS);
	else
		ClearFlag(WndFlags, GWF_FOCUS);
	
	return Has;
}

void GView::Focus(bool i)
{
	if (i)
		SetFlag(WndFlags, GWF_FOCUS);
	else
		ClearFlag(WndFlags, GWF_FOCUS);

	GWindow *Wnd = GetWindow();
	if (Wnd)
		Wnd->SetFocus(this, i ? GWindow::GainFocus : GWindow::LoseFocus);

	if (_View)
	{
		#if WINNATIVE

			if (i)
			{

				HWND hCur = GetFocus();
				if (hCur != _View)
				{
					if (In_SetWindowPos)
					{
						assert(0);
						LgiTrace("%s:%i - SetFocus %p (%-30s)\n", _FL, Handle(), Name());
					}

					SetFocus(_View);
				}
			}
			else
			{
				if (In_SetWindowPos)
				{
					assert(0);
					LgiTrace("%s:%i - SetFocus(%p)\n", _FL, GetDesktopWindow());
				}

				SetFocus(GetDesktopWindow());
			}

		#elif defined __GTK_H__

		#elif defined MAC && !defined(LGI_SDL)

			#if !defined COCOA
				GViewI *Wnd = GetWindow();
				if (Wnd && i)
				{
					OSErr e = SetKeyboardFocus(Wnd->WindowHandle(), _View, 1);
					if (e) printf("%s:%i - error setting keyboard focus (%i) to %s\n", _FL, e, GetClass());
				}
				else printf("%s:%i - no window?\n", _FL);
			#endif

		#endif
	}
}

GDragDropSource *GView::DropSource(GDragDropSource *Set)
{
	if (Set)
		d->DropSource = Set;

	return d->DropSource;
}

GDragDropTarget *GView::DropTarget(GDragDropTarget *Set)
{
	if (Set)
		d->DropTarget = Set;

	return d->DropTarget;
}

#if defined MAC
extern pascal OSStatus LgiViewDndHandler(EventHandlerCallRef inHandlerCallRef, EventRef inEvent, void *inUserData);
#endif

bool GView::DropTarget(bool t)
{
	bool Status = false;

	if (t) SetFlag(GViewFlags, GWF_DROP_TARGET);
	else ClearFlag(GViewFlags, GWF_DROP_TARGET);

	#if WINNATIVE
	if (_View)
	{
		if (t)
		{
			if (!d->DropTarget)
			{
				DragAcceptFiles(_View, t);
			}
			else
			{
				Status = RegisterDragDrop(_View, (IDropTarget*) d->DropTarget) == S_OK;
			}
		}
		else
		{
			if (_View && d->DropTarget)
			{
				Status = RevokeDragDrop(_View) == S_OK;
			}
		}
	}
	else LgiAssert(!"No window handle");

	#elif defined MAC && !defined(LGI_SDL)

	GWindow *Wnd = dynamic_cast<GWindow*>(GetWindow());
	if (Wnd)
	{
		Wnd->SetDragHandlers(t);
		if (!d->DropTarget)
			d->DropTarget = t ? Wnd : 0;
	}

	if (t)
	{
		static EventTypeSpec DragEvents[] =
		{
			{ kEventClassControl, kEventControlDragEnter },
			{ kEventClassControl, kEventControlDragWithin },
			{ kEventClassControl, kEventControlDragLeave },
			{ kEventClassControl, kEventControlDragReceive },
		};
		
		if (!d->DndHandler)
		{
			OSStatus e = ::InstallControlEventHandler(_View,
													NewEventHandlerUPP(LgiViewDndHandler),
													GetEventTypeCount(DragEvents),
													DragEvents,
													(void*)this,
													&d->DndHandler);
			if (e) LgiTrace("%s:%i - InstallEventHandler failed (%i)\n", _FL, e);
		}
		SetControlDragTrackingEnabled(_View, true);
	}
	else
	{
		SetControlDragTrackingEnabled(_View, false);
	}

	#elif defined __GTK_H__
	if (_View)
	{
		if (t)
		{
			Gtk::GtkTargetEntry targets[] =
			{
				{ "STRING",							0, 0 },
				{ "text/plain",						0, 0 },
				{ "text/uri-list",					0, 0 },
				{ "application/x-rootwindow-drop",	0, 0 },
			};

			Gtk::gtk_drag_dest_set(	_View,
									(Gtk::GtkDestDefaults)0,
									#if 1
									NULL,
									0,
									#else
									targets,
									CountOf(targets),
									#endif
									Gtk::GDK_ACTION_DEFAULT);
		}
		else
		{
			Gtk::gtk_drag_dest_unset(_View);
		}
		
		Status = true;
	}
	#endif

	return Status;
}

bool GView::Sunken()
{
	#if WINNATIVE
	return TestFlag(d->WndExStyle, WS_EX_CLIENTEDGE);
	#else
	return TestFlag(GViewFlags, GWF_SUNKEN);
	#endif
}

void GView::Sunken(bool i)
{
	#if WINNATIVE
	if (i) SetFlag(d->WndExStyle, WS_EX_CLIENTEDGE);
	else ClearFlag(d->WndExStyle, WS_EX_CLIENTEDGE);
	if (_View)
	    SetWindowLong(_View, GWL_EXSTYLE, d->WndExStyle);
	#else
	if (i) SetFlag(GViewFlags, GWF_SUNKEN);
	else ClearFlag(GViewFlags, GWF_SUNKEN);
	#endif

	if (!_BorderSize && i)
	{
		_BorderSize = 2;
	}
}

bool GView::Flat()
{
	#if WINNATIVE
	return	!TestFlag(d->WndExStyle, WS_EX_CLIENTEDGE) &&
			!TestFlag(d->WndExStyle, WS_EX_WINDOWEDGE);
	#else
	return !TestFlag(GViewFlags, GWF_SUNKEN) &&
		   !TestFlag(GViewFlags, GWF_RAISED);
	#endif
}

void GView::Flat(bool i)
{
	#if WINNATIVE
	ClearFlag(d->WndExStyle, (WS_EX_CLIENTEDGE|WS_EX_WINDOWEDGE));
	#else
	ClearFlag(GViewFlags, (GWF_RAISED|GWF_SUNKEN));
	#endif
}

bool GView::Raised()
{
	#if WINNATIVE
	return TestFlag(d->WndExStyle, WS_EX_WINDOWEDGE);
	#else
	return TestFlag(GViewFlags, GWF_RAISED);
	#endif
}

void GView::Raised(bool i)
{
	#if WINNATIVE
	if (i) SetFlag(d->WndExStyle, WS_EX_WINDOWEDGE);
	else ClearFlag(d->WndExStyle, WS_EX_WINDOWEDGE);
	#else
	if (i) SetFlag(GViewFlags, GWF_RAISED);
	else ClearFlag(GViewFlags, GWF_RAISED);
	#endif

	if (!_BorderSize && i)
	{
		_BorderSize = 2;
	}
}

int GView::GetId()
{
	return d->CtrlId;
}

void GView::SetId(int i)
{
	d->CtrlId = i;

	#if WINNATIVE
	if (_View)
		SetWindowLong(_View, GWL_ID, d->CtrlId);
	#elif defined __GTK_H__
	#elif defined MAC
	#endif
}

bool GView::GetTabStop()
{
	#if WINNATIVE
	return TestFlag(d->WndStyle, WS_TABSTOP);
	#else
	return d->TabStop;
	#endif
}

void GView::SetTabStop(bool b)
{
	#if WINNATIVE
	if (b)
		SetFlag(d->WndStyle, WS_TABSTOP);
	else
		ClearFlag(d->WndStyle, WS_TABSTOP);
	#else
	d->TabStop = b;
	#endif
	#ifdef __GTK_H__
	if (_View)
	{
		#if GtkVer(2, 18)
		ThreadCheck();
        gtk_widget_set_can_focus(_View, b);
		#else
		LgiTrace("Error: no api to set tab stop.\n");
		#endif
    }
	#endif
}

int64 GView::GetCtrlValue(int Id)
{
	GViewI *w = FindControl(Id);
	if (!w)
		printf("%s:%i - Ctrl %i not found.\n", _FL, Id);
	return (w) ? w->Value() : 0;
}

void GView::SetCtrlValue(int Id, int64 i)
{
	GViewI *w = FindControl(Id);
	if (!w)
		printf("%s:%i - Ctrl %i not found.\n", _FL, Id);
	if (w)
		w->Value(i);
}

char *GView::GetCtrlName(int Id)
{
	GViewI *w = FindControl(Id);
	if (!w)
		printf("%s:%i - Ctrl %i not found.\n", _FL, Id);
	return (w) ? w->Name() : 0;
}

void GView::SetCtrlName(int Id, const char *s)
{
	GViewI *w = FindControl(Id);
	if (!w)
		printf("%s:%i - Ctrl %i not found.\n", _FL, Id);
	if (w)
		w->Name(s);
}

bool GView::GetCtrlEnabled(int Id)
{
	GViewI *w = FindControl(Id);
	if (!w)
		printf("%s:%i - Ctrl %i not found.\n", _FL, Id);
	return (w) ? w->Enabled() : 0;
}

void GView::SetCtrlEnabled(int Id, bool Enabled)
{
	GViewI *w = FindControl(Id);
	if (!w)
		printf("%s:%i - Ctrl %i not found.\n", _FL, Id);
	if (w)
		w->Enabled(Enabled);
}

bool GView::GetCtrlVisible(int Id)
{
	GViewI *w = FindControl(Id);
	if (!w)
		printf("%s:%i - Ctrl %i not found.\n", _FL, Id);
	return (w) ? w->Visible() : 0;
}

void GView::SetCtrlVisible(int Id, bool v)
{
	GViewI *w = FindControl(Id);
	if (!w)
		printf("%s:%i - Ctrl %i not found.\n", _FL, Id);
	else
		w->Visible(v);
}

bool GView::AttachChildren()
{
	List<GViewI>::I it = Children.Start();
	while (it.Each())
	{
		GViewI *c = *it;
		if (!c->IsAttached())
		{
			if (!c->Attach(this))
			{
				LgiTrace("%s:%i - failed to attach %s\n", _FL, c->GetClass());
				return false;
			}
		}
	}

	return true;
}

GFont *GView::GetFont()
{
	const char *Cls = GetClass();
	
	if (!d->Font &&
		d->Css &&
		LgiResources::GetLoadStyles())
	{
		GFontCache *fc = LgiApp->GetFontCache();
		if (fc)
		{
			GFont *f = fc->GetFont(d->Css);
			if (f)
			{
				if (d->FontOwnType == GV_FontOwned)
					DeleteObj(d->Font);
				d->Font = f;
				d->FontOwnType = GV_FontCached;
			}
		}
	}
	
	return d->Font ? d->Font : SysFont;
}

void GView::SetFont(GFont *Font, bool OwnIt)
{
	bool Change = d->Font != Font;
	if (Change)
	{
		if (d->FontOwnType == GV_FontOwned)
		{
			DeleteObj(d->Font);
		}

		d->FontOwnType = OwnIt ? GV_FontOwned : GV_FontPtr;
		d->Font = Font;
		#if WINNATIVE
		if (_View)
			SendMessage(_View, WM_SETFONT, (WPARAM) (Font ? Font->Handle() : 0), 0);
		#endif

		for (GViewI *p = GetParent(); p; p = p->GetParent())
		{
			GTableLayout *Tl = dynamic_cast<GTableLayout*>(p);
			if (Tl)
			{
				Tl->InvalidateLayout();
				break;
			}
		}

		Invalidate();
	}
}

bool GView::IsOver(GMouse &m)
{
	return	(m.x >= 0) &&
			(m.y >= 0) &&
			(m.x < Pos.X()) &&
			(m.y < Pos.Y());
}

bool GView::WindowVirtualOffset(GdcPt2 *Offset)
{
	bool Status = false;

	if (Offset)
	{
		Offset->x = 0;
		Offset->y = 0;
		
		for (GViewI *Wnd = this; Wnd; Wnd = Wnd->GetParent())
		{
			if (!Wnd->Handle())
			{
				GRect r = Wnd->GetPos();
				GViewI *Par = Wnd->GetParent();
				if (Par)
				{
					GRect c = Par->GetClient(false);
					Offset->x += r.x1 + c.x1;
					Offset->y += r.y1 + c.y1;
				}
				else
				{
					Offset->x += r.x1;
					Offset->y += r.y1;
				}

				Status = true;
			}
			else break;
		}
	}

	return Status;
}

static int Debug_Depth = 0;

GViewI *GView::WindowFromPoint(int x, int y, bool Debug)
{
	char Tabs[64];
	if (Debug)
	{
		memset(Tabs, 9, Debug_Depth);
		Tabs[Debug_Depth] = 0;
	}

	// We iterate over the child in reverse order because if they overlap the
	// end of the list is on "top". So they should get the click or whatever
	// before the the lower windows.
	List<GViewI>::I it = Children.End();
	for (GViewI *c = *it; c; c = *--it)
	{
		GRect CPos = c->GetPos();
		
		if (CPos.Overlap(x, y) && c->Visible())
		{
			GRect CClient;
			CClient = c->GetClient(false);

            int Ox = CPos.x1 + CClient.x1;
            int Oy = CPos.y1 + CClient.y1;
			if (Debug)
			{
				LgiTrace("%s%s Pos=%s Client=%s m(%i,%i)->(%i,%i)\n",
						Tabs,
						c->GetClass(),
						CPos.GetStr(),
						CClient.GetStr(),
						x, y,
						x - Ox, y - Oy);
			}

			Debug_Depth++;
			GViewI *Child = c->WindowFromPoint(x - Ox, y - Oy, Debug);
			Debug_Depth--;
			if (Child)
			{
				return Child;
			}
		}
		else if (Debug)
		{
			LgiTrace("%sMISSED %s Pos=%s m(%i,%i)\n", Tabs, c->GetClass(), CPos.GetStr(), x, y);
		}
	}

	if (x >= 0 && y >= 0 && x < Pos.X() && y < Pos.Y())
	{
		return this;
	}

	return NULL;
}

bool GView::InThread()
{
	#if WINNATIVE
	HWND Hnd = _View;
	for (GViewI *p = GetParent(); p && !Hnd; p = p->GetParent())
	{
		Hnd = p->Handle();
	}
	
	DWORD CurThread = GetCurrentThreadId();
	DWORD ViewThread = GetWindowThreadProcessId(Hnd, NULL);
	return CurThread == ViewThread;
	#else
	OsThreadId Me = LgiGetCurrentThread();
	return LgiApp && LgiApp->GetGuiThread() == Me;
	#endif
}

bool GView::PostEvent(int Cmd, GMessage::Param a, GMessage::Param b)
{
	if (_View)
	{
		#if WINNATIVE
		return PostMessage(_View, Cmd, a, b);
		#else
		bool Ret = LgiPostEvent(_View, Cmd, a, b);
		return Ret;
		#endif
	}
	else if (InThread())
	{
		GMessage e(Cmd, a, b);
		OnEvent(&e);
		return true;
	}
	else
	{
		LgiTrace("%s:%i - No view to post event to.\n", _FL);
	}

	return false;
}

bool GView::Invalidate(GRegion *r, bool Repaint, bool NonClient)
{
	if (r)
	{
		for (int i=0; i<r->Length(); i++)
		{
			bool Last = i == r->Length()-1;
			Invalidate((*r)[i], Last ? Repaint : false, NonClient);
		}
		
		return true;
	}
	
	return false;
}

GButton *FindDefault(GViewI *w)
{
	GButton *But = 0;

	GViewIterator *i = w->IterateViews();
	if (i)
	{
		for (GViewI *c = i->First(); c; c = i->Next())
		{
			But = dynamic_cast<GButton*>(c);
			if (But && But->Default())
			{
				break;
			}
			
			But = FindDefault(c);
			if (But)
			{
				break;
			}
		}

		DeleteObj(i);
	}

	return But;
}

bool GView::Name(const char *n)
{
	GBase::Name(n);

	if (_View)
	{
		#if WINNATIVE
		char16 *Temp = GBase::NameW();
		SetWindowTextW(_View, Temp ? Temp : L"");
		#endif
	}

	Invalidate();

	return true;
}

char *GView::Name()
{
	#if WINNATIVE
	if (_View)
	{
		GView::NameW();
	}
	#endif

	return GBase::Name();
}

bool GView::NameW(const char16 *n)
{
	GBase::NameW(n);

	#if WINNATIVE
	if (_View && n)
	{
		char16 *Txt = GBase::NameW();
		SetWindowTextW(_View, Txt);
	}
	#endif

	Invalidate();
	return true;
}

char16 *GView::NameW()
{
	#if WINNATIVE
	if (_View)
	{
		int Length = GetWindowTextLengthW(_View);
		if (Length > 0)
		{
			char16 *Buf = new char16[Length+1];
			if (Buf)
			{
				Buf[0] = 0;
				int Chars = GetWindowTextW(_View, Buf, Length+1);
				Buf[Chars] = 0;
				GBase::NameW(Buf);
			}
			DeleteArray(Buf);
		}
		else
		{
			GBase::NameW(0);
		}
	}
	#endif

	return GBase::NameW();
}

GViewI *GView::FindControl(int Id)
{
	LgiAssert(Id != -1);

	if (GetId() == Id)
	{
		return this;
	}

	List<GViewI>::I List = Children.Start();
	while (List.Each())
	{
		GViewI *c = *List;
		GViewI *Ctrl = c->FindControl(Id);
		if (Ctrl)
		{
			return Ctrl;
		}
	}
	return 0;
}

GdcPt2 GView::GetMinimumSize()
{
	return d->MinimumSize;
}

void GView::SetMinimumSize(GdcPt2 Size)
{
	d->MinimumSize = Size;

	bool Change = false;
	GRect p = Pos;
	if (X() < d->MinimumSize.x)
	{
		p.x2 = p.x1 + d->MinimumSize.x - 1;
		Change = true;
	}
	if (Y() < d->MinimumSize.y)
	{
		p.y2 = p.y1 + d->MinimumSize.y - 1;
		Change = true;
	}
	if (Change)
	{
		SetPos(p);
	}
}

bool GView::SetColour(GColour &c, bool Fore)
{
	GCss *css = GetCss(true);
	if (!css)
		return false;
	
	if (Fore)
	{
		if (c.IsValid())
			css->Color(GCss::ColorDef(GCss::ColorRgb, c.c32()));
		else
			css->DeleteProp(GCss::PropColor);
	}
	else
	{
		if (c.IsValid())
			css->BackgroundColor(GCss::ColorDef(GCss::ColorRgb, c.c32()));
		else
			css->DeleteProp(GCss::PropBackgroundColor);
	}
	
	return true;
}


bool GView::SetCssStyle(const char *CssStyle)
{
    if (!d->Css && !d->Css.Reset(new GCss))
		return false;
    
    const char *Defs = CssStyle;
    bool b = d->Css->Parse(Defs, GCss::ParseRelaxed);
    if (b && d->FontOwnType == GV_FontCached)
    {
		d->Font = NULL;
		d->FontOwnType = GV_FontPtr;
    }
    
    return b;    
}

void GView::SetCss(GCss *css)
{
	d->Css.Reset(css);
}

GCss *GView::GetCss(bool Create)
{
    if (Create && !d->Css)
        d->Css.Reset(new GCss);

    return d->Css;
}

GdcPt2 &GView::GetWindowBorderSize()
{
	static GdcPt2 s;

	ZeroObj(s);

	#if WINNATIVE
	if (_View)
	{
		RECT Wnd, Client;
		GetWindowRect(Handle(), &Wnd);
		GetClientRect(Handle(), &Client);
		s.x = (Wnd.right-Wnd.left) - (Client.right-Client.left);
		s.y = (Wnd.bottom-Wnd.top) - (Client.bottom-Client.top);
	}
	#elif defined __GTK_H__
	#elif defined MAC
	s.x = 0;
	s.y = 22;
	#endif

	return s;
}

#ifdef _DEBUG
void GView::_Dump(int Depth)
{
	char Sp[65];
	memset(Sp, ' ', Depth << 2);
	Sp[Depth<<2] = 0;
	char s[256];
	sprintf_s(s, sizeof(s), "%s%p::%s %s (_View=%p)\n", Sp, this, GetClass(), GetPos().GetStr(), _View);
	LgiTrace(s);
	List<GViewI>::I i = Children.Start();
	for (GViewI *c = *i; c; c = *++i)
	{
		GView *v = c->GetGView();
		if (v)
			v->_Dump(Depth+1);
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////
#if defined(WINNATIVE)
static HANDLE FactoryEvent;
#else
static char FactoryFile[MAX_PATH];
#endif
static GArray<GViewFactory*> *AllFactories = NULL;

GViewFactory::GViewFactory()
{
	#if defined(WINNATIVE)
	char Name[256];
	sprintf_s(Name, sizeof(Name), "LgiFactoryEvent.%i", GetCurrentProcessId());
	HANDLE h = CreateEvent(NULL, false, false, Name);
	DWORD err = GetLastError();
	if (err != ERROR_ALREADY_EXISTS)
	{
		FactoryEvent = h;
		AllFactories = new GArray<GViewFactory*>;
	}
	else
	{
		LgiAssert(AllFactories);
	}
	#else
	// This is a terrible way of doing it... but I don't have a better solution ATM. :(
	LgiGetTempPath(FactoryFile, sizeof(FactoryFile));
	int len = strlen(FactoryFile);
	#ifdef POSIX
	sprintf_s(FactoryFile+len, sizeof(FactoryFile)-len, "/LgiFactoryFile.%i", getpid());
	#else
	sprintf_s(FactoryFile+len, sizeof(FactoryFile)-len, "/LgiFactoryFile");
	#endif
	if (!FileExists(FactoryFile))
	{
		GFile file;
		file.Open(FactoryFile, O_WRITE);
		AllFactories = new GArray<GViewFactory*>;
	}
	#endif

	if (AllFactories)
	{
		AllFactories->Add(this);
	}
}

GViewFactory::~GViewFactory()
{
	if (AllFactories)
	{
		AllFactories->Delete(this);
		if (AllFactories->Length() == 0)
		{
			DeleteObj(AllFactories);
			#if defined(WINNATIVE)
			CloseHandle(FactoryEvent);
			#else
			unlink(FactoryFile);
			#endif
		}
	}
}

GView *GViewFactory::Create(const char *Class, GRect *Pos, const char *Text)
{
	if (ValidStr(Class) && AllFactories)
	{
		for (int i=0; i<AllFactories->Length(); i++)
		{
			GView *v = (*AllFactories)[i]->NewView(Class, Pos, Text);
			if (v)
			{
				return v;
			}
		}
	}

	return 0;
}
