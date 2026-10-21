// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// One slot of a COM method table pointed at our function, Windows' (or the game's) one kept by
// table: a table is shared by every object of its class, so it is done once per table and later
// objects find our function there. Two classes can serve the same interface, each with its own
// table and its own original (dinput8.cpp: nothing says the mouse's table is the pad's).
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

void AccioLog(const char* fmt, ...);

template <class Fn> class Redirect
{
public:
	// The original of the table `object` uses, or nullptr if that table was never redirected.
	Fn Original(const void* object) const
	{
		void** table = *static_cast<void** const*>(object);
		for (int i = 0; i < m_count; ++i)
			if (m_tables[i] == table)
				return m_originals[i];
		return nullptr;
	}

	bool Install(void* object, int slot, Fn ours, const char* name)
	{
		void** table = *static_cast<void***>(object);
		if (table[slot] == reinterpret_cast<void*>(ours))
			return true;
		DWORD protect;
		if (m_count == kTables || !VirtualProtect(&table[slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &protect))
		{
			AccioLog("%s: not redirected (error %lu)\n", name, m_count == kTables ? 0ul : GetLastError());
			return false;
		}
		m_tables[m_count] = table;
		m_originals[m_count] = reinterpret_cast<Fn>(table[slot]);
		++m_count;
		table[slot] = reinterpret_cast<void*>(ours);
		VirtualProtect(&table[slot], sizeof(void*), protect, &protect);
		AccioLog("%s: watched\n", name);
		return true;
	}

private:
	static constexpr int kTables = 4;
	void** m_tables[kTables] = {};
	Fn m_originals[kTables] = {};
	int m_count = 0;
};
