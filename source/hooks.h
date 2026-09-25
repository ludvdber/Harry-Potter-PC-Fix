// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// The three ways this fix reaches into the game: method tables of COM objects, import tables
// of modules, and bytes of the game executable.

#pragma once
#include "accio.h"

// ---------------------------------------------------------------------------------------------
// COM method redirection.
//
// A COM object starts with a pointer to its method table, shared by every object of the same
// class. Replacing one slot sends every call of that method, from any object of the class, to
// our function; the game keeps the object Direct3D gave it. Two classes can serve the same
// interface (a device and an Ex device, say), each with its own table and its own original, so
// the originals are kept per table and looked up from the object at call time.
// ---------------------------------------------------------------------------------------------
class MethodRedirect
{
public:
	MethodRedirect(int index, void* hook) : m_index(index), m_hook(hook) {}

	// Idempotent: a table already redirected is left alone.
	bool Install(void* object);

	template <class Fn>
	Fn Original(const void* object) const { return reinterpret_cast<Fn>(Lookup(object)); }

private:
	void* Lookup(const void* object) const;

	static constexpr int kMaxTables = 4;
	int    m_index;
	void*  m_hook;
	void** m_tables[kMaxTables] = {};
	void*  m_originals[kMaxTables] = {};
	int    m_count = 0;
};

// Our own calls into Direct3D (post-processing, screenshots, the frame counter overlay) must not
// be taken for the game's: they would be counted, rescaled or post-processed a second time.
struct InternalCalls
{
	InternalCalls() { ++g_internal; }
	~InternalCalls() { --g_internal; }
	InternalCalls(const InternalCalls&) = delete;
	InternalCalls& operator=(const InternalCalls&) = delete;
};

// ---------------------------------------------------------------------------------------------
// Import redirection: `module` calls `dll!function` through its import table; the entry is
// pointed at `hook`. Returns the previous target, or nullptr if the module does not import it.
// ---------------------------------------------------------------------------------------------
void* RedirectImport(HMODULE module, const char* dll, const char* function, void* hook);

// ---------------------------------------------------------------------------------------------
// Executable bytes.
// ---------------------------------------------------------------------------------------------
bool WriteMemory(void* address, const void* data, size_t size);

// First occurrence of `pattern` in the readable sections of `module`. `mask` may be null; when
// given, '?' marks a byte that matches anything.
BYTE* FindPattern(HMODULE module, const BYTE* pattern, size_t size, const char* mask = nullptr);
