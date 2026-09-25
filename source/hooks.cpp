// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.

#include "hooks.h"
#include <cstring>

bool WriteMemory(void* address, const void* data, size_t size)
{
	DWORD old = 0;
	if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old))
		return false;
	memcpy(address, data, size);
	VirtualProtect(address, size, old, &old);
	FlushInstructionCache(GetCurrentProcess(), address, size);
	return true;
}

// ---------------------------------------------------------------------------------------------

bool MethodRedirect::Install(void* object)
{
	if (!object)
		return false;
	void** table = *reinterpret_cast<void***>(object);
	for (int i = 0; i < m_count; i++)
		if (m_tables[i] == table)
			return true;
	if (table[m_index] == m_hook)
		return true; // reached through another object whose table we already hold
	if (m_count == kMaxTables)
	{
		Log("Redirect: more than %d method tables for slot %d, not redirected\n", kMaxTables, m_index);
		return false;
	}
	void* original = table[m_index];
	if (!WriteMemory(&table[m_index], &m_hook, sizeof(void*)))
		return false;
	m_tables[m_count] = table;
	m_originals[m_count] = original;
	m_count++;
	return true;
}

bool MethodRedirect::Reclaim(void* object)
{
	if (!object)
		return false;
	void** table = *reinterpret_cast<void***>(object);
	for (int i = 0; i < m_count; i++)
	{
		if (m_tables[i] != table)
			continue;
		void* current = table[m_index];
		if (current == m_hook)
			return false;
		// Direct3D itself does this: after the first text drawn by D3DX, the system d3d9.dll
		// writes its own Present back into the table (measured 2026-09-25), and from then on every
		// frame went past the fix. What is found there becomes the original we call.
		if (!WriteMemory(&table[m_index], &m_hook, sizeof(void*)))
			return false;
		m_originals[i] = current;
		return true;
	}
	return false;
}

void* MethodRedirect::Lookup(const void* object) const
{
	void** table = *reinterpret_cast<void** const*>(object);
	for (int i = 0; i < m_count; i++)
		if (m_tables[i] == table)
			return m_originals[i];
	return m_count ? m_originals[0] : nullptr;
}

// ---------------------------------------------------------------------------------------------

static IMAGE_NT_HEADERS* NtHeaders(HMODULE module)
{
	auto* base = reinterpret_cast<BYTE*>(module);
	auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return nullptr;
	auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	return nt->Signature == IMAGE_NT_SIGNATURE ? nt : nullptr;
}

void* RedirectImport(HMODULE module, const char* dll, const char* function, void* hook)
{
	if (!module)
		return nullptr;
	IMAGE_NT_HEADERS* nt = NtHeaders(module);
	if (!nt)
		return nullptr;
	const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress)
		return nullptr;

	auto* base = reinterpret_cast<BYTE*>(module);
	// Where the function really is: entries bound without a name table are recognised by address.
	HMODULE target = GetModuleHandleA(dll);
	void* resolved = target ? reinterpret_cast<void*>(GetProcAddress(target, function)) : nullptr;

	for (auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; desc++)
	{
		if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), dll) != 0)
			continue;
		auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
		auto* names = desc->OriginalFirstThunk
			? reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk) : nullptr;
		for (int i = 0; slots[i].u1.Function; i++)
		{
			bool match = false;
			if (names && !IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal))
			{
				auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names[i].u1.AddressOfData);
				match = strcmp(reinterpret_cast<const char*>(byName->Name), function) == 0;
			}
			else if (resolved)
			{
				match = reinterpret_cast<void*>(slots[i].u1.Function) == resolved;
			}
			if (!match)
				continue;
			void* previous = reinterpret_cast<void*>(slots[i].u1.Function);
			if (previous == hook)
				return resolved; // already ours
			if (!WriteMemory(&slots[i].u1.Function, &hook, sizeof(void*)))
				return nullptr;
			return previous;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------------------------

BYTE* FindPattern(HMODULE module, const BYTE* pattern, size_t size, const char* mask)
{
	IMAGE_NT_HEADERS* nt = NtHeaders(module);
	if (!nt || !size)
		return nullptr;
	auto* base = reinterpret_cast<BYTE*>(module);
	IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
	for (WORD s = 0; s < nt->FileHeader.NumberOfSections; s++, section++)
	{
		if (!(section->Characteristics & IMAGE_SCN_MEM_READ))
			continue;
		BYTE* start = base + section->VirtualAddress;
		const size_t length = section->Misc.VirtualSize;
		if (length < size)
			continue;
		for (size_t i = 0; i + size <= length; i++)
		{
			size_t k = 0;
			while (k < size && ((mask && mask[k] == '?') || start[i + k] == pattern[k]))
				k++;
			if (k == size)
				return start + i;
		}
	}
	return nullptr;
}
