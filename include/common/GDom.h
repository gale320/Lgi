/**
	\file
	\author Matthew Allen
	\brief Document object model class.\n
	Copyright (C), <a href="mailto:fret@memecode.com">Matthew Allen</a>
 */

#ifndef _GDOM_H_
#define _GDOM_H_

class GVariant;
#include "LgiInterfaces.h"
#include "GMem.h"
#include "GArray.h"

/// API for reading and writing properties in objects.
class LgiClass GDom : virtual public GDomI
{
	friend class GScriptEval;
	friend class GScriptEnginePrivate;
	friend struct GDomRef;
	friend class GVirtualMachinePriv;

protected:
	GDom *ResolveObject(const char *Var, char *Name, char *Array);

	virtual bool _OnAccess(bool Start) { return true; }
	virtual bool GetVariant(const char *Name, GVariant &Value, char *Array = 0) { return false; }
	virtual bool SetVariant(const char *Name, GVariant &Value, char *Array = 0) { return false; }

public:
	/// Gets an object's property
	bool GetValue
	(
		/// The string describing the property
		const char *Var,
		/// The value returned
		GVariant &Value
	);
	
	/// Sets an object's property
	bool SetValue
	(
		/// The string describing the property
		const char *Var,
		/// The value to set the property to
		GVariant &Value
	);
};

/// This is a global map between strings and enum values for fast lookup of
/// object properties inside the SetVariant, GetVariant and CallMethod handlers.
enum GDomProperty
{
	ObjNone,
	
	// Common
	ObjLength,
	ObjType,
	ObjName,
	ObjStyle,
	
	// Types
	TypeList,
	TypeHashTable,
	TypeFile,
	TypeSurface,
	TypeDateTime,

	// GDateTime
	DateNone,
	DateYear,
	DateMonth,
	DateDay,
	DateHour,
	DateMin,
	DateSec,
	DateDate, // "yyyymmdd"
	DateTime, // "hhmmss"
	DateDateTime, // "yyyymmdd hhmmss"
	DateInt64,
	DateSetNow,

	// GVariant string
	StrJoin,
	StrSplit,
	StrFind,
	StrRfind,
	StrLower,
	StrUpper,
	StrStrip,
	StrInt,
	StrDouble,
	StrSub,
	
	// GSurface
	SurfaceX,
	SurfaceY,
	SurfaceBits,
	SurfaceColourSpace,
	SurfaceIncludeCursor,
	
	// List, GHashTbl
	ContainerAdd,
	ContainerDelete,
	ContainerHasKey,
	
	// GFile
	FileOpen,
	FileRead,
	FileWrite,
	FilePos,
	FileClose,
	FileModified,
	FileFolder,
	FileEncoding,
};

LgiFunc GDomProperty GStringToProp(const char *Str);

#endif
