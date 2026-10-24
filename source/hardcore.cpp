// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Hard mode for HP6 (and its two damage factors for HP5, see DamageScaleHP5), switched on and off
// while the game runs: [Accio.Hardcore] in d3d9.ini,
// read again within a second of the file changing (the launcher writes it, the game keeps going).
//   DamageTaken = what Harry receives is multiplied by this (2 = twice as hard)
//   DamageDealt = what Harry's spells do to others is multiplied by this (0.5 = foes last twice as long)
// 1 and 1 leave the game exactly as shipped.
//
// Every hit of hp6.exe goes through one routine (0x5CA7C0, read 2026-10-23 in the decompiled
// executable): target entity in eax, damage as a float on the stack. It picks the target's health
// component, multiplies the damage by the fight's own factor when the target is the player (the
// Damage of the zone's HybridSpellStatsProfile: 2.7 against Fenrir), then calls the health
// component (0x595070), which subtracts it and dates the hit for regeneration. The player is the
// pointer at 0xD35CE8 (the game's own getter at 0x4082B0 returns [0xD354F0 + 0x7F8], and the
// routine compares its target with it). The routine's first three instructions (push ebp ;
// mov ebp, esp ; and esp, -16: six bytes, no relative address) become a jump to DamageScale,
// which scales the stacked damage, replays them and jumps back.
//
//   EnemyReaction = foes wait less and dodge or shield more (2 = waits halved), for foes met after the change
// Every HybridCombatAI gets its defaults from 0x4140D0 (passed to the class registration at 0x6FC2E0):
// the 49 foes of the zones set none of these fields, so these are the values every fight uses
// (strafe wait 2 s, cover wait 2 s, attack wait in cover 0.5 s, dodge Stupefy 0, dodge Expelliarmus 0.7,
// Protego against Stupefy 0). Its final ret and the four int3 after it (five bytes) become a jump to
// ReactScale, which lets EnemyReact adjust the new foe and returns in its place.
//
//   RemovedSpells = spells Harry cannot cast even once learned (Protego, Expelliarmus...), empty for none
// Whether Harry knows a spell is asked of one routine (0xFEFEA0, reached through the jump at 0x56ECE0
// by its 13 callers, read 2026-10-24): spell number in eax, Harry on the stack, answers in al from the
// mask at [Harry + 0x2C40] that 0x56E800 rebuilds from the save flags SpellUnlock_* (state 1 = learned).
// The jump is pointed at SpellCheck, which answers "no" for a removed spell and hands the rest to the
// game. The flags themselves are left alone: the save still holds every spell learned, and emptying
// the key gives them back at once.

#include "accio.h"
#include "hooks.h"
#include <cstdlib>
#include <cstring>

namespace
{
float g_taken = 1.0f;
float g_dealt = 1.0f;
float g_react = 1.0f;
BYTE* g_back = nullptr;
volatile LONG g_hitsTaken = 0, g_hitsDealt = 0, g_foes = 0;   // hits and foes seen, for the log
LONG g_saidTaken = 0, g_saidDealt = 0, g_saidFoes = 0;
char g_ini[MAX_PATH];
FILETIME g_iniTime = {};
DWORD g_lastLook = 0;
volatile LONG g_removed = 0;          // bit n = spell number n refused
BYTE* g_spellBody = nullptr;          // the game's own answer
volatile LONG g_refused = 0;
LONG g_saidRefused = 0;

// The spell numbers of hp6.exe, as 0x56E800 pairs them with the SpellUnlock_* flags.
const struct { const char* name; int number; } kSpells[] = {
	{ "Depulso", 0 }, { "Accio", 1 }, { "Leviosa", 2 }, { "Incendio", 3 }, { "Reparo", 5 }, { "Lumos", 6 },
	{ "Protego", 7 }, { "Expelliarmus", 8 }, { "Levicorpus", 9 }, { "Petrificus", 11 }, { "Stupefy", 12 },
	{ "LeviosaThrow", 15 },
};

// HP5 (hp.exe): every loss of health goes through 0x602750 (read 2026-10-24; its one caller is the hit
// handler 0x5DF5C0): target entity in eax, damage as a float on the stack, ret 4. It only takes health in
// the duelling states of [0xC7B200] (4, 11, 12, 13). The controlled character is the pointer at 0xC1FC14;
// the foes of a fight are the members of the group the fight starts with, which 0x601CD0 marks with side 2
// in the component the entity's class names at byte +0x35 of its descriptor ([0xC5D310 + class * 4],
// component offsets at [descriptor + 0x10] + index * 8). Allies (side 1, not controlled) keep the
// damage as shipped. The first three instructions (push esi ; mov esi, eax ; test esi, esi: five bytes,
// nothing relative) become the jump; the jne after them reads the flags the replayed test leaves.
__declspec(naked) void DamageScaleHP5()
{
	__asm {
		test eax, eax
		jz replay
		cmp eax, dword ptr ds:[0C1FC14h]
		jne foe
		lock inc dword ptr [g_hitsTaken]
		fld dword ptr [esp + 4]
		fmul dword ptr [g_taken]
		fstp dword ptr [esp + 4]
		jmp replay
	foe:
		push ecx
		push edx
		mov ecx, dword ptr [eax + 50h]
		mov ecx, dword ptr [ecx * 4 + 0C5D310h]
		movzx edx, byte ptr [ecx + 35h]
		cmp edx, 0FFh
		je done
		mov ecx, dword ptr [ecx + 10h]
		mov ecx, dword ptr [ecx + edx * 8]
		cmp dword ptr [ecx + eax + 4], 2
		jne done
		lock inc dword ptr [g_hitsDealt]
		fld dword ptr [esp + 12]           // two registers pushed above the damage
		fmul dword ptr [g_dealt]
		fstp dword ptr [esp + 12]
	done:
		pop edx
		pop ecx
	replay:
		push esi
		mov esi, eax
		test esi, esi
		jmp dword ptr [g_back]
	}
}

__declspec(naked) void SpellCheck()
{
	__asm {
		cmp eax, 31
		ja keep                            // unsigned: a negative number goes to the game too
		bt dword ptr [g_removed], eax
		jnc keep
		lock inc dword ptr [g_refused]
		xor eax, eax
		ret 4
	keep:
		jmp dword ptr [g_spellBody]
	}
}

__declspec(naked) void DamageScale()
{
	__asm {
		test eax, eax
		jz replay
		cmp eax, dword ptr ds:[0D35CE8h]   // the player (a named constant here would be read as a variable)
		jne dealt
		lock inc dword ptr [g_hitsTaken]
		fld dword ptr [esp + 4]
		fmul dword ptr [g_taken]
		fstp dword ptr [esp + 4]
		jmp replay
	dealt:
		lock inc dword ptr [g_hitsDealt]
		fld dword ptr [esp + 4]
		fmul dword ptr [g_dealt]
		fstp dword ptr [esp + 4]
	replay:
		push ebp
		mov ebp, esp
		and esp, 0FFFFFFF0h
		jmp dword ptr [g_back]
	}
}

void __cdecl EnemyReact(BYTE* ai)
{
	InterlockedIncrement(&g_foes);
	const float r = g_react;
	if (r == 1.0f)
		return;
	for (const int wait : { 0x74, 0x7C, 0x80 })   // NoCover_StrafeWaitTime, Cover_WaitTime, Cover_AttackWaitTime
		*reinterpret_cast<float*>(ai + wait) /= r;
	// ReactivelyDodgeStupefyChance, ReactivelyDodgeDisarmChance, ReactivelyProtegoStupefyChance. Two of them
	// ship at 0, so a product would leave them there: above 1, part of what is missing to 1 is added
	// (x2: 0 -> 0.3, 0.7 -> 0.79) ; below 1, they shrink with r.
	for (const int chance : { 0x84, 0x88, 0x8C })
	{
		float& c = *reinterpret_cast<float*>(ai + chance);
		c = r > 1.0f ? c + (1.0f - c) * (1.0f - 1.0f / r) * 0.6f : c * r;
	}
}

__declspec(naked) void ReactScale()
{
	__asm {
		pushad
		push eax   // the new HybridCombatAI, as the defaults routine had it
		call EnemyReact
		add esp, 4
		popad
		ret
	}
}

float Factor(const char* key)
{
	char buf[32];
	GetPrivateProfileStringA("Accio.Hardcore", key, "1", buf, sizeof(buf), g_ini);
	const float v = static_cast<float>(atof(buf));
	return v < 0.1f ? 0.1f : v > 10.0f ? 10.0f : v;   // 0 or garbage must not make anyone immortal
}

bool IniTime(FILETIME& t)
{
	WIN32_FILE_ATTRIBUTE_DATA a;
	if (!GetFileAttributesExA(g_ini, GetFileExInfoStandard, &a))
		return false;
	t = a.ftLastWriteTime;
	return true;
}

// RemovedSpells: names of kSpells separated by commas or spaces, any case; an unknown name is skipped.
LONG RemovedSpells()
{
	char buf[256];
	GetPrivateProfileStringA("Accio.Hardcore", "RemovedSpells", "", buf, sizeof(buf), g_ini);
	LONG mask = 0;
	char* next = nullptr;
	for (char* word = strtok_s(buf, ", \t", &next); word; word = strtok_s(nullptr, ", \t", &next))
		for (const auto& s : kSpells)
			if (_stricmp(word, s.name) == 0)
				mask |= 1L << s.number;
	return mask;
}

void Read(const char* why)
{
	const float taken = Factor("DamageTaken"), dealt = Factor("DamageDealt"), react = Factor("EnemyReaction");
	const LONG removed = RemovedSpells();
	if (taken == g_taken && dealt == g_dealt && react == g_react && removed == g_removed && why[0] == 'c')
		return;
	g_taken = taken;
	g_dealt = dealt;
	g_react = react;
	InterlockedExchange(&g_removed, removed);
	Log("Hardcore: damage taken x%.2f, dealt x%.2f, enemy reaction x%.2f, spells removed %04lX (%s)\n", taken, dealt,
		react, removed, why);
}

void HookSpells(HMODULE exe)
{
	// cmp byte [0xCEF61E], 0 ; jne +0x49 ; pushfd ; stc ; mov ecx, 0xD6112C ; mov ecx, [ecx] ; popfd ; shr ecx, 0x12
	static const BYTE body[] = { 0x80, 0x3D, 0x1E, 0xF6, 0xCE, 0x00, 0x00, 0x75, 0x49, 0x9C, 0xF9, 0xB9, 0x2C, 0x11,
		0xD6, 0x00, 0x8B, 0x09, 0x9D, 0xC1, 0xE9, 0x12 };
	bool twice = false;
	BYTE* at = FindUniquePattern(exe, body, sizeof(body), &twice);
	if (!at)
	{
		Log("Hardcore: spell check %s, removed spells unavailable\n", twice ? "found twice" : "not found");
		return;
	}
	// The one jmp rel32 of the executable sections that lands on it: the callers all go through that jump.
	auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(exe);
	auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<BYTE*>(exe) + dos->e_lfanew);
	IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
	BYTE* jump = nullptr;
	int found = 0;
	for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
	{
		if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE))
			continue;
		BYTE* p = reinterpret_cast<BYTE*>(exe) + sec->VirtualAddress;
		const DWORD size = sec->Misc.VirtualSize;
		for (DWORD k = 0; k + 5 <= size; ++k)
		{
			if (p[k] != 0xE9)
				continue;
			LONG rel;
			memcpy(&rel, p + k + 1, sizeof(rel));
			if (p + k + 5 + rel == at)
			{
				jump = p + k;
				++found;
			}
		}
	}
	if (found != 1)
	{
		Log("Hardcore: spell check at %p, %d jumps to it, removed spells unavailable\n", at, found);
		return;
	}
	g_spellBody = at;
	BYTE code[5] = { 0xE9 };
	const LONG rel = static_cast<LONG>(reinterpret_cast<LONG_PTR>(&SpellCheck) - reinterpret_cast<LONG_PTR>(jump + 5));
	memcpy(code + 1, &rel, sizeof(rel));
	Log("Hardcore: spell check at %p, jump at %p %s\n", at, jump, WriteMemory(jump, code, sizeof(code)) ? "hooked" : "NOT hooked");
}

void HookFoes(HMODULE exe)
{
	// mov eax, [esp+4] ; movss xmm0, [0x8059CC] (Health 50) ; movss xmm1, [0x831C30] (0.5) ; movss [eax+0x60], xmm0
	static const BYTE entry[] = { 0x8B, 0x44, 0x24, 0x04, 0xF3, 0x0F, 0x10, 0x05, 0xCC, 0x59, 0x80, 0x00, 0xF3, 0x0F,
		0x10, 0x0D, 0x30, 0x1C, 0x83, 0x00, 0xF3, 0x0F, 0x11, 0x40, 0x60 };
	static const BYTE tail[] = { 0xC3, 0xCC, 0xCC, 0xCC, 0xCC };   // ret, then padding: room for a jump
	bool twice = false;
	BYTE* at = FindUniquePattern(exe, entry, sizeof(entry), &twice);
	if (!at || memcmp(at + 0x9B, tail, sizeof(tail)) != 0)
	{
		Log("Hardcore: foe defaults %s, enemy reaction unavailable\n", twice ? "found twice" : "not found");
		return;
	}
	BYTE code[5] = { 0xE9 };
	const LONG rel = static_cast<LONG>(reinterpret_cast<LONG_PTR>(&ReactScale) - reinterpret_cast<LONG_PTR>(at + 0x9B + 5));
	memcpy(code + 1, &rel, sizeof(rel));
	Log("Hardcore: foe defaults at %p %s\n", at, WriteMemory(at + 0x9B, code, sizeof(code)) ? "hooked" : "NOT hooked");
}

void OpenIni()
{
	GetModuleFileNameA(g_self, g_ini, MAX_PATH);   // d3d9.ini next to this DLL, as main.cpp reads it
	char* end = strrchr(g_ini, '\\');
	strcpy_s(end ? end + 1 : g_ini, MAX_PATH - (end ? end + 1 - g_ini : 0), "d3d9.ini");
	IniTime(g_iniTime);
	Read("start");
}

void InstallHardcoreHP5(HMODULE exe)
{
	// push esi ; mov esi, eax ; test esi, esi ; jne +6 ; mov al, 1 ; pop esi ; ret 4 ; push edi ;
	// mov edi, [0xC7B200] ; cmp edi, 4
	static const BYTE entry[] = { 0x56, 0x8B, 0xF0, 0x85, 0xF6, 0x75, 0x06, 0xB0, 0x01, 0x5E, 0xC2, 0x04, 0x00, 0x57,
		0x8B, 0x3D, 0x00, 0xB2, 0xC7, 0x00, 0x83, 0xFF, 0x04 };
	bool twice = false;
	BYTE* at = FindUniquePattern(exe, entry, sizeof(entry), &twice);
	if (!at)
	{
		Log("Hardcore: damage routine %s, hard mode unavailable\n", twice ? "found twice" : "not found");
		return;
	}
	OpenIni();
	g_back = at + 5;
	BYTE code[5] = { 0xE9 };
	const LONG rel = static_cast<LONG>(reinterpret_cast<LONG_PTR>(&DamageScaleHP5) - reinterpret_cast<LONG_PTR>(at + 5));
	memcpy(code + 1, &rel, sizeof(rel));
	Log("Hardcore: damage routine at %p %s\n", at, WriteMemory(at, code, sizeof(code)) ? "hooked" : "NOT hooked");
}
}

void InstallHardcore()
{
	HMODULE exe = GetModuleHandleA(nullptr);
	char path[MAX_PATH];
	GetModuleFileNameA(exe, path, MAX_PATH);
	const char* slash = strrchr(path, '\\');
	const char* name = slash ? slash + 1 : path;
	if (_stricmp(name, "hp.exe") == 0)
	{
		InstallHardcoreHP5(exe);
		return;
	}
	if (_stricmp(name, "hp6.exe") != 0)
		return;
	// push ebp ; mov ebp, esp ; and esp, -16 ; sub esp, 0x24 ; push ebx, esi, edi ; mov edi, eax ;
	// test edi, edi ; je +0x339 ; cmp dword [0xD610D4], 3
	static const BYTE entry[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x83, 0xEC, 0x24, 0x53, 0x56, 0x57, 0x8B, 0xF8,
		0x85, 0xFF, 0x0F, 0x84, 0x39, 0x03, 0x00, 0x00, 0x83, 0x3D, 0xD4, 0x10, 0xD6, 0x00, 0x03 };
	bool twice = false;
	BYTE* at = FindUniquePattern(exe, entry, sizeof(entry), &twice);
	if (!at)
	{
		Log("Hardcore: damage routine %s, hard mode unavailable\n", twice ? "found twice" : "not found");
		return;
	}
	OpenIni();
	g_back = at + 6;
	BYTE code[6] = { 0xE9, 0, 0, 0, 0, 0x90 };
	const LONG rel = static_cast<LONG>(reinterpret_cast<LONG_PTR>(&DamageScale) - reinterpret_cast<LONG_PTR>(at + 5));
	memcpy(code + 1, &rel, sizeof(rel));
	Log("Hardcore: damage routine at %p %s\n", at, WriteMemory(at, code, sizeof(code)) ? "hooked" : "NOT hooked");
	HookFoes(exe);
	HookSpells(exe);
}

void PollHardcore()
{
	if (!g_back)
		return;
	const DWORD now = GetTickCount();
	if (now - g_lastLook < 1000)
		return;
	g_lastLook = now;
	if (g_hitsTaken != g_saidTaken || g_hitsDealt != g_saidDealt)
	{
		g_saidTaken = g_hitsTaken;
		g_saidDealt = g_hitsDealt;
		Log("Hardcore: hits so far %ld on Harry, %ld on others\n", g_saidTaken, g_saidDealt);
	}
	if (g_foes != g_saidFoes)
	{
		g_saidFoes = g_foes;
		Log("Hardcore: foes set up so far %ld\n", g_saidFoes);
	}
	if (g_refused != g_saidRefused)
	{
		g_saidRefused = g_refused;
		Log("Hardcore: removed spells refused so far %ld times\n", g_saidRefused);
	}
	FILETIME t;
	if (IniTime(t) && CompareFileTime(&t, &g_iniTime) != 0)
	{
		g_iniTime = t;
		Read("changed");
	}
}
