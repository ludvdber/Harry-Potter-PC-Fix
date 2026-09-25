// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.

#include "accio.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

static FILE* g_file = nullptr;
static CRITICAL_SECTION g_lock;

void OpenLog(HMODULE self)
{
	InitializeCriticalSection(&g_lock);
	char path[MAX_PATH];
	GetModuleFileNameA(self, path, MAX_PATH);
	char* slash = strrchr(path, '\\');
	strcpy_s(slash ? slash + 1 : path, MAX_PATH - (slash ? slash + 1 - path : 0), "d3d9_accio.log");
	fopen_s(&g_file, path, "w");
}

void Log(const char* fmt, ...)
{
	if (!g_file)
		return;
	EnterCriticalSection(&g_lock);
	fprintf(g_file, "[%llu] ", GetTickCount64());
	va_list args;
	va_start(args, fmt);
	vfprintf(g_file, fmt, args);
	va_end(args);
	fflush(g_file);
	LeaveCriticalSection(&g_lock);
}
