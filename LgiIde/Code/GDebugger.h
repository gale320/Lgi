#ifndef _GDEBUGGER_H_
#define _GDEBUGGER_H_

#include "GVariant.h"
#include "GStringClass.h"

class GDebugEvents : public GStream
{
public:
	virtual ~GDebugEvents() {}
	
	virtual void OnState(bool Debugging, bool Running) = 0;
	virtual void OnFileLine(const char *File, int Line, bool CurrentIp) = 0;
	virtual void OnError(int Code, const char *Str) = 0;
	virtual void OnCrash(int Code) = 0;
};

class GDebugger
{
public:
	struct BreakPoint
	{
		int Index;
		GString File;
		int Line;
		bool Added;
		
		BreakPoint()
		{
			Index = 0;
			Line = 0;
			Added = false;
		}
		
		BreakPoint &operator =(const BreakPoint &b)
		{
			Index = b.Index;
			File = b.File;
			Line = b.Line;
			return *this;
		}
		
		bool operator ==(const BreakPoint &b)
		{
			if (File == b.File &&
				Line == b.Line)
				return true;
			
			return false;
		}
	};

	struct Variable
	{
		enum ScopeType
		{
			Local,
			Arg,
			Global
		}	Scope;
		
		GAutoString Name;
		GAutoString Type;
		GVariant Value;
	};
	
	virtual ~GDebugger() {}
	
	virtual bool Load(GDebugEvents *EventHandler, const char *Exe, const char *Args, const char *InitDir) = 0;
	virtual bool Restart() = 0;
	virtual bool Unload() = 0;
	
	virtual bool GetCallStack(GArray<GAutoString> &Stack) = 0;
	virtual bool GetFrame(int &Frame, GAutoString &File, int &Line) = 0;
	virtual bool SetFrame(int Frame) = 0;

	virtual bool SetBreakPoint(BreakPoint *bp) = 0;
	virtual bool RemoveBreakPoint(BreakPoint *bp) = 0;
	virtual bool GetBreakPoints(GArray<BreakPoint> &bps) = 0;

	virtual bool GetVariables(bool Locals, GArray<Variable> &vars, bool Detailed) = 0;
	virtual bool PrintObject(const char *Var, GStream *Output) = 0;
	virtual bool ReadMemory(NativeInt Addr, int Length, GArray<uint8> &OutBuf) = 0;
	virtual bool GetRegisters(GStream *Out) = 0;

	virtual bool GetLocation(GAutoString &File, int &Line) = 0;
	virtual bool SetLocation(const char *File, int Line) = 0;

	virtual bool GetRunning() = 0;
	virtual bool SetRunning(bool Run) = 0;
	virtual bool StepInto() = 0;
	virtual bool StepOver() = 0;
	virtual bool StepOut() = 0;
	virtual bool Break() = 0;
	
	virtual bool UserCommand(const char *Cmd) = 0;
};

extern GDebugger *CreateGdbDebugger();

#endif