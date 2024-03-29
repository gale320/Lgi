#include <math.h>

#include "Lgi.h"
#include "GTree.h"
#include "GScrollBar.h"
#include "GDisplayString.h"
#include "GPalette.h"

#define TREE_BLOCK          16
#define DRAG_THRESHOLD		4
#define DRAG_SCROLL_EDGE	20
#define DRAG_SCROLL_X		8
#define DRAG_SCROLL_Y		1

#ifdef LINUX
#define TreeUpdateNow		false
#else
#define TreeUpdateNow		true
#endif

#define ForAll(Items)		List<GTreeItem>::I it = Items.Start(); for (GTreeItem *c = *it; c; c = *++it)

//////////////////////////////////////////////////////////////////////////////
// Private class definitions for binary compatibility
class GTreePrivate
{
public:
	// Private data
	int				LineFlags[4];
	bool			LayoutDirty;
	GdcPt2			Limit;
	GdcPt2			LastClick;
	GdcPt2			DragStart;
	int				DragData;
	GMemDC			*IconCache;
	bool			InPour;
	int64			DropSelectTime;
    int8            IconTextGap;
    int				LastLayoutPx;
    
    // Visual style
	GTree::ThumbStyle Btns;
	bool			JoiningLines;

	// Pointers into items... be careful to clear when deleting items...
	GTreeItem		*LastHit;
	List<GTreeItem>	Selection;
	GTreeItem		*DropTarget;

	GTreePrivate()
	{
		LastLayoutPx = -1;
		DropSelectTime = 0;
		InPour = false;
		LastHit = 0;
		DropTarget = 0;
		IconCache = 0;
		LayoutDirty = true;
		IconTextGap = 0;
		
		Btns = GTree::TreeTriangle;
		JoiningLines = false;
	}
	
	~GTreePrivate()
	{
		DeleteObj(IconCache);
	}
};

class GTreeItemPrivate
{
	GArray<GDisplayString*> Ds;
	GArray<uint32> ColPx;

public:
	GTreeItem *Item;
	GRect Pos;
	GRect Thumb;
	GRect Text;
	GRect Icon;
	bool Open;
	bool Selected;
	bool Visible;
	bool Last;
	int Depth;
	
	GTreeItemPrivate(GTreeItem *it)
	{
		Item = it;
		Ds = NULL;
		Pos.ZOff(-1, -1);
		Open = false;
		Selected = false;
		Visible = false;
		Last = false;
		Depth = 0;
		Text.ZOff(-1, -1);
	}

	~GTreeItemPrivate()
	{
		Ds.DeleteObjects();
	}

	GDisplayString *GetDs(int Col, int FixPx)
	{
		if (!Ds[Col])
		{
			GFont *f = Item->GetTree() ? Item->GetTree()->GetFont() : SysFont;		
			Ds[Col] = new GDisplayString(f, Item->GetText(Col));
			if (Ds[Col])
			{
				ColPx[Col] = Ds[Col]->X();
				if (FixPx > 0)
				{
					Ds[Col]->TruncateWithDots(FixPx);
				}
			}
		}
		
		return Ds[Col];
	}
	
	void ClearDs(int Col = -1)
	{
		if (Col >= 0)
		{
			delete Ds[Col];
			Ds[Col] = NULL;
		}
		else
		{
			Ds.DeleteObjects();
		}
	}

	int GetColumnPx(int Col)
	{
		int BasePx = 0;
		
		GetDs(Col, 0);
		if (Col == 0)
		{
			BasePx = (Depth + 1) * TREE_BLOCK;
		}
		
		return ColPx[Col] + BasePx;
	}
};

//////////////////////////////////////////////////////////////////////////////
GTreeNode::GTreeNode()
{
	Parent = NULL;
	Tree = NULL;
}

GTreeNode::~GTreeNode()
{
}

void GTreeNode::_Visible(bool v)
{
	for (GTreeItem *i=GetChild(); i; i=i->GetNext())
	{
		LgiAssert(i != this);
		i->OnVisible(v);
		i->_Visible(v);
	}
}

void GTreeNode::_ClearDs(int Col)
{
	List<GTreeItem>::I it = Items.Start();
	for (GTreeItem *c = *it; c; c = *++it)
	{
		c->_ClearDs(Col);
	}
}

GTreeItem *GTreeNode::Insert(GTreeItem *Obj, int Idx)
{
	LgiAssert(Obj != this);

	if (Obj)
	{
		if (Obj->Tree)
		{
			Obj->Remove();
		}
	}
	
	GTreeItem *NewObj = (Obj) ? Obj : new GTreeItem;
	if (NewObj)
	{
		NewObj->Parent = Item();
		NewObj->_SetTreePtr(Tree);

		Items.Delete(NewObj);
		Items.Insert(NewObj, Idx);

		if (Tree)
		{
			Tree->d->LayoutDirty = true;
			if (Pos() && Pos()->Y() > 0)
			{
				Tree->_UpdateBelow(Pos()->y1);
			}
			else
			{
				Tree->Invalidate();
			}
		}
	}

	return NewObj;
}

void GTreeNode::Detach()
{
	if (Parent)
	{
		GTreeItem *It = Item();
		if (It)
		{
			LgiAssert(Parent->Items.HasItem(It));
			Parent->Items.Delete(It);
		}
		Parent = 0;
	}
	if (Tree)
	{
		Tree->d->LayoutDirty = true;
		Tree->Invalidate();
	}
	if (Item())
		Item()->_SetTreePtr(0);
}

void GTreeNode::Remove()
{
	int y = 0;
	if (Parent)
	{
		GTreeItem *i = Item();
		if (i && i->IsRoot())
		{
			GRect *p = Pos();
			GTreeItem *Prev = GetPrev();
			if (Prev)
			{
				y = Prev->d->Pos.y1;
			}
			else
			{
				y = p->y1;
			}
		}
		else
		{
			y = Parent->d->Pos.y1;
		}
	}

	GTree *t = Tree;

	if (Item())
		Item()->_Remove();

	if (t)
	{
		t->_UpdateBelow(y);
	}
}

bool GTreeNode::IsRoot()
{
	return Parent == 0 || (GTreeNode*)Parent == (GTreeNode*)Tree;
}

int GTreeNode::GetItems()
{
	return Items.Length();
}

int GTreeNode::IndexOf()
{
	if (Parent)
	{
		return Parent->Items.IndexOf(Item());
	}
	else if (Tree)
	{
		return Tree->Items.IndexOf(Item());
	}

	return -1;
}

GTreeItem *GTreeNode::GetChild()
{
	return Items.First();
}

GTreeItem *GTreeNode::GetPrev()
{
	List<GTreeItem> *l = (Parent) ? &Parent->Items : (Tree) ? &Tree->Items : 0;
	if (l)
	{
		int Index = l->IndexOf(Item());
		if (Index >= 0)
		{
			return l->ItemAt(Index-1);
		}
	}

	return 0;
}

GTreeItem *GTreeNode::GetNext()
{
	List<GTreeItem> *l = (Parent) ? &Parent->Items : (Tree) ? &Tree->Items : 0;
	if (l)
	{
		int Index = l->IndexOf(Item());
		if (Index >= 0)
		{
			return l->ItemAt(Index+1);
		}
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////////
GTreeItem::GTreeItem()
{
	d = new GTreeItemPrivate(this);
	Str = 0;
	Sys_Image = -1;
}

GTreeItem::~GTreeItem()
{
	if (Tree)
	{
		if (Tree->d->DropTarget == this)
		{
			Tree->d->DropTarget = 0;
		}

		if (Tree->d->LastHit == this)
		{
			Tree->d->LastHit = 0;
		}
		Tree->Capture(false);
	}

	int y;
	GTree *t = 0;
	if (Parent && (GTreeNode*)Parent != (GTreeNode*)Tree)
	{
		t = Tree;
		y = Parent->d->Pos.y1;
	}
	else if ((GTreeNode*)this != (GTreeNode*)Tree)
	{
		t = Tree;
		GTreeItem *p = GetPrev();
		if (p)
		{
			y = p->d->Pos.y1;
		}
		else
		{
			y = d->Pos.y1;
		}
	}

	_Remove();
	Items.DeleteObjects();
	DeleteObj(d);

	if (t)
	{
		t->_UpdateBelow(y);
	}
}

int GTreeItem::GetColumnSize(int Col)
{
	int Px = d->GetColumnPx(Col);
	if (Expanded())
	{
		ForAll(Items)
		{
			int ChildPx = c->GetColumnSize(Col);
			Px = max(ChildPx, Px);
		}
	}
	return Px;
}

GRect *GTreeItem::Pos()
{
	return &d->Pos;
}

GdcPt2 GTreeItem::_ScrollPos()
{
	GdcPt2 p;
	if (Tree) p = Tree->_ScrollPos();
	return p;
}

GRect *GTreeItem::_GetRect(GTreeItemRect Which)
{
	switch (Which)
	{
		case TreeItemPos: return &d->Pos;
		case TreeItemThumb: return &d->Thumb;
		case TreeItemText: return &d->Text;
		case TreeItemIcon: return &d->Icon;
	}
	
	return 0;
}

GRect *GTreeItem::GetPos(int Col)
{
	return &d->Pos;
}

void GTreeItem::_RePour()
{
	if (Tree)
	{
		Tree->_Pour();
	}
}

void GTreeItem::ScrollTo()
{
	if (Tree && Tree->VScroll)
	{
		GRect c = Tree->GetClient();
		GRect p = d->Pos;
		int y = d->Pos.Y() ? d->Pos.Y() : 16;
		p.Offset(0, -Tree->VScroll->Value() * y);

		if (p.y1 < c.y1)
		{
			int Lines = (c.y1 - p.y1 + y - 1) / y;
			Tree->VScroll->Value(Tree->VScroll->Value() - Lines);
		}
		else if (p.y2 > c.y2)
		{
			int Lines = (p.y2 - c.y2 + y - 1) / y;
			Tree->VScroll->Value(Tree->VScroll->Value() + Lines);
		}
	}
}

void GTreeItem::_SetTreePtr(GTree *t)
{
	if (Tree && !t)
	{
		// Clearing tree pointer, must remove all references to this item that
		// the tree might still have.
		if (d->Selected)
		{
			Tree->d->Selection.Delete(this);
			d->Selected = false;
		}
		if (Tree->d->LastHit == this)
		{
			Tree->d->LastHit = 0;
		}
		if (Tree->d->DropTarget == this)
		{
			Tree->d->DropTarget = 0;
		}
	}
	Tree = t;
	for (GTreeItem *i=Items.First(); i; i=Items.Next())
	{
		i->_SetTreePtr(t);
	}
}

void GTreeItem::_Remove()
{
	if ((GTreeNode*)this != (GTreeNode*)Tree)
	{
		if (Parent)
		{
			LgiAssert(Parent->Items.HasItem(this));
			Parent->Items.Delete(this);
		}
		else if (Tree)
		{
			LgiAssert(Tree->Items.HasItem(this));
			Tree->Items.Delete(this);
		}

		if (Tree)
		{
			LgiAssert(Tree->d);
			Tree->d->LayoutDirty = true;
			Tree->Capture(false);
		}
	}

	Parent = 0;
	_SetTreePtr(0);
}

void GTreeItem::_PourText(GdcPt2 &Size)
{
	GFont *f = Tree ? Tree->GetFont() : SysFont;
	GDisplayString ds(f, GetText());
	Size.x = ds.X() + 4;
	Size.y = 0;
}

void GTreeItem::_PaintText(GItem::ItemPaintCtx &Ctx)
{
	char *Text = GetText();
	if (Text)
	{
		GDisplayString *Ds = d->GetDs(0, d->Text.X());
		GFont *f = Tree ? Tree->GetFont() : SysFont;

		int Tab = f->TabSize();
		f->TabSize(0);
		f->Transparent(false);
		f->Colour(Ctx.Fore, Ctx.Back);
		
		if (Ds)
		{
			Ds->Draw(Ctx.pDC, d->Text.x1 + 2, d->Text.y1 + 1, &d->Text);
			if (Ctx.x2 > d->Text.x2)
			{
				GRect r = Ctx;
				r.x1 = d->Text.x2 + 1;
				if (Ctx.Columns > 1)
					Ctx.pDC->Colour(Ctx.Back);
				else
					Ctx.pDC->Colour(LC_WORKSPACE, 24);
				Ctx.pDC->Rectangle(&r);
			}
		}
		else
		{
			Ctx.pDC->Colour(Ctx.Back);
		}
		
		f->TabSize(Tab);
	}
	else
	{
		Ctx.pDC->Colour(Ctx.Back);
		Ctx.pDC->Rectangle(&Ctx);
	}
}

void GTreeItem::_Pour(GdcPt2 *Limit, int ColumnPx, int Depth, bool Visible)
{
	d->Visible = Visible;
	d->Depth = Depth;

	if (d->Visible)
	{
		GdcPt2 TextSize;
		_PourText(TextSize);
		GImageList *ImgLst = Tree->GetImageList();
		int IconX = (ImgLst && GetImage() >= 0) ? ImgLst->TileX() + Tree->d->IconTextGap : 0;
		int IconY = (ImgLst && GetImage() >= 0) ? ImgLst->TileY() : 0;
		int Height = max(TextSize.y, IconY);
		if (!Height)
		    Height = 16;

		GDisplayString *Ds = d->GetDs(0, 0);

		d->Pos.ZOff(ColumnPx - 1, (Ds ? max(Height, Ds->Y()) : Height) - 1);
		d->Pos.Offset(0, Limit->y);
		if (!d->Pos.Valid())
		{
			printf("Invalid pos: %s, ColumnPx=%i\n", d->Pos.GetStr(), ColumnPx);
		}

		Limit->x = max(Limit->x, d->Pos.x2 + 1);
		Limit->y = max(Limit->y, d->Pos.y2 + 1);
	}
	else
	{
		d->Pos.ZOff(-1, -1);
	}

	GTreeItem *n;
	for (GTreeItem *i=Items.First(); i; i=n)
	{
		n = Items.Next();
		i->d->Last = n == 0;
		i->_Pour(Limit, ColumnPx, Depth+1, d->Open && d->Visible);
	}
}

void GTreeItem::_ClearDs(int Col)
{
	d->ClearDs(Col);	
	GTreeNode::_ClearDs(Col);
}

char *GTreeItem::GetText(int i)
{
	return Str[i];
}

bool GTreeItem::SetText(const char *s, int i)
{
	if (!Str[i].Reset(NewStr(s)))
		return false;

	if (Tree)
		Update();

	return true;
}

int GTreeItem::GetImage(int Flags)
{
	return Sys_Image;
}

void GTreeItem::SetImage(int i)
{
	Sys_Image = i;
}

void GTreeItem::Update()
{
	if (Tree)
	{
		GRect p = d->Pos;
		p.x2 = 1000;
		d->ClearDs();
		Tree->_Update(&p, TreeUpdateNow);
	}
}

bool GTreeItem::Select()
{
	return d->Selected;
}

void GTreeItem::Select(bool b)
{
	if (d->Selected != b)
	{
		d->Selected = b;
		if (b)
		{
			GTreeItem *p = this;
			while ((p = p->GetParent()))
			{
				p->Expanded(true);
			}
		}

		Update();

		if (b && Tree)
		{
			Tree->_OnSelect(this);
			Tree->OnItemSelect(this);
		}
	}
}

bool GTreeItem::Expanded()
{
	return d->Open;
}

void GTreeItem::Expanded(bool b)
{
	if (d->Open != b)
	{
		d->Open = b;

		if (Items.Length() > 0)
		{
			if (Tree)
			{
				Tree->d->LayoutDirty = true;
				Tree->_UpdateBelow(d->Pos.y1);
			}
			OnExpand(b);
		}
	}
}

void GTreeItem::OnExpand(bool b)
{
	_Visible(b);
}

GTreeItem *GTreeItem::_HitTest(int x, int y, bool Debug)
{
	GTreeItem *Status = 0;

	if (d->Pos.Overlap(x, y) &&
		x > (d->Depth*TREE_BLOCK))
	{
		Status = this;
	}

	if (d->Open)
	{
		for (GTreeItem *i=Items.First(); i && !Status; i=Items.Next())
		{
			Status = i->_HitTest(x, y, Debug);
		}
	}

	return Status;
}

void GTreeItem::_MouseClick(GMouse &m)
{
	if (m.Down())
	{
		if ((Items.Length() > 0 &&
			d->Thumb.Overlap(m.x, m.y)) ||
			m.Double())
		{
			Expanded(!Expanded());
		}

		GRect rText = d->Text;
		if (Tree && Tree->Columns.Length() > 0)
			rText.x2 = Tree->X();

		if (rText.Overlap(m.x, m.y) ||
			d->Icon.Overlap(m.x, m.y))
		{
			Select(true);

			if (Tree)
			{
				Tree->OnItemClick(this, m);
			}
		}
	}
}

void GTreeItem::OnPaint(ItemPaintCtx &Ctx)
{
	LgiAssert(Tree);

	// background up to text
	GSurface *&pDC = Ctx.pDC;
	pDC->Colour(LC_WORKSPACE, 24);
	pDC->Rectangle(0, d->Pos.y1, (d->Depth*TREE_BLOCK)+TREE_BLOCK, d->Pos.y2);

	// draw trunk
	GRect Pos = d->Pos;
	Pos.x2 = Pos.x1 + Ctx.ColPx[0] - 1;

	int x = 0;
	COLOUR Lines = LC_MED;
	pDC->Colour(Lines, 24);
	if (Tree->d->JoiningLines)
	{
		for (int i=0; i<d->Depth; i++)
		{
			if (Tree->d->LineFlags[0] & (1 << i))
			{
				pDC->Line(x + 8, Pos.y1, x + 8, Pos.y2);
			}
			x += TREE_BLOCK;
		}
	}
	else
	{
		x += TREE_BLOCK * d->Depth;
	}

	// draw node
	int cy = Pos.y1 + (Pos.Y() >> 1);
	if (Items.Length() > 0)
	{
		d->Thumb.ZOff(8, 8);
		d->Thumb.Offset(x + 4, cy - 4);

		switch (Tree->d->Btns)
		{
			case GTree::TreePlus:
			{
				// plus/minus symbol
				pDC->Colour(LC_LOW, 24);
				pDC->Box(&d->Thumb);
				pDC->Colour(LC_WHITE, 24);
				pDC->Rectangle(d->Thumb.x1+1, d->Thumb.y1+1, d->Thumb.x2-1, d->Thumb.y2-1);
				pDC->Colour(LC_SHADOW, 24);
				pDC->Line(	d->Thumb.x1+2,
							d->Thumb.y1+4,
							d->Thumb.x1+6,
							d->Thumb.y1+4);

				if (!d->Open)
				{
					// not open, so draw the cross bar making the '-' into a '+'
					pDC->Colour(LC_SHADOW, 24);
					pDC->Line(	d->Thumb.x1+4,
								d->Thumb.y1+2,
								d->Thumb.x1+4,
								d->Thumb.y1+6);
				}
				break;
			}
			case GTree::TreeTriangle:
			{
				// Triangle style expander
				pDC->Colour(LC_LOW, 24);

				int Off = 2;
				if (d->Open)
				{
					for (int y=0; y<d->Thumb.Y(); y++)
					{
						int x1 = d->Thumb.x1 + y;
						int x2 = d->Thumb.x2 - y;
						if (x2 < x1)
							break;
						pDC->HLine(x1, x2, d->Thumb.y1 + y + Off);
					}
				}
				else
				{
					for (int x=0; x<d->Thumb.X(); x++)
					{
						int y1 = d->Thumb.y1 + x;
						int y2 = d->Thumb.y2 - x;
						if (y2 < y1)
							break;
						pDC->VLine(d->Thumb.x1 + x + Off, y1, y2);
					}
				}
				break;
			}
		}

		pDC->Colour(Lines, 24);

		if (Tree->d->JoiningLines)
		{
			if (Parent || IndexOf() > 0)
			{
				// draw line to item above
				pDC->Line(x + 8, Pos.y1, x + 8, d->Thumb.y1-1);
			}

			// draw line to leaf beside
			pDC->Line(d->Thumb.x2+1, cy, x + (TREE_BLOCK-1), cy);

			if (!d->Last)
			{
				// draw line to item below
				pDC->Line(x + 8, d->Thumb.y2+1, x + 8, Pos.y2);
			}
		}
	}
	else if (Tree->d->JoiningLines)
	{
		// leaf node
		pDC->Colour(LC_MED, 24);
		if (d->Last)
		{
			pDC->Rectangle(x + 8, Pos.y1, x + 8, cy);
		}
		else
		{
			pDC->Rectangle(x + 8, Pos.y1, x + 8, Pos.y2);
		}

		pDC->Rectangle(x + 8, cy, x + (TREE_BLOCK-1), cy);
	}
	x += TREE_BLOCK;

	// draw icon
	int Image = GetImage(Select());
	GImageList *Lst = Tree->GetImageList();
	if (Image >= 0 && Lst)
	{
		d->Icon.ZOff(Lst->TileX() + Tree->d->IconTextGap - 1, Pos.Y() - 1);
		d->Icon.Offset(x, Pos.y1);

		GColour Background(LC_WORKSPACE, 24);
		pDC->Colour(Background);

		if (Tree->d->IconCache)
		{
			// no flicker
			GRect From;

			From.ZOff(Lst->TileX()-1, Tree->d->IconCache->Y()-1);
			From.Offset(Lst->TileX()*Image, 0);

			pDC->Blt(d->Icon.x1, d->Icon.y1, Tree->d->IconCache, &From);

			pDC->Rectangle(d->Icon.x1 + Lst->TileX(), d->Icon.y1, d->Icon.x2, d->Icon.y2);
		}
		else
		{
			// flickers...
			int Px = d->Icon.y1 + ((Lst->TileY()-Pos.Y()) >> 1);
			pDC->Rectangle(&d->Icon);
			Tree->GetImageList()->Draw(pDC, d->Icon.x1, Px, Image, Background);
		}

		x += d->Icon.X();
	}

	// text: first column
	GdcPt2 TextSize;
	_PourText(TextSize);
	d->Text.ZOff(TextSize.x-1, Pos.Y()-1);
	d->Text.Offset(x, Pos.y1);
	(GRect&)Ctx = d->Text;
	Ctx.x2 = Ctx.ColPx[0] - 1;
	_PaintText(Ctx);
	x = Pos.x2 + 1;

	// text: other columns
	for (int i=1; i<Ctx.Columns; i++)
	{
		Ctx.Set(x, Pos.y1, x + Ctx.ColPx[i] - 1, Pos.y2);
		OnPaintColumn(Ctx, i, Tree->Columns[i]);
		x = Ctx.x2 + 1;
	}
	
	// background after text
	pDC->Colour(LC_WORKSPACE, 24);
	pDC->Rectangle(x, Pos.y1, max(Tree->X(), Tree->d->Limit.x), Pos.y2);

	// children
	if (d->Open)
	{
		if (!d->Last)
		{
			Tree->d->LineFlags[0] |= 1 << d->Depth;
		}

		COLOUR SelFore = Tree->Focus() ? LC_FOCUS_SEL_FORE : LC_NON_FOCUS_SEL_FORE;
		COLOUR SelBack = Tree->Focus() ? LC_FOCUS_SEL_BACK : LC_NON_FOCUS_SEL_BACK;
		for (GTreeItem *i=Items.First(); i; i=Items.Next())
		{
			bool IsSelected = (Tree->d->DropTarget == i) || (Tree->d->DropTarget == 0 && i->Select());

			// Foreground
			GCss::ColorDef Fill = i->GetCss(true)->Color();
			Ctx.Fore.Set(Fill.Type == GCss::ColorRgb ? Rgb32To24(Fill.Rgb32) : (IsSelected ? SelFore : LC_TEXT), 24);

			// Background	
			Fill = i->GetCss()->BackgroundColor();
			Ctx.Back.Set(Fill.Type == GCss::ColorRgb ?
						Rgb32To24(Fill.Rgb32) :
						(IsSelected ? SelBack : LC_WORKSPACE),
						24);

			i->OnPaint(Ctx);
		}

		Tree->d->LineFlags[0] &= ~(1 << d->Depth);
	}
}

void GTreeItem::OnPaintColumn(GItem::ItemPaintCtx &Ctx, int i, GItemColumn *c)
{
	GDisplayString *ds = d->GetDs(i, Ctx.ColPx[i]);
	if (ds)
	{
		GFont *f = ds->GetFont();
		f->Colour(Ctx.Fore, Ctx.Back);
		ds->Draw(Ctx.pDC, Ctx.x1 + 2, Ctx.y1 + 1, &Ctx);
	}
}		

//////////////////////////////////////////////////////////////////////////////
GTree::GTree(int id, int x, int y, int cx, int cy, const char *name) :
	ResObject(Res_TreeView)
{
	d = new GTreePrivate;
	SetId(id);
	GRect e(x, y, x+cx, y+cy);
	SetPos(e);
	if (name) Name(name);
	else Name("LGI.GTree");
	Sunken(true);

	Tree = this;
	Lines = true;
	Buttons = true;
	LinesAtRoot = true;
	EditLabels = false;
	MultipleSelect = false;
	ColumnHeaders = false;
	rItems.ZOff(-1, -1);

	#if WINNATIVE
	SetStyle(GetStyle() | WS_CHILD | WS_VISIBLE | WS_TABSTOP);
	#endif
	SetTabStop(true);
	LgiResources::StyleElement(this);
}

GTree::~GTree()
{
	Empty();
	DeleteObj(d);
}

// Internal tree methods
void GTree::_Update(GRect *r, bool Now)
{
	if (r)
	{
		GRect u = *r;
		GdcPt2 s = _ScrollPos();
		GRect c = GetClient();
		u.Offset(c.x1-s.x, c.y1-s.y);
		Invalidate(&u, Now && !d->InPour);
	}
	else
	{
		Invalidate((GRect*)0, Now && !d->InPour);
	}
}

void GTree::_UpdateBelow(int y, bool Now)
{
	GdcPt2 s = _ScrollPos();
	GRect c = GetClient();
	GRect u(c.x1, y - s.y + c.y1, X()-1, Y()-1);
	Invalidate(&u, Now);
}

void GTree::ClearDs(int Col)
{
	GTreeItem *n;
	List<GTreeItem>::I it = Items.Start();
	for (GTreeItem *i=*it; i; i=*++it)
	{
		i->_ClearDs(Col);
	}
}

GdcPt2 GTree::_ScrollPos()
{
	/*
	int All = _Limit.y / TREE_BLOCK - 1;
	int Visible = Y() / TREE_BLOCK;
	*/
	GdcPt2 Status;

	Status.x = (HScroll) ? (int)HScroll->Value() : 0;
	Status.y = (VScroll) ? (int)VScroll->Value() * TREE_BLOCK : 0;

	return Status;
}

void GTree::_UpdateScrollBars()
{
	static bool Processing = false;
	if (!Processing)
	{
		Processing = true;

		GdcPt2 Old = _ScrollPos();
		
		GRect Client = GetClient();
		bool x = d->Limit.x > Client.X();
		bool y = d->Limit.y > Client.Y();
		SetScrollBars(x, y);
		Client = GetClient();

		// x scroll... in pixels
		if (HScroll)
		{
			HScroll->SetLimits(0, d->Limit.x-1);
			HScroll->SetPage(Client.X());

			int Max = d->Limit.x - Client.X();
			if (HScroll->Value() > Max)
			{
				HScroll->Value(Max+1);
			}
		}

		// y scroll... in items
		if (VScroll)
		{
			int All = d->Limit.y / TREE_BLOCK;
			int Visible = Client.Y() / TREE_BLOCK;

			VScroll->SetLimits(0, All - 1);
			VScroll->SetPage(Visible);

			/*
			int Max = All - Visible + 1;
			if (VScroll->Value() > Max)
			{
				VScroll->Value(Max);
			}
			*/
		}

		GdcPt2 New = _ScrollPos();
		if (Old.x != New.x ||
			Old.y != New.y)
		{
			Invalidate();
		}

		Processing = false;
	}
}

void GTree::_OnSelect(GTreeItem *Item)
{
	if (!MultipleSelect)
	{
		for (GTreeItem *i=d->Selection.First(); i; i=d->Selection.Next())
		{
			if (i != Item)
			{
				i->Select(false);
			}
		}

		d->Selection.Empty();
	}
	else
	{
		d->Selection.Delete(Item);
	}

	d->Selection.Insert(Item);
}

void GTree::_Pour()
{
	d->InPour = true;
	d->Limit.x = rItems.x1;
	d->Limit.y = rItems.y1;

	int ColumnPx = 0;
	if (Columns.Length())
	{
		for (int i=0; i<Columns.Length(); i++)
		{
			GItemColumn *c = Columns[i];
			ColumnPx += c->Width();
			printf("c->Width()=%i\n", c->Width());
		}
	}
	else
	{
		ColumnPx = d->LastLayoutPx = GetClient().X();
		if (ColumnPx < 16)
			ColumnPx = 16;
	}

	GTreeItem *n;
	for (GTreeItem *i=Items.First(); i; i=n)
	{
		n = Items.Next();
		i->d->Last = n == 0;
		i->_Pour(&d->Limit, ColumnPx, 0, true);
	}

	_UpdateScrollBars();
	d->LayoutDirty = false;
	d->InPour = false;
}

// External methods and events
void GTree::OnItemSelect(GTreeItem *Item)
{
	if (Item)
	{
		Item->OnSelect();
		SendNotify(GNotifyItem_Select);
	}
}

void GTree::OnItemExpand(GTreeItem *Item, bool Expand)
{
	if (Item)
	{
		Item->OnExpand(Expand);
	}
}

GTreeItem *GTree::GetAdjacent(GTreeItem *i, bool Down)
{
	if (i)
	{
		if (Down)
		{
			GTreeItem *n = i->GetChild();
			
			if (!n ||
				!n->d->Visible)
			{
				for (n = i; n; )
				{
					GTreeItem *p = n->GetParent();
					if (p)
					{
						int Index = n->IndexOf();
						if (Index < p->Items.Length()-1)
						{
							n = n->GetNext();
							break;
						}
						else
						{
							n = p;
						}
					}
					else
					{
						n = n->GetNext();
						break;
					}
				}
			}

			return n;
		}
		else
		{
			GTreeItem *p = i->GetParent() ? i->GetParent() : 0;
			int Index = i->IndexOf();
			if (p)
			{
				GTreeItem *n = p;
				if (Index > 0)
				{
					n = i->GetPrev();
					while (	n->GetChild() &&
							n->GetChild()->d->Visible)
					{
						n = n->Items.ItemAt(n->Items.Length()-1);
					}
				}

				if (n)
				{
					return n;
				}
			}
			else if (Index > 0)
			{
				p = i->GetTree()->ItemAt(Index - 1);
				while (p->GetChild() &&
						p->GetChild()->d->Visible)
				{
					if (p->Items.First())
					{
						p = p->Items.ItemAt(p->Items.Length()-1);
					}
					else break;
				}
				return p;
			}
		}
	}
	
	return 0;
}

bool GTree::OnKey(GKey &k)
{
	bool Status = false;
	
	GTreeItem *i = d->Selection.First();
	if (!i)
	{
		i = Items.First();
		if (i)
		{
			i->Select();
		}
	}

	if (k.Down())
	{
		switch (k.vkey)
		{
			case VK_PAGEUP:
			case VK_PAGEDOWN:
			{
				if (i && i->d->Pos.Y() > 0)
				{
					int Page = GetClient().Y() / i->d->Pos.Y();
					for (int j=0; j<Page; j++)
					{
						GTreeItem *n = GetAdjacent(i, k.c16 == VK_PAGEDOWN);
						if (n)
						{
							i = n;
						}
						else break;
					}
					if (i)
					{
						i->Select(true);
						i->ScrollTo();
					}
				}
				Status = true;
				break;
			}
			case VK_HOME:
			{
				GTreeItem *i;
				if ((i = Items.First()))
				{
					i->Select(true);
					i->ScrollTo();
				}
				Status = true;
				break;
			}
			case VK_END:
			{
				GTreeItem *n = i, *p = 0;
				while ((n = GetAdjacent(n, true)))
				{
					p = n;
				}
				if (p)
				{
					p->Select(true);
					p->ScrollTo();
				}
				Status = true;
				break;
			}
			case VK_LEFT:
			{
				if (i)
				{
					if (i->Items.First() && i->Expanded())
					{
						i->Expanded(false);
						break;
					}
					else
					{
						GTreeItem *p = i->GetParent();
						if (p)
						{
							p->Select(true);
							p->Expanded(false);
							_Pour();
							break;
						}
					}
				}
				// fall thru
			}
			case VK_UP:
			{
				GTreeItem *n = GetAdjacent(i, false);
				if (n)
				{
					n->Select(true);
					n->ScrollTo();
				}
				Status = true;
				break;
			}
			case VK_RIGHT:
			{
				if (i)
				{
					i->Expanded(true);
					if (d->LayoutDirty)
					{
						_Pour();
						break;
					}
				}
				// fall thru
			}
			case VK_DOWN:
			{
				GTreeItem *n = GetAdjacent(i, true);
				if (n)
				{
					n->Select(true);
					n->ScrollTo();
				}
				Status = true;
				break;
			}
			#ifdef VK_APPS
			case VK_APPS:
			{
				GTreeItem *s = Selection();
				if (s)
				{
					GRect *r = &s->d->Text;
					if (r)
					{
						GMouse m;
						m.x = r->x1 + (r->X() >> 1);
						m.y = r->y1 + (r->Y() >> 1);
						m.Target = this;
						m.ViewCoords = true;
						m.Down(true);
						m.Right(true);
						
						s->OnMouseClick(m);
					}
				}
				break;
			}
			#endif
			default:
			{
				// LgiTrace("Key c16=%i, down=%i\n", k.c16, k.Down());
				break;
			}
		}
	}

	if (i && i != (GTreeItem*)this)
	{
		i->OnKey(k);
	}

	return Status;
}

GTreeItem *GTree::ItemAtPoint(int x, int y, bool Debug)
{
	GdcPt2 s = _ScrollPos();

	for (GTreeItem *i = Items.First(); i; i=Items.Next())
	{
		GTreeItem *Hit = i->_HitTest(s.x + x, s.y + y, Debug);
		if (Hit)
		{
			return Hit;
		}
	}

	return 0;
}

bool GTree::OnMouseWheel(double Lines)
{
	if (VScroll)
	{
		VScroll->Value(VScroll->Value() + (int)Lines);
	}
	
	return true;
}

void GTree::OnMouseClick(GMouse &m)
{
	if (m.Down())
	{
		DragMode = DRAG_NONE;
		if (ColumnHeaders &&
			ColumnHeader.Overlap(m.x, m.y))
		{
			d->DragStart.x = m.x;
			d->DragStart.y = m.y;

			// Clicked on a column heading
			GItemColumn *Resize, *Over;
			int Index = HitColumn(m.x, m.y, Resize, Over);

			if (Resize)
			{
				if (m.Double())
				{
					Resize->Width(Resize->GetContentSize() + DEFAULT_COLUMN_SPACING);
					Invalidate();
				}
				else
				{
					DragMode = RESIZE_COLUMN;
					d->DragData = Columns.IndexOf(Resize);
					Capture(true);
				}
			}
			/*
			else
			{
				DragMode = CLICK_COLUMN;
				d->DragData = Columns.IndexOf(Over);
				if (Over)
				{
					Over->Value(true);
					GRect r = Over->GetPos();
					Invalidate(&r);
					Capture(true);
				}
			}
			*/
		}
		else if (rItems.Overlap(m.x, m.y))
		{
			Focus(true);
			Capture(true);
			d->LastClick.x = m.x;
			d->LastClick.y = m.y;

			d->LastHit = ItemAtPoint(m.x, m.y, true);
			if (d->LastHit)
			{
				GdcPt2 c = _ScrollPos();
				m.x += c.x;
				m.y += c.y;
				d->LastHit->_MouseClick(m);
			}
		}
	}
	else if (IsCapturing())
	{
		Capture(false);

		if (rItems.Overlap(m.x, m.y))
		{
			d->LastClick.x = m.x;
			d->LastClick.y = m.y;

			d->LastHit = ItemAtPoint(m.x, m.y);
			if (d->LastHit)
			{
				GdcPt2 c = _ScrollPos();
				m.x += c.x;
				m.y += c.y;
				d->LastHit->_MouseClick(m);
			}
		}
	}
}

void GTree::OnMouseMove(GMouse &m)
{
	if (!IsCapturing())
		return;

	switch (DragMode)
	{
		/*
		case DRAG_COLUMN:
		{
			if (DragCol)
			{
				GdcPt2 p;
				PointToScreen(p);

				GRect r = DragCol->GetPos();
				r.Offset(-p.x, -p.y); // to view co-ord

				r.Offset(m.x - DragCol->GetOffset() - r.x1, 0);
				if (r.x1 < 0) r.Offset(-r.x1, 0);
				if (r.x2 > X()-1) r.Offset((X()-1)-r.x2, 0);

				r.Offset(p.x, p.y); // back to screen co-ord
				DragCol->SetPos(r, true);
				r = DragCol->GetPos();
			}
			break;
		}
		*/
		case RESIZE_COLUMN:
		{
			GItemColumn *c = Columns[d->DragData];
			if (c)
			{
				int OldWidth = c->Width();
				int NewWidth = m.x - c->GetPos().x1;

				c->Width(max(NewWidth, 4));
				_ClearDs(d->DragData);
				Invalidate();
			}
			break;
		}
		default:
		{
			if (rItems.Overlap(m.x, m.y))
			{
				if (abs(d->LastClick.x - m.x) > DRAG_THRESHOLD ||
					abs(d->LastClick.y - m.y) > DRAG_THRESHOLD)
				{
					OnItemBeginDrag(d->LastHit, m.Flags);
					Capture(false);
				}
			}
			break;
		}
	}
}

void GTree::OnPosChange()
{
	if (Columns.Length() == 0 &&
		d->LastLayoutPx != GetClient().X())
		d->LayoutDirty = true;
	GLayout::OnPosChange();
	_UpdateScrollBars();
}

void GTree::OnPaint(GSurface *pDC)
{
	#if 0 // coverage testing...
	pDC->Colour(GColour(255, 0, 255));
	pDC->Rectangle();
	#endif

	rItems = GetClient();
	GFont *f = GetFont();
	if (ShowColumnHeader())
	{
		ColumnHeader.ZOff(rItems.X()-1, f->GetHeight() + 4);
		PaintColumnHeadings(pDC);
		rItems.y1 = ColumnHeader.y2 + 1;
	}
	else
	{
		ColumnHeader.ZOff(-1, -1);
	}

	d->IconTextGap = GetFont()->GetHeight() / 6;

	// icon cache
	if (GetImageList() &&
		!d->IconCache)
	{
		int CacheHeight = max(SysFont->GetHeight(), GetImageList()->Y());
		
		d->IconCache = new GMemDC;
		if (d->IconCache &&
			d->IconCache->Create(GetImageList()->X(), CacheHeight, GdcD->GetColourSpace()))
		{
			if (d->IconCache->GetColourSpace() == CsIndex8)
			{
				d->IconCache->Palette(new GPalette(GdcD->GetGlobalColour()->GetPalette()));
			}

			GColour Background(LC_WORKSPACE, 24);
			d->IconCache->Colour(Background);
			d->IconCache->Rectangle();
			d->IconCache->Op(GDC_ALPHA);

			GetImageList()->Lock();
			int DrawY = (CacheHeight - GetImageList()->TileY()) >> 1;
			LgiAssert(DrawY >= 0);
			for (int i=0; i<GetImageList()->GetItems(); i++)
			{
				GetImageList()->Draw(d->IconCache, i * GetImageList()->TileX(), DrawY, i, Background);
			}
			GetImageList()->Unlock();
			d->IconCache->Unlock();
		}
	}

	// scroll
	GdcPt2 s = _ScrollPos();
	int Ox, Oy;
	pDC->GetOrigin(Ox, Oy);
	pDC->SetOrigin(Ox + s.x, Oy + s.y);

	// selection colour
	GArray<int> ColPx;
	GItem::ItemPaintCtx Ctx;
	Ctx.pDC = pDC;
	if (Columns.Length() > 0)
	{
		Ctx.Columns = Columns.Length();
		for (int i=0; i<Columns.Length(); i++)
			ColPx[i] = Columns[i]->Width();
	}
	else
	{
		Ctx.Columns = 1;
		ColPx[0] = rItems.X();
	}
	Ctx.ColPx = &ColPx[0];	
	COLOUR SelFore = Focus() ? LC_FOCUS_SEL_FORE : LC_NON_FOCUS_SEL_FORE;
	COLOUR SelBack = Focus() ? LC_FOCUS_SEL_BACK : LC_NON_FOCUS_SEL_BACK;

	// layout items
	if (d->LayoutDirty)
	{
		_Pour();
	}

	// paint items
	ZeroObj(d->LineFlags);
	for (GTreeItem *i = Items.First(); i; i=Items.Next())
	{
		bool IsSelected = (d->DropTarget == i) || (d->DropTarget == 0 && i->Select());

		// Foreground
		GCss::ColorDef Fill = i->GetCss(true)->Color();
		Ctx.Fore.Set(Fill.Type == GCss::ColorRgb ? Rgb32To24(Fill.Rgb32) : (IsSelected ? SelFore : LC_TEXT), 24);

		// Background	
		Fill = i->GetCss()->BackgroundColor();
		Ctx.Back.Set(Fill.Type == GCss::ColorRgb ? Rgb32To24(Fill.Rgb32) : (IsSelected ? SelBack : LC_WORKSPACE), 24);

		i->OnPaint(Ctx);
	}

	pDC->SetOrigin(Ox, Oy);
	if (d->Limit.y-s.y < rItems.Y())
	{
		// paint after items
		pDC->Colour(LC_WORKSPACE, 24);
		pDC->Rectangle(rItems.x1, d->Limit.y - s.y, rItems.x2, rItems.y2);
	}
}

int GTree::OnNotify(GViewI *Ctrl, int Flags)
{
	switch (Ctrl->GetId())
	{
		case IDC_HSCROLL:
		case IDC_VSCROLL:
		{
			Invalidate();
			break;
		}
	}

	return GLayout::OnNotify(Ctrl, Flags);
}

GMessage::Result GTree::OnEvent(GMessage *Msg)
{
	return GLayout::OnEvent(Msg);
}

GTreeItem *GTree::Insert(GTreeItem *Obj, int Pos)
{
	GTreeItem *NewObj = GTreeNode::Insert(Obj, Pos);
	if (NewObj)
	{
		NewObj->_SetTreePtr(this);
	}

	return NewObj;
}

bool GTree::Remove(GTreeItem *Obj)
{
	if (Obj &&
		Obj->Tree == this)
	{
		Obj->Remove();
		return true;
	}
	return false;
}

void GTree::RemoveAll()
{
	for (GTreeItem *i=Items.First(); i; i=Items.First())
	{
		i->_Remove();
	}

	Invalidate();
}

void GTree::Empty()
{
	GTreeItem *i;
	while ((i = Items.First()))
	{
		Delete(i);
	}
}

bool GTree::Delete(GTreeItem *Obj)
{
	bool Status = false;
	
	if (Obj)
	{
		GTreeItem *i;
		while ((i = Obj->Items.First()))
		{
			Delete(i);
		}
		
		Obj->Remove();
		DeleteObj(Obj);
		Status = true;
	}
	
	return Status;
}

void GTree::OnPulse()
{
	if (d->DropTarget)
	{
		int64 p = LgiCurrentTime() - d->DropSelectTime;
		if (p >= 1000)
		{
			SetPulse();

			if (!d->DropTarget->Expanded() &&
				d->DropTarget->GetChild())
			{
				d->DropTarget->Expanded(true);
			}
		}
	}	

	if (InsideDragOp())
	{
		GMouse m;
		if (GetMouse(m))
		{
			GRect c = GetClient();
			if (VScroll)
			{
				if (m.y < DRAG_SCROLL_EDGE)
				{
					// Scroll up...
					VScroll->Value(VScroll->Value() - DRAG_SCROLL_Y);
				}
				else if (m.y > c.Y() - DRAG_SCROLL_EDGE)
				{
					// Scroll down...
					VScroll->Value(VScroll->Value() + DRAG_SCROLL_Y);
				}
			}

			if (HScroll)
			{
				if (m.x < DRAG_SCROLL_EDGE)
				{
					// Scroll left...
					HScroll->Value(HScroll->Value() - DRAG_SCROLL_X);
				}
				else if (m.x > c.X() - DRAG_SCROLL_EDGE)
				{
					// Scroll right...
					HScroll->Value(HScroll->Value() + DRAG_SCROLL_X);
				}
			}
		}
	}
}

int GTree::GetContentSize(int ColumnIdx)
{
	int MaxPx = 0;
	
	for (GTreeItem *i = Items.First(); i; i=Items.Next())
	{
		int ItemPx = i->GetColumnSize(ColumnIdx);
		MaxPx = max(ItemPx, MaxPx);
	}
	
	return MaxPx;
}

LgiCursor GTree::GetCursor(int x, int y)
{
	GItemColumn *Resize, *Over;
	HitColumn(x, y, Resize, Over);
	if (Resize)
		return LCUR_SizeHor;
	
	return LCUR_Normal;
}

void GTree::OnDragEnter()
{
	InsideDragOp(true);
	SetPulse(120);
}

void GTree::OnDragExit()
{
	InsideDragOp(false);
	SetPulse();
	SelectDropTarget(0);
}

void GTree::SelectDropTarget(GTreeItem *Item)
{
	if (Item != d->DropTarget)
	{
		bool Update = (d->DropTarget != 0) ^ (Item != 0);
		GTreeItem *Old = d->DropTarget;
		
		d->DropTarget = Item;
		if (Old)
		{
			Old->Update();
		}

		if (d->DropTarget)
		{
			d->DropTarget->Update();
			d->DropSelectTime = LgiCurrentTime();
		}

		if (Update)
		{
			OnFocus(true);
		}
	}
}

bool GTree::Select(GTreeItem *Obj)
{
	bool Status = false;
	if (Obj && IsAttached())
	{
		Obj->Select(true);
		Status = true;
	}
	else if (d->Selection.Length())
	{		
		d->Selection.Empty();
		OnItemSelect(0);
		Status = true;
	}
	return Status;
}

GTreeItem *GTree::Selection()
{
	return d->Selection.First();
}

void GTree::OnItemClick(GTreeItem *Item, GMouse &m)
{
	if (Item)
	{
		Item->OnMouseClick(m);
	}
}

void GTree::OnItemBeginDrag(GTreeItem *Item, int Flags)
{
	if (Item)
	{
		GMouse m;
		m.x = m.y = 0;
		m.Target = NULL;
		m.ViewCoords = false;
		m.Flags = Flags;
		Item->OnBeginDrag(m);
	}
}

void GTree::OnFocus(bool b)
{
	// errors during deletion of the control can cause 
	// this to be called after the destructor
	if (d)
	{
		for (GTreeItem *i=d->Selection.First(); i; i=d->Selection.Next())
		{
			i->Update();
		}
	}
}

static void GTreeItemUpdateAll(GTreeNode *n)
{
	for (GTreeItem *i=n->GetChild(); i; i=i->GetNext())
	{
		i->Update();
		GTreeItemUpdateAll(i);
	}
}

void GTree::UpdateAllItems()
{
	d->LayoutDirty = true;
	GTreeItemUpdateAll(this);
}

void GTree::SetVisualStyle(ThumbStyle Btns, bool JoiningLines)
{
	d->Btns = Btns;
	d->JoiningLines = JoiningLines;
	Invalidate();
}

