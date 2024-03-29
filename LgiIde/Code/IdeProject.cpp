#if defined(WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <stdio.h>

#include "Lgi.h"
#include "LgiIde.h"
#include "GDragAndDrop.h"
#include "GToken.h"
#include "resdefs.h"
#include "GProcess.h"
#include "GCombo.h"
#include "INet.h"
#include "GListItemCheckBox.h"
#include "FtpThread.h"
#include "GClipBoard.h"
#include "GDropFiles.h"
#include "GSubProcess.h"

extern const char *Untitled;

#define IDM_INSERT					100
#define IDM_NEW_FOLDER				101
#define IDM_RENAME					102
#define IDM_DELETE					103
#define IDM_SETTINGS				104
#define IDM_INSERT_DEP				105
#define IDM_SORT_CHILDREN			106
#define IDM_PROPERTIES				107
#define IDM_BROWSE_FOLDER			108
#define IDM_OPEN_TERM				109
#define IDM_IMPORT_FOLDER			110
#define IDM_WEB_FOLDER				111
#define IDM_INSERT_FTP				113

#ifdef WIN32
#define LGI_STATIC_LIBRARY_EXT		"lib"
#else
#define LGI_STATIC_LIBRARY_EXT		"a"
#endif

const char *PlatformNames[] =
{
	"Windows",
	"Linux",
	"Mac",
	"Haiku",
	0
};

int PlatformCtrlId[] =
{
	IDC_WIN32,
	IDC_LINUX,
	IDC_MAC,
	IDC_HAIKU,
	0
};

#define ForAllProjectNodes(Var) \
	for (ProjectNode *Var=dynamic_cast<ProjectNode*>(GetChild()); \
		Var; \
		Var=dynamic_cast<ProjectNode*>(Var->GetNext()))

enum NodeType
{
	NodeNone,
	NodeDir,
	NodeSrc,
	NodeHeader,
	NodeDependancy,
	NodeResources,
	NodeGraphic,
	NodeWeb,
	NodeText,
};

char SourcePatterns[] = "*.c;*.h;*.cpp;*.java;*.d;*.php;*.html;*.css";

char *ToUnixPath(char *s)
{
	if (s)
	{
		char *c;
		while ((c = strchr(s, '\\')))
		{
			*c = '/';
		}
	}
	return s;
}

GAutoString ToNativePath(const char *s)
{
	GAutoString a(NewStr(s));
	if (a)
	{
		if (strnicmp(a, "ftp://", 6))
		{
			for (char *c = a; *c; c++)
			{
				#ifdef WIN32
				if (*c == '/')
					*c = '\\';
				#else
				if (*c == '\\')
					*c = '/';
				#endif
			}
		}
	}
	return a;
}

//////////////////////////////////////////////////////////////////////////////////
class WebFldDlg : public GDialog
{
public:
	char *Name;
	GAutoString Ftp;
	char *Www;

	WebFldDlg(GViewI *p, char *name, char *ftp, char *www)
	{
		Name = 0;
		Www = 0;
		SetParent(p);
		LoadFromResource(IDD_WEB_FOLDER);

		if (ftp)
		{
			GUri u(ftp);
			SetCtrlName(IDC_HOST, u.Host);
			SetCtrlName(IDC_USERNAME, u.User);
			SetCtrlName(IDC_PASSWORD, u.Pass);
			SetCtrlName(IDC_PATH, u.Path);
		}

		SetCtrlName(IDC_NAME, name);
		SetCtrlName(IDC_WWW, www);
		MoveToCenter();
	}

	~WebFldDlg()
	{
		DeleteArray(Name);
		DeleteArray(Www);
	}

	int OnNotify(GViewI *v, int f)
	{
		switch (v->GetId())
		{
			case IDOK:
			{
				GUri u;
				u.Host = NewStr(GetCtrlName(IDC_HOST));
				u.User = NewStr(GetCtrlName(IDC_USERNAME));
				u.Pass = NewStr(GetCtrlName(IDC_PASSWORD));
				u.Path = NewStr(GetCtrlName(IDC_PATH));
				u.Protocol = NewStr("ftp");
				Ftp = u.GetUri();
				Www = NewStr(GetCtrlName(IDC_WWW));
				Name = NewStr(GetCtrlName(IDC_NAME));
				EndModal(1);
				break;
			}
			case IDCANCEL:
			{
				EndModal(0);
				break;
			}
		}

		return 0;
	}
};

class FtpFile : public GListItem
{
	IFtpEntry *e;
	GListItemCheckBox *v;
	GAutoString Uri;
	
public:
	FtpFile(IFtpEntry *entry, char *uri)
	{
		Uri.Reset(NewStr(uri));
		e = entry;
		v = new GListItemCheckBox(this, 0);
	}

	char *GetText(int c)
	{
		static char buf[256];
		switch (c)
		{
			case 1:
			{
				return e->Name;
			}
			case 2:
			{
				LgiFormatSize(buf, sizeof(buf), e->Size);
				return buf;
			}
			case 3:
			{
				e->Date.Get(buf, sizeof(buf));
				return buf;
			}
		}

		return 0;
	}

	char *DetachUri()
	{
		if (v->Value())
		{
			return Uri.Release();
		}

		return 0;
	}
};

class AddFtpFile : public GDialog, public FtpCallback
{
	GUri *Base;
	GList *Files;
	GList *Log;
	FtpThread *Thread;

public:
	GArray<char*> Uris;

	AddFtpFile(GViewI *p, char *ftp)
	{
		SetParent(p);
		Base = new GUri(ftp);
		Thread = 0;
		Files = Log = 0;
		if (LoadFromResource(IDD_FTP_FILE))
		{
			MoveToCenter();
			if (GetViewById(IDC_FILES, Files) &&
				GetViewById(IDC_LOG, Log))
			{
				FtpThread *t = GetFtpThread();
				if (t)
				{
					FtpCmd *cmd = new FtpCmd(FtpList, this);
					if (cmd)
					{
						cmd->Watch = Log;
						cmd->Uri = NewStr(ftp);
						t->Post(cmd);
						// Thread = new FtpThread(ftp, Files, Log);
					}
				}
			}
		}
	}

	~AddFtpFile()
	{
		Uris.DeleteArrays();
	}

	void OnCmdComplete(FtpCmd *Cmd)
	{
		if (Cmd && Base)
		{
			for (IFtpEntry *e=Cmd->Dir.First(); e; e=Cmd->Dir.Next())
			{
				if (e->Name && !e->IsDir())
				{
					GUri fu(Cmd->Uri);
					char path[256];
					if (Base->Path)
						sprintf(path, "%s/%s", Base->Path, e->Name.Get());
					else
						sprintf(path, "/%s", e->Name.Get());
					DeleteArray(fu.Path);
					fu.Path = NewStr(path);
					GAutoString Uri = fu.GetUri();

					Files->Insert(new FtpFile(e, Uri));
				}
			}

			Cmd->Dir.Empty();
		}
	}

	int OnNotify(GViewI *c, int f)
	{
		switch (c->GetId())
		{
			case IDOK:
			{
				List<GListItem> a;
				if (Files)
				{
					Files->GetAll(a);
					for (GListItem *i=a.First(); i; i=a.Next())
					{
						FtpFile *f=dynamic_cast<FtpFile*>(i);
						if (!f) break;
						char *u = f->DetachUri();
						if (u)
							Uris.Add(u);
					}
				}
				EndModal(1);
				break;
			}
			case IDCANCEL:
			{
				EndModal(0);
				break;
			}
		}
		return 0;
	}
};

//////////////////////////////////////////////////////////////////////////////////
class FileProps : public GDialog
{
public:
	NodeType Type;
	int Platforms;

	FileProps(GView *p, char *m, NodeType t, int plat)
	{
		Platforms = plat;
		Type = t;
		SetParent(p);
		if (LoadFromResource(IDD_FILE_PROPS))
		{
			MoveToCenter();
			SetCtrlName(IDC_MSG, m);
			
			GCombo *c;
			if (GetViewById(IDC_TYPE, c))
			{
				c->Insert("None");
				c->Insert("Folder");
				c->Insert("Source");
				c->Insert("Header");
				c->Insert("Dependency");
				c->Insert("Resource");
				c->Insert("Graphic");
				c->Insert("Text");
				c->Value(Type);
			}
			else LgiTrace("%s:%i - Failed to get Type combo.\n", _FL);

			for (int i=0; PlatformNames[i]; i++)
			{
				SetCtrlValue(PlatformCtrlId[i], ((1 << i) & Platforms) != 0);
			}
		}
	}
	
	int OnNotify(GViewI *c, int f)
	{
		switch (c->GetId())
		{
			case IDOK:
			{
				int t = GetCtrlValue(IDC_TYPE);
				if (t >= NodeSrc)
				{
					Type = (NodeType) t;
				}

				Platforms = 0;
				for (int i=0; PlatformNames[i]; i++)
				{
					if (GetCtrlValue(PlatformCtrlId[i]))
					{
						Platforms |= 1 << i;
					}
				}
				
				// fall thru
			}
			case IDCANCEL:
			case IDC_COPY_PATH:
			{
				EndModal(c->GetId());
				break;
			}
		}
		
		return 0;
	}
};

//////////////////////////////////////////////////////////////////////////////////
class ProjectNode;

/*
class ProjectSettings
{
public:
	char *MakeFile;
	char *Exe;
	char *ExeArgs;
	char *DefineSym;
	int TargetType;
	char *TargetName;
	char *IncludePaths;
	char *LibPaths;
	char *Libs;
	char *CommentFile;
	char *CommentFunc;
	char *MakefileRules;
	int TabSize;
	int IndentSize;
	int UseTabs;
	int ShowWhitespace;
	int Compiler;

	ProjectSettings()
	{
		MakeFile = 0;
		Exe = 0;
		ExeArgs = 0;
		TargetType = 0;
		TargetName = 0;
		IncludePaths = 0;
		LibPaths = 0;
		Libs = 0;
		CommentFile = 0;
		CommentFunc = 0;
		TabSize = 8;
		IndentSize = 4;
		UseTabs = 0;
		ShowWhitespace = 0;
		DefineSym = 0;
		MakefileRules = 0;
		Compiler = 0;
	}
	
	~ProjectSettings()
	{
		DeleteArray(MakeFile);
		DeleteArray(Exe);
		DeleteArray(ExeArgs);
		DeleteArray(TargetName);
		DeleteArray(IncludePaths);
		DeleteArray(LibPaths);
		DeleteArray(Libs);
		DeleteArray(CommentFile);
		DeleteArray(CommentFunc);
		DeleteArray(DefineSym);
		DeleteArray(MakefileRules);
	}
	
	ProjectSettings &operator =(ProjectSettings &p)
	{
		#define AssignStr(n) { char *t = NewStr(p.n); DeleteArray(n); n = t; }
		
		AssignStr(MakeFile);
		AssignStr(Exe);
		AssignStr(ExeArgs);
		AssignStr(DefineSym);
		TargetType = p.TargetType;
		AssignStr(TargetName);
		AssignStr(IncludePaths);
		AssignStr(LibPaths);
		AssignStr(Libs);
		AssignStr(CommentFile);
		AssignStr(CommentFunc);
		AssignStr(MakefileRules);
		TabSize = p.TabSize;
		IndentSize = p.IndentSize;
		UseTabs = p.UseTabs;
		ShowWhitespace = p.ShowWhitespace;
		Compiler = p.Compiler;

		return *this;
	}
};
*/

class BuildThread : public GThread, public GStream
{
	IdeProject *Proj;
	GAutoString Makefile;
	GAutoString Args;
	GAutoPtr<GSubProcess> SubProc;

	enum CompilerType
	{
		DefaultCompiler,
		VisualStudio,
		MingW,
		Gcc
	}	Compiler;

public:
	BuildThread(IdeProject *proj, char *mf, const char *args = 0);
	~BuildThread();
	
	int Write(const void *Buffer, int Size, int Flags = 0);
	char *FindExe();
	GAutoString WinToMingWPath(const char *path);
	int Main();
};

class IdeProjectPrivate
{
public:
	AppWnd *App;
	IdeProject *Project;
	bool Dirty;
	GAutoString FileName;
	IdeProject *ParentProject;
	IdeProjectSettings Settings;
	GAutoPtr<BuildThread> Build;

	IdeProjectPrivate(AppWnd *a, IdeProject *project) :
		Project(project),
		Settings(project)
	{
		App = a;
		Dirty = false;
		ParentProject = 0;
	}

	void CollectAllFiles(GTreeNode *Base, GArray<ProjectNode*> &Files, bool SubProjects, int Platform);
};

/////////////////////////////////////////////////////////////////////////////////////
NodeSource::~NodeSource()
{
	if (nView)
	{
		nView->nSrc = 0;
	}
}

NodeView::~NodeView()
{
	if (nSrc)
	{
		nSrc->nView = 0;
	}
}

class ProjectNode : public IdeCommon, public GDragDropSource, public FtpCallback, public NodeSource
{
	NodeType Type;
	int Platforms;
	char *File;
	char *LocalCache;
	char *Name;
	IdeProject *Dep;
	bool IgnoreExpand;

	void OpenLocalCache(IdeDoc *&Doc)
	{
		if (LocalCache)
		{
			Doc = Project->GetApp()->OpenFile(LocalCache, this);
			if (Doc)
			{
				if (Doc && Doc->GetEdit())
				{
					Doc->SetProject(Project);
					
					int i = Project->d->Settings.GetInt(ProjEditorIndentSize);
					Doc->GetEdit()->SetIndentSize(i > 0 ? i : 4);
					i = Project->d->Settings.GetInt(ProjEditorTabSize);
					Doc->GetEdit()->SetTabSize(i > 0 ? i : 4);
					Doc->GetEdit()->SetHardTabs(Project->d->Settings.GetInt(ProjEditorUseHardTabs));
					Doc->GetEdit()->SetShowWhiteSpace(Project->d->Settings.GetInt(ProjEditorShowWhiteSpace));
					Doc->GetEdit()->Invalidate();
				}
			}
			else
			{
				LgiMsg(Tree, "Couldn't open file '%s'", AppName, MB_OK, LocalCache);
			}
		}
	}

	void OnCmdComplete(FtpCmd *Cmd)
	{
		if (!Cmd)
			return;

		if (Cmd->Status && Cmd->File)
		{
			if (Cmd->Cmd == FtpRead)
			{
				DeleteArray(LocalCache);
				LocalCache = NewStr(Cmd->File);

				IdeDoc *Doc;
				OpenLocalCache(Doc);
			}
			else if (Cmd->Cmd == FtpWrite)
			{
				Cmd->View->OnSaveComplete(Cmd->Status);
			}
		}
	}

public:
	ProjectNode(IdeProject *p) : IdeCommon(p)
	{
		Platforms = PLATFORM_ALL;
		Type = NodeNone;
		File = 0;
		Name = 0;
		IgnoreExpand = false;
		Dep = 0;
		LocalCache = 0;
		Tag = NewStr("Node");
	}

	~ProjectNode()
	{
		if (Dep)
		{
			DeleteObj(Dep);
		}

		DeleteArray(File);
		DeleteArray(Name);
		DeleteArray(LocalCache);
	}

	int GetPlatforms()
	{
		return Platforms;
	}

	char *GetLocalCache()
	{
		return LocalCache;
	}
	
	bool Load(GDocView *Edit, NodeView *Callback)
	{
		bool Status = false;

		if (IsWeb())
		{
			if (LocalCache)
			{
				Status = Edit->Open(LocalCache);
			}
			else LgiAssert(!"No LocalCache");
		}
		else
		{
			GAutoString Full = GetFullPath();
			Status = Edit->Open(Full);
		}
		
		return Status;
	}

	bool Save(GDocView *Edit, NodeView *Callback)
	{
		bool Status = false;

		if (IsWeb())
		{
			if (LocalCache)
			{
				if (Edit->Save(LocalCache))
				{
					FtpThread *t = GetFtpThread();
					if (t)
					{
						FtpCmd *c = new FtpCmd(FtpWrite, this);
						if (c)
						{
							c->View = Callback;
							c->Watch = Project->GetApp()->GetFtpLog();
							c->Uri = NewStr(File);
							c->File = NewStr(LocalCache);
							t->Post(c);
						}
					}
				}
				else LgiAssert(!"Editbox save failed.");
			}
			else LgiAssert(!"No LocalCache");
		}
		else
		{
			GAutoString f = GetFullPath();
			Status = Edit->Save(f);

			if (Callback)
				Callback->OnSaveComplete(Status);
		}
		
		
		return Status;
	}

	bool IsWeb()
	{
		char *Www = GetAttr(OPT_Www);
		char *Ftp = GetAttr(OPT_Ftp);

		if
		(
			Www ||
			Ftp ||
			(File && strnicmp(File, "ftp://", 6) == 0)
		)
			return true;

		return false;
	}
	
	ProjectNode *ChildNode()
	{
		return dynamic_cast<ProjectNode*>(GetChild());
	}
	
	ProjectNode *NextNode()
	{
		return dynamic_cast<ProjectNode*>(GetNext());
	}
	
	void SetClean()
	{
		if (Dep)
		{
			Dep->SetClean();
		}
		
		ForAllProjectNodes(p)
		{
			p->SetClean();
		}
	}
	
	IdeProject *GetDep()
	{
		return Dep;
	}
	
	IdeProject *GetProject()
	{
		return Project;
	}
	
	bool GetFormats(List<char> &Formats)
	{
		Formats.Insert(NewStr(NODE_DROP_FORMAT));
		return true;
	}

	bool GetData(GVariant *Data, char *Format)
	{
		if (Format && stricmp(Format, NODE_DROP_FORMAT) == 0)
		{
			void *t = this;
			Data->SetBinary(sizeof(this), (void*) &t);
			return true;
		}
		
		return false;
	}
	
	bool OnBeginDrag(GMouse &m)
	{
		return Drag(Tree, DROPEFFECT_MOVE);
	}
	
	char *GetFileName()
	{
		return File;
	}

	void SetFileName(char *f)
	{
		char Rel[MAX_PATH];
		DeleteArray(File);
		if (Project->RelativePath(Rel, f))
		{
			File = NewStr(Rel);
		}
		else
		{
			File = NewStr(f);
		}

		if (File)
		{
			char MimeType[256];
			
			GAutoString FullPath = GetFullPath();
			if (FullPath)
			{
				if (LgiGetFileMimeType(FullPath, MimeType, sizeof(MimeType)) &&
					strnicmp(MimeType, "image/", 6) == 0)
				{
					Type = NodeGraphic;
				}
			}
			
			if (!Type)
			{
				char *Ext = LgiGetExtension(File);

				if ((Ext && stricmp(Ext, "h") == 0) ||
					stristr(File, "makefile") != 0)
				{
					Type = NodeHeader;
				}
				else if (Ext)
				{
					if (stricmp(Ext, "lr8") == 0)
					{
						Type = NodeResources;
					}
					else if (stricmp(Ext, "cpp") == 0 ||
							stricmp(Ext, "c") == 0 ||
							stricmp(Ext, "h") == 0)
					{
						Type = NodeSrc;
					}
					else if (stricmp(Ext, "php") == 0 ||
							 stricmp(Ext, "asp") == 0 ||
							 stricmp(Ext, "html") == 0 ||
							 stricmp(Ext, "htm") == 0 ||
							 stricmp(Ext, "css") == 0)
					{
						Type = NodeWeb;
					}
					else if (stricmp(Ext, "txt") == 0)
					{
						Type = NodeText;
					}
				}
			}
		}
		
		Project->SetDirty();
	}

	char *GetName()
	{
		return Name;
	}

	void SetName(const char *f)
	{
		DeleteArray(Name);
		Name = NewStr(f);
		Type = NodeDir;
	}

	NodeType GetType()
	{
		return Type;
	}

	void SetType(NodeType t)
	{
		Type = t;
	}

	int GetImage(int f)
	{
		if (IsWeb())
		{
			return File ? ICON_SOURCE : ICON_WEB;
		}

		switch (Type)
		{
			default:
				break;
			case NodeSrc:
				return ICON_SOURCE;
			case NodeNone:
			case NodeHeader:
			case NodeWeb:
				return ICON_HEADER;
			case NodeDependancy:
				return ICON_DEPENDANCY;
			case NodeGraphic:
				return ICON_GRAPHIC;
			case NodeResources:
				return ICON_RESOURCE;
		}

		return ICON_FOLDER;
	}

	char *GetText(int c)
	{
		if (File)
		{
			char *d = 0;

			if (IsWeb())
			{
				d = File ? strrchr(File, '/') : 0;
			}
			else
			{
				#ifdef WIN32
				char Other = '/';
				#else
				char Other = '\\';
				#endif
				char *s;
				while ((s = strchr(File, Other)))
				{
					*s = DIR_CHAR;
				}
				
				d = strrchr(File, DIR_CHAR);
			}
			
			if (d) return d + 1;
			else return File;
		}

		return Name ? Name : (char*)Untitled;
	}

	void OnExpand(bool b)
	{
		if (!IgnoreExpand)
		{
			Project->SetDirty();
		}
	}
	
	bool Serialize()
	{
		SerializeAttr("File", File);
		SerializeAttr("Name", Name);
		SerializeAttr("Type", (int&)Type);
		SerializeAttr("Platforms", (int&)Platforms);

		char *Ext = LgiGetExtension(File);

		if (Ext)
		{
			if (stricmp(Ext, "cpp") == 0 ||
				stricmp(Ext, "c") == 0)
			{
				Type = NodeSrc;
			}
			else if (stricmp(Ext, "h") == 0)
			{
				Type = NodeHeader;
			}
			else if (stricmp(Ext, "lr8") == 0)
			{
				Type = NodeResources;
			}			
		}

		if (Type == NodeDir)
		{
			int Open = Expanded();
			SerializeAttr("Open", Open);
			if (!Write)
			{
				IgnoreExpand = true;
				Expanded(Open);
				IgnoreExpand = false;
			}
		}
		else if (Type == NodeDependancy)
		{
			if (!Write)
			{
				GAutoString Full = GetFullPath();				
				Dep = Project->GetApp()->OpenProject(Full, Project, false, true);
			}
		}
		else
		{
			if (!Write)
			{
				// Check that file exists.
				GAutoString p = GetFullPath();
				if (p)
				{
					if (FileExists(p))
					{
						if (!LgiIsRelativePath(File))
						{
							// Try and fix up any non-relative paths that have crept in...
							char Rel[MAX_PATH];
							if (Project->RelativePath(Rel, File))
							{
								DeleteArray(File);
								File = NewStr(Rel);
								Project->SetDirty();
							}
						}
					}
					else
					{
						// File doesn't exist... has it moved???
						GAutoString Path = Project->GetBasePath();
						if (Path)
						{
							// Find the file.
							char *d = strrchr(p, DIR_CHAR);
							GArray<char*> Files;
							GArray<const char*> Ext;
							Ext.Add(d ? d + 1 : p);
							if (LgiRecursiveFileSearch(Path, &Ext, &Files))
							{
								if (Files.Length())
								{
									GStringPipe Buf;
									Buf.Print(	"The file:\n"
												"\n"
												"\t%s\n"
												"\n"
												"doesn't exist. Is this the right file:\n"
												"\n"
												"\t%s",
												p.Get(),
												Files[0]);
									char *Msg = Buf.NewStr();
									if (Msg)
									{
										GAlert a(Project->GetApp(), "Missing File", Msg, "Yes", "No", "Browse...");
										switch (a.DoModal())
										{
											case 1: // Yes
											{
												SetFileName(Files[0]);
												break;
											}
											case 2: // No
											{
												break;
											}
											case 3: // Browse
											{
												GFileSelect s;
												s.Parent(Project->GetApp());
												s.Type("Code", SourcePatterns);
												if (s.Open())
												{
													SetFileName(s.Name());
												}
												break;
											}
										}
										
										DeleteArray(Msg);
									}
								}
								else
								{
									GStringPipe Buf;
									Buf.Print(	"The file:\n"
												"\n"
												"\t%s\n"
												"\n"
												"doesn't exist.",
												p.Get());
									char *Msg = Buf.NewStr();
									if (Msg)
									{
										GAlert a(Project->GetApp(), "Missing File", Msg, "Skip", "Delete", "Browse...");
										switch (a.DoModal())
										{
											case 1: // Skip
											{
												break;
											}
											case 2: // Delete
											{
												Project->SetDirty();
												delete this;
												return false;
												break;
											}
											case 3: // Browse
											{
												GFileSelect s;
												s.Parent(Project->GetApp());
												s.Type("Code", SourcePatterns);
												if (s.Open())
												{
													SetFileName(s.Name());
												}
												break;
											}
										}
										
										DeleteArray(Msg);
									}
								}
							}
						}
						else
						{
							LgiTrace("%s:%i - Project::GetBasePath failed.\n", _FL);
						}
					}
				}
			}
		}

		return true;
	}
	
	GAutoString GetFullPath()
	{
		GAutoString FullPath;
		
		if (LgiIsRelativePath(File))
		{
			// Relative path
			GAutoString Path = Project->GetBasePath();
			if (Path)
			{
				char p[MAX_PATH];
				LgiMakePath(p, sizeof(p), Path, File);
				FullPath.Reset(NewStr(p));
			}
		}
		else
		{
			// Absolute path
			FullPath.Reset(NewStr(File));
		}
		
		return FullPath;
	}

	IdeDoc *Open()
	{
		static bool Processing = false;
		IdeDoc *Doc = 0;

		if (Processing)
		{
			LgiAssert(!"Not recursive");
		}

		if (!Processing)
		{
			Processing = true;

			if (IsWeb())
			{
				if (File)
				{
					if (LocalCache)
					{
						OpenLocalCache(Doc);
					}
					else
					{
						FtpThread *t = GetFtpThread();
						if (t)
						{
							FtpCmd *c = new FtpCmd(FtpRead, this);
							if (c)
							{
								c->Watch = Project->GetApp()->GetFtpLog();
								c->Uri = NewStr(File);
								t->Post(c);
							}
						}
					}
				}
			}
			else
			{		
				switch (Type)
				{
					case NodeDir:
					{
						Expanded(true);
						break;
					}
					case NodeResources:
					{
						GAutoString FullPath = GetFullPath();
						if (FullPath)
						{
							char Exe[256];
							LgiGetExePath(Exe, sizeof(Exe));
							LgiTrimDir(Exe);
							#if defined WIN32
							LgiMakePath(Exe, sizeof(Exe), Exe, "Debug\\LgiRes.exe");
							#elif defined LINUX
							LgiMakePath(Exe, sizeof(Exe), Exe, "LgiRes/lgires");
							#endif
							
							if (FileExists(Exe))
							{
								LgiExecute(Exe, FullPath);
							}
						}
						else
						{
							LgiMsg(Tree, "No Path", AppName);
						}
						break;
					}
					case NodeGraphic:
					{
						GAutoString FullPath = GetFullPath();
						if (FullPath)
						{
							LgiExecute(FullPath);
						}
						else
						{
							LgiMsg(Tree, "No Path", AppName);
						}
						break;
					}
					default:
					{
						GAutoString FullPath = GetFullPath();
						if (FullPath)
						{
							Doc = Project->GetApp()->FindOpenFile(FullPath);
							if (!Doc)
							{
								Doc = Project->GetApp()->NewDocWnd(0, this);
								if (Doc && Doc->GetEdit())
								{
									if (Doc->GetEdit()->Open(FullPath))
									{
										Doc->SetProject(Project);
										
										int i = Project->d->Settings.GetInt(ProjEditorIndentSize);
										Doc->GetEdit()->SetIndentSize(i > 0 ? i : 4);
										i = Project->d->Settings.GetInt(ProjEditorTabSize);
										Doc->GetEdit()->SetTabSize(i > 0 ? i : 4);
										Doc->GetEdit()->SetHardTabs(Project->d->Settings.GetInt(ProjEditorUseHardTabs));
										Doc->GetEdit()->SetShowWhiteSpace(Project->d->Settings.GetInt(ProjEditorShowWhiteSpace));

										Doc->GetEdit()->Invalidate();
									}
									else
									{
										LgiMsg(Tree, "Couldn't open file '%s'", AppName, MB_OK, FullPath.Get());
									}
								}
							}
							else
							{
								Doc->Raise();
								Doc->Focus(true);
							}
						}
						else
						{
							LgiMsg(Tree, "No Path", AppName);
						}
						break;
					}
				}
			}

			Processing = false;
		}
		
		return Doc;
	}
	
	void Delete()
	{
		if (nView)
		{
			nView->OnDelete();
		}

		Project->SetDirty();
		GXmlTag::RemoveTag();							
		delete this;
	}
	
	bool OnKey(GKey &k)
	{
		if (k.Down() && k.IsChar)
		{
			if (k.vkey == VK_RETURN)
			{
				Open();
				return true;
			}
			else if (k.vkey == VK_DELETE)
			{
				GTreeItem *s = GetNext();
				if (s || (s = GetParent()))
				{
					s->Select(true);
				}
				
				Delete();
				return true;
			}
		}
		
		return false;
	}

	void OnMouseClick(GMouse &m)
	{
		GTreeItem::OnMouseClick(m);
		
		if (m.IsContextMenu())
		{
			GSubMenu Sub;

			Select(true);

			if (Type == NodeDir)
			{
				if (IsWeb())
				{
					Sub.AppendItem("Insert FTP File", IDM_INSERT_FTP, true);
				}
				else
				{
					Sub.AppendItem("Insert File", IDM_INSERT, true);
				}
				
				Sub.AppendItem("New Folder", IDM_NEW_FOLDER, true);
				Sub.AppendItem("Import Folder", IDM_IMPORT_FOLDER, true);
				Sub.AppendSeparator();

				if (!IsWeb())
				{
					Sub.AppendItem("Rename", IDM_RENAME, true);
				}
			}
			Sub.AppendItem("Remove", IDM_DELETE, true);
			Sub.AppendItem("Sort", IDM_SORT_CHILDREN, true);
			Sub.AppendSeparator();
			Sub.AppendItem("Browse Folder", IDM_BROWSE_FOLDER, ValidStr(File));
			Sub.AppendItem("Open Terminal", IDM_OPEN_TERM, ValidStr(File));
			Sub.AppendItem("Properties", IDM_PROPERTIES, true);

			m.ToScreen();
			GdcPt2 c = _ScrollPos();
			m.x -= c.x;
			m.y -= c.y;
			switch (Sub.Float(Tree, m.x, m.y))
			{
				case IDM_INSERT_FTP:
				{
					AddFtpFile Add(Tree, GetAttr(OPT_Ftp));
					if (Add.DoModal())
					{
						for (int i=0; i<Add.Uris.Length(); i++)
						{
							ProjectNode *New = new ProjectNode(Project);
							if (New)
							{
								New->SetFileName(Add.Uris[i]);
								InsertTag(New);
								SortChildren();
								Project->SetDirty();
							}
						}
					}
					break;
				}
				case IDM_INSERT:
				{
					GFileSelect s;
					s.Parent(Tree);
					s.Type("Source", SourcePatterns);
					s.Type("Makefiles", "*makefile");
					s.Type("All Files", LGI_ALL_FILES);
					s.MultiSelect(true);

					GAutoString Dir = Project->GetBasePath();
					if (Dir)
					{
						s.InitialDir(Dir);
					}

					if (s.Open())
					{
						for (int i=0; i<s.Length(); i++)
						{
							if (!Project->InProject(s[i], false))
							{
								ProjectNode *New = new ProjectNode(Project);
								if (New)
								{
									New->SetFileName(s[i]);
									InsertTag(New);
									SortChildren();
									Project->SetDirty();
								}
							}
							else
							{
								LgiMsg(Tree, "'%s' is already in the project.\n", AppName, MB_OK, s[i]);
							}
						}
					}
					break;
				}
				case IDM_IMPORT_FOLDER:
				{
					GFileSelect s;
					s.Parent(Tree);

					GAutoString Dir = Project->GetBasePath();
					if (Dir)
					{
						s.InitialDir(Dir);
					}

					if (s.OpenFolder())
					{
						GArray<char*> Files;
						GArray<const char*> Ext;
						GToken e(SourcePatterns, ";");
						for (int i=0; i<e.Length(); i++)
						{
							Ext.Add(e[i]);
						}
						
						if (LgiRecursiveFileSearch(s.Name(), &Ext, &Files))
						{
							int Start = strlen(s.Name()) + 1;
							for (int i=0; i<Files.Length(); i++)
							{
								char *f = Files[i];
								GToken p(f + Start, DIR_STR);
								ProjectNode *Insert = this;
								
								// Find sub nodes, and drill into directory heirarchy,
								// creating the nodes if needed.
								for (int i=0; Insert && i<p.Length()-1; i++)
								{
									// Find existing node...
									bool Found = false;
									for (ProjectNode *c=Insert->ChildNode(); c; c=c->NextNode())
									{
										if (c->GetType() == NodeDir &&
											c->GetName() &&
											stricmp(c->GetName(), p[i]) == 0)
										{
											Insert = c;
											Found = true;
											break;
										}
									}
									
									if (!Found)
									{
										// Create the node
										IdeCommon *Com = Insert->GetSubFolder(Project, p[i], true);												
										Insert = dynamic_cast<ProjectNode*>(Com);
										LgiAssert(Insert);
									}
								}
								
								// Insert the file into the tree...
								if (Insert)
								{
									ProjectNode *New = new ProjectNode(Project);
									if (New)
									{
										New->SetFileName(f);
										Insert->InsertTag(New);
										Insert->SortChildren();
										Project->SetDirty();
									}
								}
							}
							
							Files.DeleteArrays();
						}														
					}
					break;
				}
				case IDM_SORT_CHILDREN:
				{
					SortChildren();
					Project->SetDirty();
					break;
				}
				case IDM_NEW_FOLDER:
				{
					GInput Name(Tree, "", "Name:", AppName);
					if (Name.DoModal())
					{
						GetSubFolder(Project, Name.Str, true);
					}
					break;
				}
				case IDM_RENAME:
				{
					GInput Name(Tree, "", "Name:", AppName);
					if (Name.DoModal())
					{
						SetName(Name.Str);
						Project->SetDirty();
						Update();
					}
					break;
				}
				case IDM_DELETE:
				{
					Delete();
					return;
					break;
				}
				case IDM_BROWSE_FOLDER:
				{
					GAutoString Path = GetFullPath();
					if (Path)
						LgiBrowseToFile(Path);
					break;
				}
				case IDM_OPEN_TERM:
				{
					if (Type == NodeDir)
					{
					}
					else
					{
						GAutoString Path = GetFullPath();
						if (Path)
						{
							LgiTrimDir(Path);
							
							#if defined LINUX
							
							char *Term = 0;
							char *Format = 0;
							switch (LgiGetWindowManager())
							{
								case WM_Kde:
									Term = "konsole";
									Format = "--workdir ";
									break;
								case WM_Gnome:
									Term = "gnome-terminal";
									Format = "--working-directory=";
									break;
							}
							
							if (Term)
							{
								char s[256];
								strcpy_s(s, sizeof(s), Format);
								
								char *o = s + strlen(s), *i = Path;
								*o++ = '\"';
								while (*i)
								{
									if (*i == ' ')
									{
										*o++ = '\\';
									}
									*o++ = *i++;
								}
								*o++ = '\"';
								*o++ = 0;
								LgiExecute(Term, s);
							}
							
							#elif defined WIN32
							
							LgiExecute("cmd", 0, Path);
							
							#endif
						}
					}
					break;
				}
				case IDM_PROPERTIES:
				{
					OnProperties();
					break;
				}
			}
		}
		else if (m.Left())
		{
			if (Type != NodeDir && m.Double())
			{
				if
				(
					(
						IsWeb()
						||
						Type == NodeSrc
						||
						Type == NodeHeader
						||
						Type == NodeResources
						||
						Type == NodeGraphic
						||
						Type == NodeWeb
						||
						Type == NodeText
					)
					&&
					ValidStr(File)
				)
				{
					Open();
				}
				else
				{
					LgiAssert(!"Unknown file type");
				}
			}
		}
	}
	
	ProjectNode *FindFile(const char *In, char **Full)
	{
		if (File)
		{
			bool Match = false;

			if (IsWeb())
			{
				Match = File ? stricmp(In, File) == 0 : false;
			}
			else if (strchr(In, DIR_CHAR))
			{
				// Match partial or full path
				char Full[MAX_PATH] = "";
				
				if (LgiIsRelativePath(File))
				{
					GAutoString Base = Project->GetBasePath();
					if (Base)
						LgiMakePath(Full, sizeof(Full), Base, File);
					else
						LgiTrace("%s,%i - Couldn't get full IDoc path.\n", __FILE__, __LINE__);
				}
				else
				{
					strcpy_s(Full, sizeof(Full), File);
				}
				
				GString MyPath(Full);
				GString::Array MyArr = MyPath.Split(DIR_STR);
				GString InPath(In);
				GString::Array InArr = InPath.Split(DIR_STR);
				int Common = min(MyArr.Length(), InArr.Length());
				Match = true;
				for (int i = 0; i < Common; i++)
				{
					char *a = MyArr[MyArr.Length()-(i+1)];
					char *b = InArr[InArr.Length()-(i+1)];
					if (_stricmp(a, b))
					{
						Match = false;
						break;
					}
				}
			}
			else
			{
				// Match file name only
				char *Dir = strrchr(File, DIR_CHAR);
				if (Dir)
				{
					Match = stricmp(Dir + 1, In) == 0;
				}
			}
			
			if (Match)
			{
				if (Full)
				{
					if (File[0] == '.')
					{
						GAutoString Base = Project->GetBasePath();
						if (Base)
						{
							char f[256];
							LgiMakePath(f, sizeof(f), Base, File);
							*Full = NewStr(f);
						}
					}
					else
					{
						*Full = NewStr(File);
					}
				}
				
				return this;
			}
		}
		
		ForAllProjectNodes(c)
		{
			ProjectNode *n = c->FindFile(In, Full);
			if (n)
			{
				return n;
			}
		}
		
		return 0;
	}
	
	void OnProperties()
	{
		if (IsWeb())
		{
			bool IsFolder = File == 0;

			WebFldDlg Dlg(Tree, Name, IsFolder ? GetAttr(OPT_Ftp) : File, GetAttr(OPT_Www));
			if (Dlg.DoModal())
			{
				if (IsFolder)
				{
					SetName(Dlg.Name);
					SetAttr(OPT_Ftp, Dlg.Ftp);
					SetAttr(OPT_Www, Dlg.Www);
				}
				else
				{
					DeleteArray(File);
					File = NewStr(Dlg.Ftp);
				}

				Project->SetDirty();
				Update();
			}
		}
		else if (Type == NodeDir)
		{
		}
		else if (Type == NodeDependancy)
		{
			GAutoString Path = GetFullPath();
			LgiMsg(Tree, "Path: %s", AppName, MB_OK, Path.Get());
		}
		else
		{
			GAutoString Path = GetFullPath();
			if (Path)
			{
				char Size[32];
				int64 FSize = LgiFileSize(Path);
				LgiFormatSize(Size, sizeof(Size), FSize);
				char Msg[512];
				sprintf(Msg, "Source Code:\n\n\t%s\n\nSize: %s (%i bytes)", Path.Get(), Size, (int32)FSize);
			
				FileProps Dlg(Tree, Msg, Type, Platforms);
				switch (Dlg.DoModal())
				{
					case IDOK:
					{
						if (Type != Dlg.Type)
						{
							Type = Dlg.Type;
							Project->SetDirty();
						}
						if (Platforms != Dlg.Platforms)
						{
							Platforms = Dlg.Platforms;
							Project->SetDirty();
						}
						
						Update();
						break;
					}
					case IDC_COPY_PATH:
					{
						GClipBoard Clip(Tree);
						Clip.Text(Path);
						break;
					}
				}
			}
		}
	}
};

//////////////////////////////////////////////////////////////////////////////////
int NodeSort(GTreeItem *a, GTreeItem *b, int d)
{
	ProjectNode *A = dynamic_cast<ProjectNode*>(a);
	ProjectNode *B = dynamic_cast<ProjectNode*>(b);
	if (A && B)
	{
		if
		(
			(A->GetType() == NodeDir)
			^
			(B->GetType() == NodeDir)
		)
		{
			return A->GetType() == NodeDir ? -1 : 1;
		}
		else
		{
			char *Sa = a->GetText(0);
			char *Sb = b->GetText(0);
			if (Sa && Sb)
			{
				return stricmp(Sa, Sb);
			}
		}
	}

	return 0;
}

int XmlSort(GXmlTag *a, GXmlTag *b, int d)
{
	GTreeItem *A = dynamic_cast<GTreeItem*>(a);
	GTreeItem *B = dynamic_cast<GTreeItem*>(b);
	return NodeSort(A, B, d);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
IdeCommon::IdeCommon(IdeProject *p)
{
	Project = p;
}

IdeCommon::~IdeCommon()
{
	Remove();
}

bool IdeCommon::OnOpen(GXmlTag *Src)
{
	Copy(*Src);
	Write = false;
	if (!Serialize())
		return false;

	List<GXmlTag>::I it = Src->Children.Start();
	for (GXmlTag *c = *it; c; c = *++it)
	{
		if (c->IsTag("Node"))
		{
			ProjectNode *pn = new ProjectNode(Project);
			if (pn)
			{
				if (pn->OnOpen(c))
					InsertTag(pn);
			}
		}
	}
	
	return true;
}

void IdeCommon::CollectAllSubProjects(List<IdeProject> &c)
{
	ForAllProjectNodes(p)
	{
		if (p->GetType() == NodeDependancy)
		{
			if (p->GetDep())
				c.Insert(p->GetDep());
		}
		
		p->CollectAllSubProjects(c);
	}
}

IdePlatform GetCurrentPlatform()
{
	#if defined(WIN32)
	return PlatformWin32;
	#elif defined(LINUX)
	return PlatformLinux;
	#elif defined(MAC)
	return PlatformMac;
	#elif defined(BEOS)
	return PlatformHaiku;
	#else
	#error "Not impl."
	#endif
}

void IdeCommon::CollectAllSource(GArray<char*> &c, IdePlatform Platform)
{
	ForAllProjectNodes(p)
	{
		switch (p->GetType())
		{
			case NodeSrc:
			case NodeHeader:
			{
				if (Platform == PlatformCurrent)
					Platform = GetCurrentPlatform();
					
				int Flags = p->GetPlatforms();
				if (Flags & (1 << Platform))
				{
					GAutoString path = p->GetFullPath();
					if (path)
					{
						c.Add(path.Release());
					}
				}
				break;
			}
			default:
			{
				break;
			}
		}
		
		p->CollectAllSource(c, Platform);
	}
}

void IdeCommon::SortChildren()
{
	Items.Sort(NodeSort, 0);
	Children.Sort(XmlSort, 0);
	
	if (Tree)
	{
		_RePour();
		Tree->Invalidate();
	}
}

void IdeCommon::InsertTag(GXmlTag *t)
{
	GXmlTag::InsertTag(t);

	GTreeItem *i = dynamic_cast<GTreeItem*>(t);
	if (i)
	{
		Insert(i);
	}
}

void IdeCommon::RemoveTag()
{
	GXmlTag::RemoveTag();
	Detach();
}

IdeCommon *IdeCommon::GetSubFolder(IdeProject *Project, char *Name, bool Create)
{
	if (Name)
	{
		ForAllProjectNodes(c)
		{
			if (c->GetType() == NodeDir)
			{
				if (c->GetName() && stricmp(c->GetName(), Name) == 0)
				{
					return c;
				}
			}
		}

		ProjectNode *New = new ProjectNode(Project);
		if (New)
		{
			New->SetName(Name);
			InsertTag(New);
			Project->SetDirty();
			return New;
		}
	}

	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
BuildThread::BuildThread(IdeProject *proj, char *mf, const char *args) : GThread("BuildThread")
{
	Proj = proj;
	Makefile.Reset(NewStr(mf));
	Args.Reset(NewStr(args));
	Compiler = DefaultCompiler;

	GAutoString Comp(NewStr(Proj->GetSettings()->GetStr(ProjCompiler)));
	if (Comp)
	{
		// Use the specified compiler...
		if (!stricmp(Comp, "VisualStudio"))
			Compiler = VisualStudio;
		else if (!stricmp(Comp, "MingW"))
			Compiler = MingW;
		else if (!stricmp(Comp, "gcc"))
			Compiler = Gcc;
	}

	if (Compiler == DefaultCompiler)
	{
		// Use default compiler for platform...
		#ifdef WIN32
		Compiler = VisualStudio;
		#else
		Compiler = Gcc;
		#endif
	}
		
	if (Proj->GetApp())
		Proj->GetApp()->PostEvent(M_APPEND_TEXT, 0, 0);

	Run();
}

BuildThread::~BuildThread()
{
	if (SubProc)
		SubProc->Interrupt();

	while (!IsExited())
	{
		LgiSleep(10);
	}
}

int BuildThread::Write(const void *Buffer, int Size, int Flags)
{
	if (Proj->GetApp())
	{
		Proj->GetApp()->PostEvent(M_APPEND_TEXT, (GMessage::Param)NewStr((char*)Buffer, Size), 0);
	}
	return Size;
}

char *BuildThread::FindExe()
{
	char Exe[256] = "";
	GToken p(getenv("PATH"), LGI_PATH_SEPARATOR);
	
	if (Compiler == VisualStudio)
	{
		for (int i=0; i<p.Length(); i++)
		{
			char Path[256];
			LgiMakePath(Path, sizeof(Path), p[i], "msdev.exe");
			if (FileExists(Path))
			{
				return NewStr(Path);
			}
		}
	}
	else
	{
		if (Compiler == MingW)
		{
			// Have a look in the default spot first...
			const char *Def = "C:\\MinGW\\msys\\1.0\\bin\\make.exe";
			if (FileExists(Def))
			{
				return NewStr(Def);
			}				
		}
		
		if (!FileExists(Exe))
		{
			for (int i=0; i<p.Length(); i++)
			{
				LgiMakePath
				(
					Exe,
					sizeof(Exe),
					p[i],
					"make"
					#ifdef WIN32
					".exe"
					#endif
				);
				if (FileExists(Exe))
				{
					return NewStr(Exe);
				}
			}
		}
	}
	
	return 0;
}

GAutoString BuildThread::WinToMingWPath(const char *path)
{
	GToken t(path, "\\");
	GStringPipe a(256);
	for (int i=0; i<t.Length(); i++)
	{
		char *p = t[i];
		char *colon = strchr(p, ':');
		if (colon)
		{
			*colon = 0;
			StrLwr(p);
		}
		a.Print("/%s", p);				
	}
	return GAutoString(a.NewStr());
}

int BuildThread::Main()
{
	const char *Err = 0;
	char ErrBuf[256];

// printf("BuildThread::Main.0\n");
	
	const char *Exe = FindExe();
// printf("BuildThread::Main.1 Exe='%s'\n", Exe);
	if (Exe)
	{
		bool Status = false;
		GAutoString MakePath(NewStr(Makefile));
		GVariant Jobs;
		if (!Proj->GetApp()->GetOptions()->GetValue(OPT_Jobs, Jobs) || Jobs.CastInt32() < 1)
			Jobs = 2;

// printf("BuildThread::Main.2 Jobs=%i\n", Jobs.CastInt32());
		if (Compiler == VisualStudio)
		{
			char a[256];
			sprintf(a, "\"%s\" /make \"All - Win32 Debug\"", Makefile.Get());
			GProcess Make;
			Status = Make.Run(Exe, a, 0, true, 0, this);

			if (!Status)
			{
				Err = "Running make failed";
				LgiTrace("%s,%i - %s.\n", _FL, Err);
			}
		}
		else
		{
			GStringPipe a;
			char InitDir[MAX_PATH];
			LgiMakePath(InitDir, sizeof(InitDir), MakePath, "..");
			
			if (Compiler == MingW)
			{
				char *Dir = strrchr(MakePath, DIR_CHAR);
				#if 1
				a.Print("/C \"%s\"", Exe);
				
				/*	As of MSYS v1.0.18 the support for multiple jobs causes make to hang:
					http://sourceforge.net/p/mingw/bugs/1950/
					http://mingw-users.1079350.n2.nabble.com/MSYS-make-freezes-td7579038.html
					
					Apparently it'll be "fixed" in v1.0.19. We'll see. >:-(
				
				if (Jobs.CastInt32() > 1)
					a.Print(" -j %i", Jobs.CastInt32());
				
				*/
				
				a.Print(" -f \"%s\"", Dir ? Dir + 1 : MakePath.Get());
				#else
				a.Print("/C set");
				#endif
				Exe = "C:\\Windows\\System32\\cmd.exe";
			}
			else
			{
				if (Jobs.CastInt32())
					a.Print("-j %i -f \"%s\"", Jobs.CastInt32(), MakePath.Get());
				else
					a.Print("-f \"%s\"", MakePath.Get());
			}

			if (Args)
				a.Print(" %s", Args.Get());
			GAutoString Temp(a.NewStr());

// printf("BuildThread::Main.3 SubProc(%s)\n", Temp.Get());
			if (SubProc.Reset(new GSubProcess(Exe, Temp)))
			{
// printf("BuildThread::Main.4 SubProc(%s)\n", InitDir);
				SubProc->SetInitFolder(InitDir);
				if (Compiler == MingW)
					SubProc->SetEnvironment("PATH", "c:\\MingW\\bin;C:\\MinGW\\msys\\1.0\\bin;%PATH%");
				
// printf("BuildThread::Main.5 SubProc starting\n");
				if ((Status = SubProc->Start(true, false)))
				{
// printf("BuildThread::Main.6 SubProc reading output\n");
					// Read all the output					
					char Buf[256];
					int rd;
					while ( (rd = SubProc->Read(Buf, sizeof(Buf))) > 0 )
					{
						Write(Buf, rd);
					}
					
// printf("BuildThread::Main.7 SubProc getting exit value\n");
					uint32 ex = SubProc->GetExitValue();
					Print("Make exited with %i (0x%x)\n", ex, ex);
				}
				else
				{
					// Create a nice error message.
// printf("BuildThread::Main.8 getting err\n");
					GAutoString ErrStr = LgiErrorCodeToString(SubProc->GetErrorCode());
					if (ErrStr)
					{
						char *e = ErrStr + strlen(ErrStr);
						while (e > ErrStr && strchr(" \t\r\n.", e[-1]))
							*(--e) = 0;
					}
					
					sprintf_s(ErrBuf, sizeof(ErrBuf), "Running make failed with %i (%s)\n",
						SubProc->GetErrorCode(),
						ErrStr.Get());
					Err = ErrBuf;
				}
			}
		}
	}
	else
	{
		Err = "Couldn't find 'make'";
		LgiTrace("%s,%i - %s.\n", _FL, Err);
	}

// printf("BuildThread::Main.9 posting done\n");
	AppWnd *w = Proj->GetApp();
	if (w)
	{
		w->PostEvent(M_BUILD_DONE);		
		if (Err)
			Proj->GetApp()->PostEvent(M_BUILD_ERR, 0, (GMessage::Param)NewStr(Err));
	}
	
// printf("BuildThread::Main.10 exiting\n");
	return 0;
}

//////////////////////////////////////////////////////////////////////////////////
IdeProject::IdeProject(AppWnd *App) : IdeCommon(NULL)
{
	Project = this;
	d = new IdeProjectPrivate(App, this);
	Tag = NewStr("Project");
}

IdeProject::~IdeProject()
{
	d->App->OnProjectDestroy(this);
	DeleteObj(d);
}

void IdeProject::ShowFileProperties(const char *File)
{
	ProjectNode *Node = NULL;
	char *fp = FindFullPath(File, &Node);
	if (Node)
	{
		Node->OnProperties();
	}
}

const char *IdeProject::GetFileComment()
{
	return d->Settings.GetStr(ProjCommentFile);
}

const char *IdeProject::GetFunctionComment()
{
	return d->Settings.GetStr(ProjCommentFunction);
}

IdeProject *IdeProject::GetParentProject()
{
	return d->ParentProject;
}

void IdeProject::SetParentProject(IdeProject *p)
{
	d->ParentProject = p;
}

bool IdeProject::GetChildProjects(List<IdeProject> &c)
{
	CollectAllSubProjects(c);
	return c.First() != 0;
}

bool IdeProject::RelativePath(char *Out, char *In, bool Debug)
{
	if (Out && In)
	{
		GAutoString Base = GetBasePath();
		if (Base)
		{
if (Debug) LgiTrace("XmlBase='%s'\n     In='%s'\n", Base.Get(), In);
			
			GToken b(Base, DIR_STR);
			GToken i(In, DIR_STR);
			
if (Debug) LgiTrace("Len %i-%i\n", b.Length(), i.Length());
			
			int ILen = i.Length() + (DirExists(In) ? 0 : 1);
			int Max = min(b.Length(), ILen);
			int Common = 0;
			for (; Common < Max; Common++)
			{
				#ifdef WIN32
				#define StrCompare stricmp
				#else
				#define StrCompare strcmp
				#endif
				
if (Debug) LgiTrace("Cmd '%s'-'%s'\n", b[Common], i[Common]);
				if (StrCompare(b[Common], i[Common]) != 0)
				{
					break;
				}
			}
			
if (Debug) LgiTrace("Common=%i\n", Common);
			if (Common > 0)
			{
				if (Common < b.Length())
				{
					Out[0] = 0;
					int Back = b.Length() - Common;
if (Debug) LgiTrace("Back=%i\n", Back);
					for (int n=0; n<Back; n++)
					{
						strcat(Out, "..");
						if (n < Back - 1) strcat(Out, DIR_STR);
					}
				}
				else
				{
					strcpy(Out, ".");
				}
				for (int n=Common; n<i.Length(); n++)
				{
					sprintf(Out+strlen(Out), "%s%s", DIR_STR, i[n]);
				}
				
				return true;
			}	
			else
			{
				strcpy(Out, In);
				return true;
			}		
		}
	}
	
	return false;
}

bool IdeProject::GetExePath(char *Path, int Len)
{
	const char *PExe = d->Settings.GetStr(ProjExe);
	if (PExe)
	{
		if (LgiIsRelativePath(PExe))
		{
			GAutoString Base = GetBasePath();
			if (Base)
			{
				LgiMakePath(Path, Len, Base, PExe);
			}
			else return false;
		}
		else
		{
			strcpy_s(Path, Len, PExe);
		}
		
		return true;
	}
	else return false;
}

GAutoString IdeProject::GetMakefile()
{
	GAutoString Path;
	const char *PMakefile = d->Settings.GetStr(ProjMakefile);
	if (PMakefile)
	{
		if (LgiIsRelativePath(PMakefile))
		{
			GAutoString Base = GetBasePath();
			if (Base)
			{
				char p[MAX_PATH];
				LgiMakePath(p, sizeof(p), Base, PMakefile);
				Path.Reset(NewStr(p));
			}
		}
		else
		{
			Path.Reset(NewStr(PMakefile));
		}
	}
	
	return Path;
}

void IdeProject::Clean(bool Release)
{
	if (!d->Build &&
		d->Settings.GetStr(ProjMakefile))
	{
		GAutoString m = GetMakefile();
		if (m)
		{
			GString a = "clean";
			if (Release)
				a += " Build=Release";
			d->Build.Reset(new BuildThread(this, m, a));
		}
	}
}

char *QuoteStr(char *s)
{
	GStringPipe p(256);
	while (s && *s)
	{
		if (*s == ' ')
		{
			p.Push("\\ ", 2);
		}
		else p.Push(s, 1);
		s++;
	}
	return p.NewStr();
}

class ExecuteThread : public GThread, public GStream
{
	IdeProject *Proj;
	char *Exe, *Args, *Path;
	int Len;
	ExeAction Act;

public:
	ExecuteThread(IdeProject *proj, const char *exe, const char *args, char *path, ExeAction act) : GThread("ExecuteThread")
	{
		Len = 32 << 10;
		Proj = proj;
		Act = act;
		Exe = NewStr(exe);
		Args = NewStr(args);
		Path = NewStr(path);
		DeleteOnExit = true;
		Run();
	}
	
	~ExecuteThread()
	{
		DeleteArray(Exe);
		DeleteArray(Args);
		DeleteArray(Path);
	}
	
	int Main()
	{
		if (Proj->GetApp())
		{
			Proj->GetApp()->PostEvent(M_APPEND_TEXT, 0, 1);
		}
		
		if (Exe)
		{
			GProcess p;
			if (Act == ExeDebug)
			{
				char *a = QuoteStr(Exe);
				char *b = QuoteStr(Path);				
				
				p.Run("kdbg", a, b, true, 0, this);
				
				DeleteArray(a);
				DeleteArray(b);
			}
			else if (Act == ExeValgrind)
			{
				#ifdef LINUX
				if (Proj->GetExecutable())
				{
					char Path[256];
					strcpy_s(Path, sizeof(Path), Exe);
					LgiTrimDir(Path);
									
					char *Term = 0;
					char *WorkDir = 0;
					char *Execute = 0;
					switch (LgiGetWindowManager())
					{
						case WM_Kde:
							Term = "konsole";
							WorkDir = "--workdir ";
							Execute = "-e";
							break;
						case WM_Gnome:
							Term = "gnome-terminal";
							WorkDir = "--working-directory=";
							Execute = "-x";
							break;
					}
					
					if (Term && WorkDir && Execute)
					{					
						char *e = QuoteStr(Proj->GetExecutable());
						char *p = QuoteStr(Path);
						char *a = Proj->GetExeArgs() ? Proj->GetExeArgs() : (char*)"";
						char Args[512];
						sprintf(Args,
								"%s%s "
								"--noclose "
								"%s valgrind --tool=memcheck --num-callers=20 %s %s",
								WorkDir,
								p,
								Execute,
								e, a);
								
						LgiExecute(Term, Args);
					}
				}
				#endif
			}
			else
			{
				p.Run(Exe, Args, Path, true, 0, this);
			}
		}
		
		return 0;
	}

	int Write(const void *Buffer, int Size, int Flags = 0)
	{
		if (Len > 0)
		{
			if (Proj->GetApp())
			{
				Size = min(Size, Len);
				Proj->GetApp()->PostEvent(M_APPEND_TEXT, (GMessage::Param)NewStr((char*)Buffer, Size), 1);
				Len -= Size;
			}
			return Size;
		}
		
		return 0;
	}
};

GDebugContext *IdeProject::Execute(ExeAction Act)
{
	GAutoString Base = GetBasePath();
	if (d->Settings.GetStr(ProjExe) &&
		Base)
	{
		char e[MAX_PATH];
		if (GetExePath(e, sizeof(e)))
		{
			if (FileExists(e))
			{
				const char *Args = d->Settings.GetStr(ProjArgs);
				if (Act == ExeDebug)
				{
					return new GDebugContext(d->App, this, e, Args);
				}
				else
				{
					new ExecuteThread(this, e, Args, Base, Act);
				}
			}
			else
			{
				LgiMsg(Tree, "Executable '%s' doesn't exist.\n", AppName, MB_OK, e);
			}
		}
	}
	
	return NULL;
}

void IdeProject::Build(bool All, bool Release)
{
	if (!d->Build)
	{
		GAutoString m = GetMakefile();
		if (m)
		{		
			d->Build.Reset(new BuildThread(this, m, Release?"Build=Release":NULL));
		}
	}
}

void IdeProject::StopBuild()
{
	d->Build.Reset();
}

bool IdeProject::Serialize()
{
	return true;
}

AppWnd *IdeProject::GetApp()
{
	return d->App;
}

const char *IdeProject::GetIncludePaths()
{
	return d->Settings.GetStr(ProjIncludePaths);
}

const char *IdeProject::GetPreDefinedValues()
{
	return d->Settings.GetStr(ProjDefines);
}

const char *IdeProject::GetExeArgs()
{
	return d->Settings.GetStr(ProjArgs);
}

const char *IdeProject::GetExecutable()
{
	return d->Settings.GetStr(ProjExe);
}

char *IdeProject::GetFileName()
{
	return d->FileName;
}

int IdeProject::GetPlatforms()
{
	return PLATFORM_ALL;
}
	
GAutoString IdeProject::GetFullPath()
{
	GAutoString Status;
	if (!d->FileName)
	{
		LgiAssert(!"No path.");
		return Status;
	}

	GArray<char*> sections;
	IdeProject *proj = this;
	while (	proj &&
			proj->GetFileName() &&
			LgiIsRelativePath(proj->GetFileName()))
	{
		sections.AddAt(0, proj->GetFileName());
		proj = proj->GetParentProject();
	}

	if (!proj)
	{
		LgiAssert(!"All projects have a relative path?");
		return Status; // No absolute path in the parent projects?
	}

	char p[MAX_PATH];
	strcpy_s(p, sizeof(p), proj->GetFileName()); // Copy the base path
	if (sections.Length() > 0)
		LgiTrimDir(p); // Trim off the filename
	for (int i=0; i<sections.Length(); i++) // For each relative section
	{
		LgiMakePath(p, sizeof(p), p, sections[i]); // Append relative path
		if (i < sections.Length() - 1)
			LgiTrimDir(p); // Trim filename off
	}
	
	Status.Reset(NewStr(p));
	return Status;
}

GAutoString IdeProject::GetBasePath()
{
	GAutoString a = GetFullPath();
	LgiTrimDir(a);
	return a;
}

void IdeProject::CreateProject()
{
	Empty();
	
	d->FileName.Reset();
	d->App->GetTree()->Insert(this);
	
	ProjectNode *f = new ProjectNode(this);
	if (f)
	{
		f->SetName("Source");
		f->SetType(NodeDir);
		InsertTag(f);
	}
	f = new ProjectNode(this);
	if (f)
	{
		f->SetName("Headers");
		f->SetType(NodeDir);
		InsertTag(f);
	}
	d->Dirty = true;
	Expanded(true);	
}

bool IdeProject::OpenFile(char *FileName)
{
	bool Status = false;
	
	Empty();

	if (LgiIsRelativePath(FileName))
	{
		char p[MAX_PATH];
		getcwd(p, sizeof(p));
		LgiMakePath(p, sizeof(p), p, FileName);
		d->FileName.Reset(NewStr(p));
	}
	else
	{
		d->FileName.Reset(NewStr(FileName));
	}

	if (d->FileName)
	{
		GFile f;
		if (f.Open(d->FileName, O_READWRITE))
		{
			GXmlTree x;
			GXmlTag r;
			if ((Status = x.Read(&r, &f)))
			{
				OnOpen(&r);
				d->App->GetTree()->Insert(this);
				Expanded(true);
				
				d->Settings.Serialize(&r, false /* read */);
			}
			else
			{
				LgiMsg(Tree, x.GetErrorMsg(), AppName);
			}
		}
	}

	return Status;
}

bool IdeProject::SaveFile(char *FileName)
{
	GAutoString Full = GetFullPath();
	if (ValidStr(Full))
	{
		GFile f;
		
		if (f.Open(Full, O_WRITE))
		{
			GXmlTree x;

			d->Settings.Serialize(this, true /* write */);
			if (x.Write(this, &f))
			{
				d->Dirty = false;
				return true;
			}
		}
	}

	return false;
}

void IdeProject::SetDirty()
{
	d->Dirty = true;
}

void IdeProject::SetClean()
{
	if (d->Dirty)
	{
		if (!ValidStr(d->FileName))
		{
			GFileSelect s;
			s.Parent(Tree);
			s.Name("Project.xml");
			if (s.Save())
			{
				d->FileName.Reset(NewStr(s.Name()));
				d->App->OnFile(d->FileName, true);
				Update();
			}
		}

		SaveFile(0);
	}

	ForAllProjectNodes(p)
	{
		p->SetClean();
	}
}

char *IdeProject::GetText(int Col)
{
	if (d->FileName)
	{
		char *s = strrchr(d->FileName, DIR_CHAR);
		return s ? s + 1 : d->FileName;
	}

	return (char*)Untitled;
}

int IdeProject::GetImage(int Flags)
{
	return 0;
}

void IdeProject::Empty()
{
	GXmlTag *t;
	while ((t = Children.First()))
	{
		ProjectNode *n = dynamic_cast<ProjectNode*>(t);
		if (n)
		{
			n->Remove();
		}
		DeleteObj(t);
	}
}

GXmlTag *IdeProject::Create(char *Tag)
{
	if (!stricmp(Tag, TagSettings))
		return false;

	return new ProjectNode(this);
}

void IdeProject::OnMouseClick(GMouse &m)
{
	if (m.IsContextMenu())
	{
		GSubMenu Sub;
		Sub.AppendItem("New Folder", IDM_NEW_FOLDER, true);
		Sub.AppendItem("New Web Folder", IDM_WEB_FOLDER, true);
		Sub.AppendSeparator();
		Sub.AppendItem("Settings", IDM_SETTINGS, true);
		Sub.AppendItem("Insert Dependency", IDM_INSERT_DEP, true);

		m.ToScreen();
		GdcPt2 c = _ScrollPos();
		m.x -= c.x;
		m.y -= c.y;
		switch (Sub.Float(Tree, m.x, m.y))
		{
			case IDM_NEW_FOLDER:
			{
				GInput Name(Tree, "", "Name:", AppName);
				if (Name.DoModal())
				{
					GetSubFolder(this, Name.Str, true);
				}
				break;
			}
			case IDM_WEB_FOLDER:
			{
				WebFldDlg Dlg(Tree, 0, 0, 0);
				if (Dlg.DoModal())
				{
					if (Dlg.Ftp && Dlg.Www)
					{
						IdeCommon *f = GetSubFolder(this, Dlg.Name, true);
						if (f)
						{
							f->SetAttr(OPT_Ftp, Dlg.Ftp);
							f->SetAttr(OPT_Www, Dlg.Www);
						}
					}
				}
				break;
			}
			case IDM_SETTINGS:
			{
				if (d->Settings.Edit(Tree))
				{
					SetDirty();
				}
				break;
			}
			case IDM_INSERT_DEP:
			{
				GFileSelect s;
				s.Parent(Tree);
				s.Type("Project", "*.xml");
				if (s.Open())
				{
					ProjectNode *New = new ProjectNode(this);
					if (New)
					{
						New->SetFileName(s.Name());
						New->SetType(NodeDependancy);
						InsertTag(New);
						SetDirty();
					}
				}
				break;
			}
		}
	}
}

char *IdeProject::FindFullPath(const char *File, class ProjectNode **Node)
{
	char *Full = 0;
	
	ForAllProjectNodes(c)
	{
		ProjectNode *n = c->FindFile(File, &Full);
		if (n)
		{
			if (Node)
				*Node = n;			
			break;
		}
	}
	
	return Full;		
}

bool IdeProject::InProject(const char *Path, bool Open, IdeDoc **Doc)
{
	ProjectNode *n = 0;

	ForAllProjectNodes(c)
	{
		if ((n = c->FindFile(Path, 0)))
		{
			break;
		}
	}
	
	if (n && Doc)
	{
		*Doc = n->Open();
	}
	
	return n != 0;
}

char *GetQuotedStr(char *Str)
{
	char *s = strchr(Str, '\"');
	if (s)
	{
		s++;
		char *e = strchr(s, '\"');
		if (e)
		{
			return NewStr(s, e - s);
		}
	}

	return 0;
}

void IdeProject::ImportDsp(char *File)
{
	if (File && FileExists(File))
	{
		char Base[256];
		strcpy(Base, File);
		LgiTrimDir(Base);
		
		char *Dsp = ReadTextFile(File);
		if (Dsp)
		{
			GToken Lines(Dsp, "\r\n");
			IdeCommon *Current = this;
			bool IsSource = false;
			for (int i=0; i<Lines.Length(); i++)
			{
				char *L = Lines[i];
				if (strnicmp(L, "# Begin Group", 13) == 0)
				{
					char *Folder = GetQuotedStr(L);
					if (Folder)
					{
						IdeCommon *Sub = Current->GetSubFolder(this, Folder, true);
						if (Sub)
						{
							Current = Sub;
						}
						DeleteArray(Folder);
					}
				}
				else if (strnicmp(L, "# End Group", 11) == 0)
				{
					IdeCommon *Parent = dynamic_cast<IdeCommon*>(Current->GetParent());
					if (Parent)
					{
						Current = Parent;
					}
				}
				else if (strnicmp(L, "# Begin Source", 14) == 0)
				{
					IsSource = true;
				}
				else if (strnicmp(L, "# End Source", 12) == 0)
				{
					IsSource = false;
				}
				else if (Current && IsSource && strncmp(L, "SOURCE=", 7) == 0)
				{
					ProjectNode *New = new ProjectNode(this);
					if (New)
					{
						char *Src = 0;
						if (strchr(L, '\"'))
						{
							Src = GetQuotedStr(L);
						}
						else
						{
							Src = NewStr(L + 7);
						}						
						if (Src)
						{
							// Make absolute path
							char Abs[256];
							LgiMakePath(Abs, sizeof(Abs), Base, ToUnixPath(Src));
							
							// Make relitive path
							New->SetFileName(Src);
							DeleteArray(Src);
						}

						Current->InsertTag(New);
						SetDirty();
					}
				}
			}
			
			DeleteArray(Dsp);
		}
	}
}

IdeProjectSettings *IdeProject::GetSettings()
{
	return &d->Settings;
}

bool IdeProject::BuildIncludePaths(GArray<char*> &Paths, bool Recurse, IdePlatform Platform)
{
	List<IdeProject> Projects;
	if (Recurse)
	{
		GetChildProjects(Projects);
	}
	Projects.Insert(this, 0);
	
	for (IdeProject *p=Projects.First(); p; p=Projects.Next())
	{
		const char *AllIncludes = d->Settings.GetStr(ProjIncludePaths, NULL, Platform);
		GAutoString IncludePaths = ToNativePath(AllIncludes);
		if (IncludePaths)
		{
			GAutoString Base = p->GetBasePath();
			
			GArray<GAutoString> Inc;
			GToken Parts(IncludePaths, (char*)",;\r\n");
			for (int i=0; i<Parts.Length(); i++)
			{
				char *Path = Parts[i];
				while (*Path && strchr(WhiteSpace, *Path))
					Path++;
				
				if (*Path == '`')
				{
					// Run config app to get the full path list...
					GAutoString a(TrimStr(Path, "`"));
					char *Args = strchr(a, ' ');
					if (Args)
						*Args++ = 0;
					GProcess p;
					GStringPipe Out;
					if (p.Run(a, Args, NULL, true, NULL, &Out))
					{
						GAutoString result(Out.NewStr());
						GToken t(result, " \t\r\n");
						for (int i=0; i<t.Length(); i++)
						{
							char *inc = t[i];
							if (inc[0] == '-' &&
								inc[1] == 'I')
							{
								Inc.New().Reset(NewStr(inc + 2));
							}
						}
					}
					else LgiTrace("%s:%i - Error: failed to run process for '%s'\n", _FL, a.Get());
				}
				else
				{
					// Add path literal
					Inc.New().Reset(NewStr(Path));
				}
			}
			
			for (int i=0; i<Inc.Length(); i++)
			{
				char *Path = Inc[i];
				char *Full = 0, Buf[MAX_PATH];
				if
				(
					*Path != '/'
					&&
					!(
						IsAlpha(*Path)
						&&
						Path[1] == ':'
					)
				)
				{
					LgiMakePath(Buf, sizeof(Buf), Base, Path);
					Full = Buf;
				}
				else
				{
					Full = Inc[i];
				}
				
				bool Has = false;
				for (int n=0; n<Paths.Length(); n++)
				{
					if (stricmp(Paths[n], Full) == 0)
					{
						Has = true;
						break;
					}
				}
				
				if (!Has)
				{
					Paths.Add(NewStr(Full));
				}
			}
		}
	}

	return true;
}

void IdeProjectPrivate::CollectAllFiles(GTreeNode *Base, GArray<ProjectNode*> &Files, bool SubProjects, int Platform)
{
	for (GTreeItem *i = Base->GetChild(); i; i = i->GetNext())
	{
		IdeProject *Proj = dynamic_cast<IdeProject*>(i);
		if (Proj)
		{
			if (Proj->GetParentProject() && !SubProjects)
			{
				continue;
			}
		}
		else
		{
			ProjectNode *p = dynamic_cast<ProjectNode*>(i);
			if (p)
			{
				if (p->GetType() == NodeSrc ||
					p->GetType() == NodeHeader)
				{
					if (p->GetPlatforms() & Platform)
					{
						Files.Add(p);
					}
				}
			}
		}

		CollectAllFiles(i, Files, SubProjects, Platform);
	}
}

GAutoString IdeProject::GetTargetName(IdePlatform Platform)
{
	GAutoString Status;
	const char *t = d->Settings.GetStr(ProjTargetName, NULL, Platform);
	if (ValidStr(t))
	{
		// Take target name from the settings
		Status.Reset(NewStr(t));
	}
	else
	{
		char *s = strrchr(d->FileName, DIR_CHAR);
		if (s)
		{
			// Generate the target executable name
			char Target[MAX_PATH];
			strcpy_s(Target, sizeof(Target), s + 1);
			s = strrchr(Target, '.');
			if (s) *s = 0;
			strlwr(Target);
			s = Target;
			for (char *i = Target; *i; i++)
			{
				if (*i != '.' && *i != ' ')
				{
					*s++ = *i;
				}
			}
			*s = 0;
			Status.Reset(NewStr(Target));
		}
	}
	
	return Status;
}

bool IdeProject::GetTargetFile(char *Buf, int BufSize)
{
	bool Status = false;
	GAutoString Target = GetTargetName(PlatformCurrent);
	if (Target)
	{
		const char *TargetType = d->Settings.GetStr(ProjTargetType);
		if (TargetType)
		{
			if (!stricmp(TargetType, "Executable"))
			{
				strcpy_s(Buf, BufSize, Target);
				Status = true;
			}
			else if (!stricmp(TargetType, "DynamicLibrary"))
			{
				char t[MAX_PATH];
				strcpy_s(t, sizeof(t), Target);
				char *ext = LgiGetExtension(t);
				if (!ext)
					sprintf(t + strlen(t), ".%s", LGI_LIBRARY_EXT);
				else if (stricmp(ext, LGI_LIBRARY_EXT))
					strcpy(ext, LGI_LIBRARY_EXT);
				strcpy_s(Buf, BufSize, t);
				Status = true;
			}
			else if (!stricmp(TargetType, "StaticLibrary"))
			{
				snprintf(Buf, BufSize, "lib%s.%s", Target.Get(), LGI_STATIC_LIBRARY_EXT);
				Status = true;
			}
		}
	}
	
	return Status;
}

int StrCmp(char *a, char *b, int d)
{
	return stricmp(a, b);
}

int StrSort(char **a, char **b)
{
	return stricmp(*a, *b);
}

struct Dependency
{
	bool Scanned;
	GAutoString File;
	
	Dependency(const char *f)
	{
		File.Reset(NewStr(f));
	}
};

const char *CastEmpty(char *s)
{
	return s ? s : "";
}

bool IdeProject::GetAllDependencies(GArray<char*> &Files, IdePlatform Platform)
{
	GHashTbl<char*, Dependency*> Deps;
	GAutoString Base = GetBasePath();
	
	// Build list of all the source files...
	GArray<char*> Src;
	CollectAllSource(Src, Platform);
	
	// Get all include paths
	GArray<char*> IncPaths;
	BuildIncludePaths(IncPaths, false, Platform);
	
	// Add all source to dependencies
	for (int i=0; i<Src.Length(); i++)
	{
		char *f = Src[i];
		Dependency *dep = Deps.Find(f);
		if (!dep)
			Deps.Add(f, new Dependency(f));
	}
	
	// Scan all dependencies for includes
	GArray<Dependency*> Unscanned;
	do
	{
		// Find all the unscanned dependencies
		Unscanned.Length(0);
		for (Dependency *d = Deps.First(); d; d = Deps.Next())
		{
			if (!d->Scanned)
				Unscanned.Add(d);
		}		

		for (int i=0; i<Unscanned.Length(); i++)
		{
			// Then scan source for includes...
			Dependency *d = Unscanned[i];
			d->Scanned = true;
			
			char *Src = d->File;
			char Full[MAX_PATH];
			if (LgiIsRelativePath(d->File))
			{
			    LgiMakePath(Full, sizeof(Full), Base, d->File);
			    Src = Full;
			}
			
			GArray<char*> SrcDeps;
			if (GetDependencies(Src, IncPaths, SrcDeps, Platform))
			{
				for (int n=0; n<SrcDeps.Length(); n++)
				{
					// Add include to dependencies...
					char *File = SrcDeps[n];

					if (LgiIsRelativePath(File))
					{
						LgiMakePath(Full, sizeof(Full), Base, File);
						File = Full;
					}

					if (!Deps.Find(File))
					{						
						Deps.Add(File, new Dependency(File));
					}
				}
				SrcDeps.DeleteArrays();
			}
		}
	}
	while (Unscanned.Length() > 0);
	
	for (Dependency *d = Deps.First(); d; d = Deps.Next())
	{
		Files.Add(d->File.Release());
	}
	
	Src.DeleteArrays();
	Deps.DeleteObjects();
	return true;
}

bool IdeProject::GetDependencies(const char *SourceFile, GArray<char*> &IncPaths, GArray<char*> &Files, IdePlatform Platform)
{
    if (!FileExists(SourceFile))
    {
        LgiTrace("%s:%i - can't read '%s'\n", _FL, SourceFile);
        return false;
    }

	GAutoString c8(ReadTextFile(SourceFile));
	if (!c8)
		return false;

	GArray<char*> Headers;
	if (!BuildHeaderList(c8, Headers, IncPaths, false))
		return false;
	
	for (int n=0; n<Headers.Length(); n++)
	{
		char *i = Headers[n];
		char p[MAX_PATH];
		if (!RelativePath(p, i))
		{
			strcpy_s(p, sizeof(p), i);
		}
		ToUnixPath(p);
		Files.Add(NewStr(p));
	}
	Headers.DeleteArrays();
	
	return true;
}

static bool RenameMakefileForPlatform(GAutoString &MakeFile, IdePlatform Platform)
{
	if (!MakeFile)
		return false;

	char *Dot = strrchr(MakeFile, '.');
	if (Dot && stricmp(sCurrentPlatform, ++Dot) != 0)
	{
		char mk[MAX_PATH];
		*Dot = 0;
		sprintf_s(mk, sizeof(mk), "%s%s", MakeFile.Get(), PlatformNames[Platform]);
		if ((Dot = strrchr(mk, '.')))
			strlwr(Dot);
		MakeFile.Reset(NewStr(mk));
	}
	
	return true;
}

bool IdeProject::CreateMakefile(IdePlatform Platform)
{
	const char *PlatformName = PlatformNames[Platform];
	const char *PlatformLibraryExt = NULL;
	const char *PlatformStaticLibExt = NULL;
	const char *PlatformExeExt = "";
	const char *LinkerFlags;
	GStream *Log = d->App->GetBuildLog();
	
	LgiAssert(Log);
	if (!Log)
		return false;
	
	Log->Print("CreateMakefile...\n");
	
	if (Platform == PlatformWin32)
		LinkerFlags = ",--enable-auto-import";
	else
		LinkerFlags = ",-soname,$(TargetFile),-export-dynamic,-R.";

	char Buf[256];
	GAutoString MakeFile = GetMakefile();
	if (!MakeFile)
	{
		MakeFile.Reset(NewStr("../Makefile"));
	}
	// RenameMakefileForPlatform(MakeFile, Platform);
	
	// LGI_LIBRARY_EXT
	switch (Platform)
	{
		case PlatformWin32:
			PlatformLibraryExt = "dll";
			PlatformStaticLibExt = "lib";
			PlatformExeExt = ".exe";
			break;
		case PlatformLinux:
		case PlatformHaiku:
			PlatformLibraryExt = "so";
			PlatformStaticLibExt = "a";
			break;
		case PlatformMac:
			PlatformLibraryExt = "dylib";
			PlatformStaticLibExt = "a";
			break;
		default:
			LgiAssert(0);
			break;
	}
	
	GFile m;
	if (!m.Open(MakeFile, O_WRITE))
	{
		Log->Print("Error: Failed to open '%s' for writing.\n", MakeFile.Get());
		return false;
	}
	
	m.SetSize(0);
	
	m.Print("#!/usr/bin/make\n"
			"#\n"
			"# This makefile generated by LgiIde\n"
			"# http://www.memecode.com/lgi.php\n"
			"#\n"
			"\n"
			".SILENT :\n"
			"\n"
			"CC = gcc\n"
			"CPP = g++\n");

	// Collect all files that require building
	GArray<ProjectNode*> Files;
	d->CollectAllFiles
	(
		this,
		Files,
		false,
		1 << Platform
	);
	
	GAutoString Target = GetTargetName(Platform);
	if (Target)
	{
		m.Print("Target = %s\n", Target.Get());

		// Output the build mode, flags and some paths
		int BuildMode = d->App->GetBuildMode();
		char *BuildModeName = BuildMode ? (char*)"Release" : (char*)"Debug";
		m.Print("ifndef Build\n"
				"	Build = %s\n"
				"endif\n",
				BuildModeName);
		
		GString sDefines[2];
		GString sLibs[2];
		GString sIncludes[2];
		const char *ExtraLinkFlags = NULL;
		const char *ExeFlags = NULL;
		if (Platform == PlatformWin32)
		{
			ExtraLinkFlags = "";
			ExeFlags = " -mwindows";
			m.Print("BuildDir = $(Build)\n"
					"\n"
					"Flags = -fPIC -w -fno-inline -fpermissive\n");
			
			const char *DefDefs = "-DWIN32 -D_REENTRANT";
			sDefines[0] = DefDefs;
			sDefines[1] = DefDefs;
		}
		else
		{
			char PlatformCap[32];
			strcpy_s(	PlatformCap,
						sizeof(PlatformCap),
						Platform == PlatformHaiku ? "BEOS" : PlatformName);
			strupr(PlatformCap);
			
			ExtraLinkFlags = "";
			ExeFlags = "";
			m.Print("BuildDir = $(Build)\n"
					"\n"
					"Flags = -fPIC -w -fno-inline -fpermissive\n" // -fexceptions
					);
			sDefines[0].Printf("-D%s -D_REENTRANT", PlatformCap);
			#ifdef LINUX
			sDefines[0] += " -D_FILE_OFFSET_BITS=64"; // >:-(
			sDefines[0] += " -DPOSIX";
			#endif
			sDefines[1] = sDefines[0];
		}

		List<IdeProject> Deps;
		GetChildProjects(Deps);

		const char *sConfig[] = {"Debug", "Release"};
		for (int Cfg = 0; Cfg < CountOf(sConfig); Cfg++)
		{
			// Set the config
			d->Settings.SetCurrentConfig(sConfig[Cfg]);
		
			// Get the defines setup
			const char *PDefs = d->Settings.GetStr(ProjDefines, NULL, Platform);
			if (ValidStr(PDefs))
			{
				GToken Defs(PDefs, " ;,\r\n");
				for (int i=0; i<Defs.Length(); i++)
				{
					GString s;
					s.Printf(" -D%s", Defs[i]);
					sDefines[Cfg] += s;
				}
			}
		
			// Collect all dependencies, output their lib names and paths
			const char *PLibPaths = d->Settings.GetStr(ProjLibraryPaths, NULL, Platform);
			if (ValidStr(PLibPaths))
			{
				GToken LibPaths(PLibPaths, " \r\n");
				for (int i=0; i<LibPaths.Length(); i++)
				{
					GString s;
					s.Printf(" \\\n\t\t-L%s", ToUnixPath(LibPaths[i]));
					sLibs[Cfg] += s;
				}
			}

			const char *PLibs = d->Settings.GetStr(ProjLibraries, NULL, Platform);
			if (ValidStr(PLibs))
			{
				GToken Libs(PLibs, "\r\n");
				for (int i=0; i<Libs.Length(); i++)
				{
					char *l = Libs[i];
					GString s;
					if (*l == '`' || *l == '-')
						s.Printf(" \\\n\t\t%s", Libs[i]);
					else
						s.Printf(" \\\n\t\t-l%s", ToUnixPath(Libs[i]));
					sLibs[Cfg] += s;
				}
			}

			for (IdeProject *d=Deps.First(); d; d=Deps.Next())
			{
				GAutoString Target = d->GetTargetName(Platform);
				if (Target)
				{
					char t[MAX_PATH];
					strcpy_s(t, sizeof(t), Target);
					if (!strnicmp(t, "lib", 3))
						memmove(t, t + 3, strlen(t + 3) + 1);
					char *dot = strrchr(t, '.');
					if (dot)
						*dot = 0;
													
					GString s;
					s.Printf(" \\\n\t\t-l%s$(Tag)", ToUnixPath(t));
					sLibs[Cfg] += s;

					GAutoString Base = d->GetBasePath();
					if (Base)
					{
						s.Printf(" \\\n\t\t-L%s/$(BuildDir)", ToUnixPath(Base));
						sLibs[Cfg] += s;
					}
				}
			}
			
			// Includes

			// Do include paths
			GHashTable Inc;
			const char *AllIncludes = d->Settings.GetStr(ProjIncludePaths, NULL, Platform);
			if (ValidStr(AllIncludes))
			{
				// Add settings include paths.
				GToken Paths(AllIncludes, "\r\n", Platform);
				for (int i=0; i<Paths.Length(); i++)
				{
					char *p = Paths[i];
					GAutoString pn = ToNativePath(p);
					if (!Inc.Find(pn))
					{
						Inc.Add(pn);
					}
				}
			}
			
			// Add paths of headers
			for (int i=0; i<Files.Length(); i++)
			{
				ProjectNode *n = Files[i];
				
				if (n->GetFileName())
				{
					char *e = LgiGetExtension(n->GetFileName());
					if (e && stricmp(e, "h") == 0)
					{
						for (char *Dir=n->GetFileName(); *Dir; Dir++)
						{
							if (*Dir == '/' || *Dir == '\\')
							{
								*Dir = DIR_CHAR;
							}
						}						

						char Path[256];
						strcpy_s(Path, sizeof(Path), n->GetFileName());

						LgiTrimDir(Path);
					
						char Rel[256];
						if (!RelativePath(Rel, Path))
						{
							strcpy(Rel, Path);
						}
						
						if (stricmp(Rel, ".") != 0)
						{
							GAutoString RelN = ToNativePath(Rel);
							if (!Inc.Find(RelN))
							{
								Inc.Add(RelN);
							}
						}
					}
				}
			}

			List<char> Incs;
			char *i;
			for (void *b=Inc.First(&i); b; b=Inc.Next(&i))
			{
				Incs.Insert(NewStr(i));
			}
			Incs.Sort(StrCmp, 0);
			for (i = Incs.First(); i; i = Incs.Next())
			{
				GString s;
				if (*i == '`')
					s.Printf(" \\\n\t\t%s", i);
				else
					s.Printf(" \\\n\t\t-I%s", ToUnixPath(i));
				sIncludes[Cfg] += s;
			}
		}

		// Output the defs section for Debug and Release		

		// Debug specific
		m.Print("\n"
				"ifeq ($(Build),Debug)\n"
				"	Flags += -g\n"
				"	Tag = d\n"
				"	Defs = -D_DEBUG %s\n"
				"	Libs = %s\n"
				"	Inc = %s\n",
					CastEmpty(sDefines[0].Get()),
					CastEmpty(sLibs[0].Get()),
					CastEmpty(sIncludes[0].Get()));
		
		// Release specific
		m.Print("else\n"
				"	Flags += -s -Os\n"
				"	Defs = %s\n"
				"	Libs = %s\n"
				"	Inc = %s\n"
				"endif\n"
				"\n",
					CastEmpty(sDefines[1].Get()),
					CastEmpty(sLibs[1].Get()),
					CastEmpty(sIncludes[1].Get()));
		
		if (Files.First())
		{
			ProjectNode *n;

			/*
			// Do include paths
			GHashTable Inc;
			const char *AllIncludes = d->Settings.GetStr(ProjIncludePaths, NULL, Platform);
			if (ValidStr(AllIncludes))
			{
				// Add settings include paths.
				GToken Paths(AllIncludes, "\r\n", Platform);
				for (int i=0; i<Paths.Length(); i++)
				{
					char *p = Paths[i];
					GAutoString pn = ToNativePath(p);
					if (!Inc.Find(pn))
					{
						Inc.Add(pn);
					}
				}
			}
			
			// Add paths of headers
			for (int i=0; i<Files.Length(); i++)
			{
				ProjectNode *n = Files[i];
				
				if (n->GetFileName())
				{
					char *e = LgiGetExtension(n->GetFileName());
					if (e && stricmp(e, "h") == 0)
					{
						for (char *Dir=n->GetFileName(); *Dir; Dir++)
						{
							if (*Dir == '/' || *Dir == '\\')
							{
								*Dir = DIR_CHAR;
							}
						}						

						char Path[256];
						strcpy_s(Path, sizeof(Path), n->GetFileName());

						LgiTrimDir(Path);
					
						char Rel[256];
						if (!RelativePath(Rel, Path))
						{
							strcpy(Rel, Path);
						}
						
						if (stricmp(Rel, ".") != 0)
						{
							GAutoString RelN = ToNativePath(Rel);
							if (!Inc.Find(RelN))
							{
								Inc.Add(RelN);
							}
						}
					}
				}
			}

			// Output include paths
			m.Print("# Includes\n"
					"Inc =");
					
			List<char> Incs;
			char *i;
			for (void *b=Inc.First(&i); b; b=Inc.Next(&i))
			{
				Incs.Insert(NewStr(i));
			}
			Incs.Sort(StrCmp, 0);
			for (i = Incs.First(); i; i = Incs.Next())
			{
				if (*i == '`')
					m.Print(" \\\n\t%s", i);
				else
					m.Print(" \\\n\t-I%s", ToUnixPath(i));
			}
			
			m.Print("\n\n");
			*/
			
			GArray<char*> IncPaths;
			if (BuildIncludePaths(IncPaths, false, Platform))
			{
				// Do dependencies
				m.Print("# Dependencies\n"
						"Depends =\t");
				
				for (int c = 0; c < Files.Length(); c++)
				{
					ProjectNode *n = Files[c];
					if (n->GetType() == NodeSrc)
					{
						GAutoString f = ToNativePath(n->GetFileName());
						char *d = f ? strrchr(f, DIR_CHAR) : 0;
						char *file = d ? d + 1 : f;
						d = file ? strrchr(file, '.') : 0;
						if (d)
						{
							if (c) m.Print(" \\\n\t\t\t");
							m.Print("%.*s.o", d - file, file);
						}
					}
				}
				m.Print("\n\n");

				// Write out the target stuff
				m.Print("# Target\n");

				GHashTable DepFiles;

				const char *TargetType = d->Settings.GetStr(ProjTargetType, NULL, Platform);
				if (TargetType)
				{
					if (!stricmp(TargetType, "Executable"))
					{
						m.Print("# Executable target\n"
								"$(Target) :");
								
						GStringPipe Rules;
						IdeProject *Dep;
						for (Dep=Deps.First(); Dep; Dep=Deps.Next())
						{
							// Get dependency to create it's own makefile...
							Dep->CreateMakefile(Platform);
						
							// Build a rule to make the dependency if any of the source changes...
							char t[MAX_PATH] = "";
							GAutoString DepBase = Dep->GetBasePath();
							GAutoString Base = GetBasePath();
							
							if (DepBase && Base && Dep->GetTargetFile(t, sizeof(t)))
							{
								char Rel[MAX_PATH] = "";
								if (!RelativePath(Rel, DepBase))
								{
									strcpy_s(Rel, sizeof(Rel), DepBase);
								}
								ToUnixPath(Rel);
								
								// Add tag to target name
								GToken Parts(t, ".");
								if (Parts.Length() == 2)
									sprintf_s(t, sizeof(t), "lib%s$(Tag).%s", Parts[0], Parts[1]);
								else
									sprintf_s(t, sizeof(t), "%s", Parts[0]);

								sprintf(Buf, "%s/$(BuildDir)/%s", Rel, t);
								m.Print(" %s", Buf);
								
								GArray<char*> AllDeps;
								Dep->GetAllDependencies(AllDeps, Platform);
								LgiAssert(AllDeps.Length() > 0);
								AllDeps.Sort(StrSort);

								Rules.Print("%s : ", Buf);
								for (int i=0; i<AllDeps.Length(); i++)
								{
									if (i)
										Rules.Print(" \\\n\t");
									
									char Rel[MAX_PATH];
									char *f = RelativePath(Rel, AllDeps[i]) ? Rel : AllDeps[i];
									ToUnixPath(f);
									Rules.Print("%s", f);
									
									// Add these dependencies to this makefiles dep list
									if (!DepFiles.Find(f))
										DepFiles.Add(f);
								}
								
								AllDeps.DeleteArrays();
								
								Rules.Print("\n\texport Build=$(Build); \\\n"
											"\t$(MAKE) -C %s",
											Rel);

								GAutoString Mk = Dep->GetMakefile();
								// RenameMakefileForPlatform(Mk, Platform);

								char *DepMakefile = strrchr(Mk, DIR_CHAR);
								if (DepMakefile)
								{
									Rules.Print(" -f %s", DepMakefile + 1);
								}

								Rules.Print("\n\n");
							}
						}

						m.Print(" outputfolder $(Depends)\n"
								"	@echo Linking $(Target) [$(Build)]...\n"
								"	$(CPP)%s%s %s%s -o \\\n"
								"		$(Target) $(addprefix $(BuildDir)/,$(Depends)) $(Libs)\n"
								"	@echo Done.\n"
								"\n",
								ExtraLinkFlags,
								ExeFlags,
								ValidStr(LinkerFlags) ? "-Wl" : "", LinkerFlags);

						GAutoString r(Rules.NewStr());
						if (r)
						{
							m.Write(r, strlen(r));
						}

						// Various fairly global rules
						m.Print("# Create the output folder\n"
								"outputfolder :\n"
								"	-mkdir -p $(BuildDir) 2> /dev/null\n"
								"\n");
							
						m.Print("# Clean out targets\n"
								"clean :\n"
								"	rm -f $(BuildDir)/*.o $(Target)%s\n"
								"	@echo Cleaned $(BuildDir)\n",
								LGI_EXECUTABLE_EXT);
						
						for (IdeProject *d=Deps.First(); d; d=Deps.Next())
						{
							GAutoString mk = d->GetMakefile();
							if (mk)
							{
								GAutoString my_base = GetBasePath();
								GAutoString dep_base = d->GetBasePath();
								GAutoString rel_dir = LgiMakeRelativePath(my_base, dep_base);
								char *mk_leaf = strrchr(mk, DIR_CHAR);
								m.Print("	+make -C \"%s\" -f \"%s\" clean\n",
									ToUnixPath(rel_dir ? rel_dir.Get() : dep_base.Get()),
									ToUnixPath(mk_leaf ? mk_leaf + 1 : mk.Get()));
							}
						}
						m.Print("\n");
					}
					// Shared library
					else if (!stricmp(TargetType, "DynamicLibrary"))
					{
						m.Print("TargetFile = lib$(Target)$(Tag).%s\n"
								"$(TargetFile) : outputfolder $(Depends)\n"
								"	@echo Linking $(TargetFile) [$(Build)]...\n"
								"	$(CPP)$s -shared \\\n"
								"		%s%s \\\n"
								"		-o $(BuildDir)/$(TargetFile) \\\n"
								"		$(addprefix $(BuildDir)/,$(Depends)) \\\n"
								"		$(Libs)\n"
								"	@echo Done.\n"
								"\n",
								PlatformLibraryExt,
								ValidStr(ExtraLinkFlags) ? "-Wl" : "", ExtraLinkFlags,
								LinkerFlags);

						// Cleaning target
						m.Print("# Create the output folder\n"
								"outputfolder :\n"
								"	-mkdir -p $(BuildDir) 2> /dev/null\n"
								"\n"
								"# Clean out targets\n"
								"clean :\n"
								"	rm -f $(BuildDir)/*.o $(BuildDir)/$(TargetFile)\n"
								"	@echo Cleaned $(BuildDir)\n"
								"\n",
								PlatformLibraryExt);
					}
					// Static library
					else if (!stricmp(TargetType, "StaticLibrary"))
					{
						m.Print("TargetFile = lib$(Target)$(Tag).%s\n"
								"$(TargetFile) : outputfolder $(Depends)\n"
								"	@echo Linking $(TargetFile) [$(Build)]...\n"
								"	ar rcs $(BuildDir)/$(TargetFile) $(addprefix $(BuildDir)/,$(Depends))\n"
								"	@echo Done.\n"
								"\n",
								PlatformStaticLibExt);

						// Cleaning target
						m.Print("# Create the output folder\n"
								"outputfolder :\n"
								"	-mkdir -p $(BuildDir) 2> /dev/null\n"
								"\n"
								"# Clean out targets\n"
								"clean :\n"
								"	rm -f $(BuildDir)/*.o $(BuildDir)/$(TargetFile)\n"
								"	@echo Cleaned $(BuildDir)\n"
								"\n",
								PlatformStaticLibExt);
					}
				}

				// Create dependency tree, starting with all the source files.
				for (int idx=0; idx<Files.Length(); idx++)
				{
					ProjectNode *n = Files[idx];
					if (n->GetType() == NodeSrc)
					{
						GAutoString Src = n->GetFullPath();
						if (Src)
						{
							char Part[256];
							
							char *d = strrchr(Src, DIR_CHAR);
							d = d ? d + 1 : Src;
							strcpy(Part, d);
							char *Dot = strrchr(Part, '.');
							if (Dot) *Dot = 0;

							char Rel[MAX_PATH];
							if (!RelativePath(Rel, Src))
							{
					 			strcpy_s(Rel, sizeof(Rel), Src);
							}
							
							m.Print("%s.o : %s ", Part, ToUnixPath(Rel));

							GArray<char*> SrcDeps;
							if (GetDependencies(Src, IncPaths, SrcDeps, Platform))
							{
								for (int i=0; i<SrcDeps.Length(); i++)
								{
									char *SDep = SrcDeps[i];
									
									if (stricmp(Src.Get(), SDep) != 0)
									{
										if (i) m.Print(" \\\n\t");
										m.Print("%s", SDep);
										if (!DepFiles.Find(SDep))
										{
											DepFiles.Add(SDep);
										}
									}
									else printf("%s:%i - not add dep: '%s' '%s'\n", _FL, Src.Get(), SDep);
								}
								SrcDeps.DeleteArrays();
							}

							char *Ext = LgiGetExtension(Src);
							const char *Compiler = Src && !stricmp(Ext, "c") ? "CC" : "CPP";

							m.Print("\n"
									"	@echo $(<F) [$(Build)]\n"
									"	$(%s) $(Inc) $(Flags) $(Defs) -c $< -o $(BuildDir)/$(@F)\n"
									"\n",
									Compiler);
						}
					}
				}
				
				// Do remaining include file dependencies
				bool Done = false;
				GHashTable Processed;
				GAutoString Base = GetBasePath();
				while (!Done)
				{
					Done = true;
					char *Src;
					for (void *b=DepFiles.First(&Src); b; b=DepFiles.Next(&Src))
					{
						if (Processed.Find(Src))
							continue;

						Done = false;
						Processed.Add(Src);
						
						char Full[MAX_PATH], Rel[MAX_PATH];
						if (LgiIsRelativePath(Src))
						{
							LgiMakePath(Full, sizeof(Full), Base, Src);
							strcpy_s(Rel, sizeof(Rel), Src);
						}
						else
						{
							strcpy_s(Full, sizeof(Full), Src);
							GAutoString a = LgiMakeRelativePath(Base, Src);
							if (a)
							{
								strcpy_s(Rel, sizeof(Rel), a);
							}
							else
							{
								strcpy_s(Rel, sizeof(Rel), a);
								LgiTrace("%s:%i - Failed to make relative path '%s' '%s'\n",
									_FL,
									Base.Get(), Src);
							}
						}
						
						char *c8 = ReadTextFile(Full);
						if (c8)
						{
							GArray<char*> Headers;
							if (BuildHeaderList(c8, Headers, IncPaths, false))
							{
								m.Print("%s : ", Rel);

								for (int n=0; n<Headers.Length(); n++)
								{
									char *i = Headers[n];
									
									if (n) m.Print(" \\\n\t");
									
									char Rel[MAX_PATH];
									if (!RelativePath(Rel, i))
									{
				 						strcpy(Rel, i);
									}

									if (stricmp(i, Full) != 0)
										m.Print("%s", ToUnixPath(Rel));
									
									if (!DepFiles.Find(i))
									{
										DepFiles.Add(i);
									}
								}
								Headers.DeleteArrays();

								m.Print("\n\n");
							}
							else LgiTrace("%s:%i - Error: BuildHeaderList failed for '%s'\n", _FL, Full);
							
							DeleteArray(c8);
						}
						else LgiTrace("%s:%i - Error: Failed to read '%s'\n", _FL, Full);
						
						break;
					}
				}

				// Output VPATH
				m.Print("VPATH=%%.cpp \\\n");
				for (int i=0; i<IncPaths.Length(); i++)
				{
					char *p = IncPaths[i];
					if (p && !strchr(p, '`'))
					{
						if (!LgiIsRelativePath(p))
						{
							GAutoString a = LgiMakeRelativePath(Base, p);
							m.Print("\t%s \\\n", a?a:p);
						}
						else
						{
							m.Print("\t%s \\\n", p);
						}
					}
				}
				m.Print("\t$(BuildDir)\n\n");

				const char *OtherMakefileRules = d->Settings.GetStr(ProjMakefileRules, NULL, Platform);
				if (ValidStr(OtherMakefileRules))
				{
					m.Print("\n%s\n", OtherMakefileRules);
				}					
			}
		}
		else
		{
			m.Print("# No files require building.\n");
		}			
	}

	Log->Print("...Done: '%s'\n", MakeFile.Get());

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////
IdeTree::IdeTree() : GTree(100, 0, 0, 100, 100)
{
	Hit = 0;
}

void IdeTree::OnAttach()
{
	SetWindow(this);
}

void IdeTree::OnDragExit()
{
	SelectDropTarget(0);
}

int IdeTree::WillAccept(List<char> &Formats, GdcPt2 p, int KeyState)
{
	static bool First = true;
	
	for (char *f=Formats.First(); f; )
	{
		if (First)
			LgiTrace("    WillAccept='%s'\n", f);
		
		if (stricmp(f, NODE_DROP_FORMAT) == 0 ||
			stricmp(f, LGI_FileDropFormat) == 0)
		{
			f = Formats.Next();
		}
		else
		{
			Formats.Delete(f);
			DeleteArray(f);
			f = Formats.Current();
		}
	}
	
	First = false;
		
	if (Formats.Length() > 0)
	{
		Hit = ItemAtPoint(p.x, p.y);
		if (Hit)
		{
			if (!stricmp(Formats.First(), LGI_FileDropFormat))
			{
				SelectDropTarget(Hit);
				return DROPEFFECT_LINK;
			}
			else
			{
				IdeCommon *Src = dynamic_cast<IdeCommon*>(Selection());
				IdeCommon *Dst = dynamic_cast<IdeCommon*>(Hit);
				if (Src && Dst)
				{
					// Check this folder is not a child of the src
					for (IdeCommon *n=Dst; n; n=dynamic_cast<IdeCommon*>(n->GetParent()))
					{
						if (n == Src)
						{
							return DROPEFFECT_NONE;
						}
					}
				}

				// Valid target
				SelectDropTarget(Hit);
				return DROPEFFECT_MOVE;
			}
		}
	}
	else LgiTrace("%s:%i - No valid drop formats.\n", _FL);

	return DROPEFFECT_NONE;
}

int IdeTree::OnDrop(char *Format, GVariant *Data, GdcPt2 p, int KeyState)
{
	SelectDropTarget(0);

	if (!Hit)
		Hit = ItemAtPoint(p.x, p.y);

	if (!Hit || !Data || !Format)
		return DROPEFFECT_NONE;
	
	if (!stricmp(Format, NODE_DROP_FORMAT))
	{
		if (Data->Type == GV_BINARY && Data->Value.Binary.Length == sizeof(ProjectNode*))
		{
			ProjectNode *Src = ((ProjectNode**)Data->Value.Binary.Data)[0];
			if (Src)
			{
				ProjectNode *Folder = dynamic_cast<ProjectNode*>(Hit);
				while (Folder && Folder->GetType() > NodeDir)
				{
					Folder = dynamic_cast<ProjectNode*>(Folder->GetParent());
				}

				IdeCommon *Dst = dynamic_cast<IdeCommon*>(Folder?Folder:Hit);
				if (Dst)
				{
					// Check this folder is not a child of the src
					for (IdeCommon *n=Dst; n; n=dynamic_cast<IdeCommon*>(n->GetParent()))
					{
						if (n == Src)
						{
							return DROPEFFECT_NONE;
						}
					}

					// Detach
					GTreeItem *i = dynamic_cast<GTreeItem*>(Src);
					i->Detach();
					if (Src->GXmlTag::Parent)
					{
						LgiAssert(Src->GXmlTag::Parent->Children.HasItem(Src));
						Src->GXmlTag::Parent->Children.Delete(Src);
					}
					
					// Attach
					Src->GXmlTag::Parent = Dst;
					Dst->Children.Insert(Src);
					Dst->Insert(Src);
					
					// Dirty
					Src->GetProject()->SetDirty();
				}
			
				return DROPEFFECT_MOVE;
			}
		}
	}
	else if (!stricmp(Format, LGI_FileDropFormat))
	{
		GDropFiles Df(*Data);
		for (int i=0; i<Df.Length(); i++)
		{
			LgiTrace("[%i]=%s\n", i, Df[i]);
		}
	}

	return DROPEFFECT_NONE;
}

