#include "Lgi.h"
#include "GBrowser.h"
#include "GHtml2.h"
#include "INet.h"
#include "IHttp.h"
#include "GEdit.h"
#include "GButton.h"
#include "GToken.h"

#define M_LOADED		(M_USER+3000)
#define M_BUSY			(M_USER+3001)

enum {
	IDC_HTML = 100,
	IDC_URI,
	IDC_BACK,
	IDC_FORWARD,
	IDC_REFRESH_STOP
};

class GBrowserPriv;
class GBrowserThread : public GThread, public GSemaphore
{
	GBrowserPriv *d;
	GArray<GAutoString> Work;
	bool Loop;
	OsSocket SockHnd;

public:
	GBrowserThread(GBrowserPriv *priv);
	~GBrowserThread();

	bool Add(char *Uri);
	void Stop();
	int Main();
};

class GBrowserPriv : public GDocumentEnv
{
public:
	typedef GHashTbl<char*,GStream*> Collection;

	struct FileLock : public GSemaphore::Auto
	{
		Collection *Files;

		FileLock(GSemaphore *s, char *file, int line) : GSemaphore::Auto(s, file, line)
		{
			Files = 0;
		}
	};

	typedef GAutoPtr<FileLock> FilePtr;

private:
	Collection Files; // requires locking to access

public:
	GBrowser *Wnd;
	Html2::GHtml2 *Html;
	GAutoPtr<GBrowserThread> Thread;
	GEdit *UriEdit;
	GButton *Back;
	GButton *Forward;
	GButton *Stop;
	GArray<GAutoString> History;
	int CurHistory;
	bool Loading;

	GBrowserPriv(GBrowser *wnd)
	{
		Wnd = wnd;
		Html = 0;
		Back = Forward = 0;
		CurHistory = 0;
		Loading = false;
	}

	FilePtr Lock()
	{
		FilePtr p(new FileLock(this, _FL));
		p->Files = &Files;
		return p;
	}

	void LoadStream(GStream *s)
	{
		int64 len = s->GetSize();
		GArray<char> txt;
		txt.Length(len);
		s->Read(&txt[0], len);
		txt.Add(0);
		Html->Name(&txt[0]);
	}

	bool LoadCurrent()
	{
		char *Uri = History[CurHistory];
		if (!Uri)
			return false;

		GUri u(Uri);
		bool IsFile = FileExists(u.Path);
		bool IsHttp = false;

		if (IsFile)
		{
			u.Protocol = NewStr("file");
		}
		else if (u.Set(Uri))
		{
			if (u.Protocol)
			{
				if (!stricmp(u.Protocol, "file"))
				{
					if (!u.Path)
						return false;

					IsFile = true;
				}
				else if (!stricmp(u.Protocol, "http"))
				{
					IsHttp = true;
				}
				else
					return false; // Protocol not support
			}
			else
			{
				// Probably...?
				IsHttp = true;
				u.Protocol = NewStr("http");
				History[CurHistory].Reset(u.Get());
				Uri = History[CurHistory];
			}
		}
		
		if (IsFile)
		{
			GAutoString txt(ReadTextFile(u.Path));
			if (txt)
			{
				Html->Name(txt);
			}
			else return false;
		}
		else if (IsHttp)
		{
			if (!Thread)
				Thread.Reset(new GBrowserThread(this));
			Thread->Add(Uri);
		}

		if (u.Anchor)
		{
			Html->GotoAnchor(u.Anchor);
		}

		GAutoString a(u.Get());
		UriEdit->Name(a);

		Wnd->SetCtrlEnabled(IDC_BACK, CurHistory > 0);
		Wnd->SetCtrlEnabled(IDC_FORWARD, CurHistory < History.Length() - 1);

		return true;
	}

	bool OnNavigate(char *Uri)
	{
		GUri u(Uri);
		char Sep, Buf[MAX_PATH];
		if (!u.Protocol)
		{
			// Relative link?
			char *Cur = History[CurHistory], *c;
			if (strchr(Cur, '\\'))
				Sep = '\\';
			else
				Sep = '/';
			
			// Trim off filename...
			char *Last = strrchr(Cur, Sep);
			GToken t(Uri, "\\/");
			int Ch;
			if (*Uri != '#' && Last)
				sprintf(Buf, "%.*s", Last - Cur, Cur);
			else
				strsafecpy(Buf, Cur, sizeof(Buf));

			char *End = Buf + strlen(Buf) - 1;
			if (*End == Sep)
				*End = 0;

			for (int i=0; i<t.Length(); i++)
			{
				if (!stricmp(t[i], "."))
					;
				else if (!stricmp(t[i], ".."))
				{
					if (End = strrchr(Buf, Sep))
						*End = 0;
				}
				else if (t[i][0] == '#')
					sprintf(Buf+strlen(Buf), "%s", t[i]);
				else
					sprintf(Buf+strlen(Buf), "%c%s", Sep, t[i]);
			}

			Uri = Buf;
		}
		else if (!stricmp(u.Protocol, "mailto"))
		{
			LgiExecute(Uri);
			return true;
		}

		return Wnd->SetUri(Uri);
	}

	LoadType GetContent(LoadJob *&j)
	{
		if (!j || !j->Uri)
			return LoadError;

		char *Uri = History[CurHistory];
		GUri u(j->Uri);
		char *LoadFileName = 0;
		if (u.Protocol)
		{
			
		}
		else LoadFileName = j->Uri;		
		
		if (LoadFileName)
		{
			char p[MAX_PATH];
			LgiMakePath(p, sizeof(p), Uri, "..");
			LgiMakePath(p, sizeof(p), p, LoadFileName);
			if (FileExists(p))
			{
				char *Ext = LgiGetExtension(p);
				if
				(
					Ext &&
					(
						!stricmp(Ext, "jpg") ||
						!stricmp(Ext, "jpeg") ||
						!stricmp(Ext, "gif") ||
						!stricmp(Ext, "png") ||
						!stricmp(Ext, "tif") ||
						!stricmp(Ext, "tiff") ||
						!stricmp(Ext, "bmp") ||
						!stricmp(Ext, "ico")
					)
				)
				{
					j->pDC.Reset(LoadDC(p));
					return LoadImmediate;
				}
				else
				{
					GFile *f;
					if (j->Stream.Reset(f = new GFile))
					{
						if (!f->Open(p, O_READ))
						{
							j->Stream.Reset();
							j->Error.Reset(NewStr("Can't open file."));
							return LoadError;
						}

						return LoadImmediate;
					}
				}
			}
		}

		return LoadError;
	}
};

GBrowserThread::GBrowserThread(GBrowserPriv *priv)
{
	Loop = true;
	d = priv;
	SockHnd = INVALID_SOCKET;
	Run();
}

GBrowserThread::~GBrowserThread()
{
	Loop = false;
	while (!IsExited())
		LgiSleep(10);
}

void GBrowserThread::Stop()
{
	if (SockHnd != INVALID_SOCKET)
	{
		LgiTrace("CloseSock %p\n", SockHnd);
		#ifdef WIN32
		closesocket(SockHnd);
		#else
		close(SockHnd);
		#endif
		SockHnd = INVALID_SOCKET;
	}
}

bool GBrowserThread::Add(char *Uri)
{
	if (Lock(_FL))
	{
		Work.New().Reset(NewStr(Uri));
		Unlock();
		return true;
	}
	return false;
}

int GBrowserThread::Main()
{
	bool IsBusy = false;

	while (Loop)
	{
		GAutoString Uri;
		if (Lock(_FL))
		{
			if (Work.Length())
			{
				Uri = Work[0];
				Work.DeleteAt(0, true);

				if (!IsBusy)
					d->Wnd->PostEvent(M_BUSY, IsBusy = true);
			}
			else if (IsBusy)
				d->Wnd->PostEvent(M_BUSY, IsBusy = false);

			Unlock();
		}

		if (Uri)
		{
			GUri u(Uri);
			if (!u.Port)
				u.Port = HTTP_PORT;

			IHttp h;
			int Status = 0;
			GAutoPtr<GStream> p(new GStringPipe);
			if (p)
			{
				GProxyUri Proxy;
				if (Proxy.Host)
					h.SetProxy(Proxy.Host, Proxy.Port);

				GSocket *Sock = new GSocket;
				if (h.Open(Sock, u.Host, u.Port))
				{
					SockHnd = Sock->Handle();
					LgiTrace("Sock %p\n", SockHnd);
					bool b = h.GetFile(0, Uri, *p, GET_TYPE_NORMAL|GET_NO_CACHE, &Status);
					LgiTrace("b %i\n", b);
					GBrowserPriv::FilePtr f = d->Lock();
					if (!b)
					{
						p->Print("<h1>HTTP Error</h1>\nCode: %i<br>", Status);
					}
					f->Files->Add(Uri, p.Release());
					d->Wnd->PostEvent(M_LOADED);
					SockHnd = INVALID_SOCKET;
					LgiTrace("Sock %p\n", SockHnd);
				}
			}
		}
		else LgiSleep(50);
	}

	return false;
}

GBrowser::GBrowser(char *Title, char *Uri)
{
	d = new GBrowserPriv(this);
	d->Back = 0;
	d->Forward = 0;
	d->UriEdit = 0;
	d->Html = 0;
	d->Stop = 0;
	Name(Title?Title:(char*)"Browser");

	GRect r(0, 0, 800, 600);
	SetPos(r);
	MoveToCenter();

	if (Attach(0))
	{
		AddView(d->Back = new GButton(IDC_BACK, 0, 0, 30, 20, "<-"));
		AddView(d->Forward = new GButton(IDC_FORWARD, 0, 0, 30, 20, "->"));
		AddView(d->Stop = new GButton(IDC_REFRESH_STOP, 0, 0, -1, 20, "Refresh"));
		AddView(d->UriEdit = new GEdit(IDC_URI, 0, 0, 100, 20, 0));
		AddView(d->Html = new Html2::GHtml2(IDC_HTML, 0, 0, 100, 100));

		AttachChildren();
		OnPosChange();
		Visible(true);

		d->Html->SetEnv(d);
		d->Html->SetLinkDoubleClick(false);
		d->Back->Enabled(false);
		d->Forward->Enabled(false);
		if (Uri)
			SetUri(Uri);
	}
}

GBrowser::~GBrowser()
{
	DeleteObj(d);
}

bool GBrowser::SetUri(char *Uri)
{
	if (Uri)
	{
		char s[MAX_PATH];
		if (DirExists(Uri))
		{
			sprintf(s, "%s%cindex.html", Uri, DIR_CHAR);
			Uri = s;
		}

		// Delete history past the current point
		while (d->History.Length() > d->CurHistory + 1)
		{
			d->History.DeleteAt(d->CurHistory + 1);
		}

		// Add a new URI to the history
		d->History.New().Reset(NewStr(Uri));
		d->CurHistory = d->History.Length() - 1;

		// Enabled the controls accordingly
		d->Back->Enabled(d->CurHistory > 0);
		d->Forward->Enabled(d->CurHistory < d->History.Length() - 1);

		// Show the content
		d->LoadCurrent();
	}
	else
	{
		d->Html->Name("");
	}

	return true;
}

void GBrowser::OnPosChange()
{
	GRect c = GetClient();
	GRect e = c;
	e.y2 = e.y1 + SysFont->GetHeight() + 8;
	GRect back = e;
	GRect forward = e;
	GRect stop = e;
	back.x2 = back.x1 + (d->Back ? d->Back->X() - 1 : 0);
	forward.x1 = back.x2 + 1;
	forward.x2 = forward.x1 + (d->Forward ? d->Forward->X() - 1 : 0);
	stop.x1 = forward.x2 + 1;
	stop.x2 = stop.x1 + (d->Stop ? d->Stop->X() - 1 : 0);
	e.x1 = stop.x2 + 1;

	GRect h = c;
	h.y1 = e.y2 + 1;

	if (d->Back)
		d->Back->SetPos(back);
	if (d->Forward)
		d->Forward->SetPos(forward);
	if (d->Stop)
		d->Stop->SetPos(stop);
	if (d->UriEdit)
		d->UriEdit->SetPos(e);
	if (d->Html)
		d->Html->SetPos(h);
}

int GBrowser::OnNotify(GViewI *c, int f)
{
	switch (c->GetId())
	{
		case IDC_URI:
		{
			printf("uri notify %i\n", f);
			if (f == VK_RETURN)
			{
				char *u = c->Name();
				SetUri(u);
				break;
			}
			break;
		}
		case IDC_BACK:
		{
			if (d->CurHistory > 0)
			{
				d->CurHistory--;
				d->LoadCurrent();
			}
			break;
		}
		case IDC_FORWARD:
		{
			if (d->CurHistory < d->History.Length() - 1)
			{
				d->CurHistory++;
				d->LoadCurrent();
			}
			break;
		}
		case IDC_REFRESH_STOP:
		{
			if (d->Loading)
			{
				d->Thread->Stop();
			}
			else // refresh
			{
				d->LoadCurrent();
			}
			break;
		}
	}

	return 0;
}

int GBrowser::OnEvent(GMessage *m)
{
	switch (MsgCode(m))
	{
		case M_LOADED:
		{
			GBrowserPriv::FilePtr f = d->Lock();

			char *Uri = d->History[d->CurHistory];
			GStream *s = f->Files->Find(Uri);
			if (s)
			{
				f->Files->Delete(Uri);
				d->LoadStream(s);
				DeleteObj(s);
			}			
			break;
		}
		case M_BUSY:
		{
			if (d->Loading = MsgA(m))
				SetCtrlName(IDC_REFRESH_STOP, "Stop");
			else
				SetCtrlName(IDC_REFRESH_STOP, "Refresh");
			break;
		}
	}
	return GWindow::OnEvent(m);
}
