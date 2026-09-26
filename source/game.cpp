// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// What is changed inside each game executable. Every address and byte sequence below was read
// in the retail executables (gof_f.exe, hp.exe, hp6.exe, hp7.exe; no ASLR, base 0x400000) and each
// sequence occurs exactly once in its file. A change whose bytes are not found is skipped and
// logged: a different build of the game then runs as it always did.

#include "hooks.h"
#include <cstring>

namespace
{
struct Bytes
{
	const BYTE* data;
	size_t size;
};
#define BYTES(...) []() { static const BYTE b[] = { __VA_ARGS__ }; return Bytes{ b, sizeof(b) }; }()

enum class FovKind
{
	None,
	Degrees,          // the field of view itself, in degrees
	RadiansPerDegree, // the pi/180 the engine multiplies angles by: raising it widens the view
	RadiansPerDegreeDouble, // the same, stored as a double (HP7 part 2)
	RadiansPerDegreeLoad,   // one load of a shared double pi/180, pointed at our own value (HP7 part 1)
};

struct Profile
{
	const char* exe;
	const char* name;

	// The resolution the engine starts with before any option is read (640x480 or 800x600):
	// two 32-bit immediates in the start-up code, at byte `widthAt` and `heightAt` of `resolution`.
	Bytes resolution;
	int widthAt, heightAt;

	// The first float in the image equal to the engine's aspect ratio is the one it renders with.
	float aspect;
	const float* oldAspects; // index 1.. of the previous file format
	int oldAspectCount;

	FovKind fov;
	uintptr_t fovAddress;
	float fovOriginal;
	const float* oldFovs;    // index 1.. of the previous file format; none: the value itself
	int oldFovCount;

	// HP4 plays its character animations at 20 frames per second: a float pushed at `animAt`.
	Bytes animations;
	int animAt;

	// HP5 and HP6 start with a presentation interval of 2 (30 frames per second). The 32-bit
	// immediate at `unlockAt` is set to 1.
	Bytes unlock;
	int unlockAt;

	// Frame-rate ceiling the engine reads from memory and sometimes resets; held at the chosen
	// value by a background thread. With `capOnlyNonZero`, a zero (the engine pausing its clock)
	// is left alone.
	uintptr_t capAddress;
	bool capOnlyNonZero;
	int capDefault;

	// HP4 copies its frame-rate reference (60.0) into a variable once, from a constant that
	// unrelated code shares. The load is pointed at our own value instead of changing the shared
	// constant: the 32-bit address at `capLoadAt`.
	Bytes capLoad;
	int capLoadAt;

	// HP4 draws a haze on some levels (Forbidden Forest, lake, maze, graveyard): horizontal bands,
	// each a triangle strip cut into 16-pixel columns across the screen. The strip is built in a
	// stack array of 129 column pairs, so a screen wider than 2048 pixels overruns it and the game
	// crashes (1920: 121 pairs; 2560: 161). `haze` is the jump at the start of the method, made
	// unconditional to skip the haze (what the earlier fix did on every level); `hazeColumns` is
	// the column count (add edi, 15 ; sar edi, 4 at `hazeColumnsAt`), replaced by a call that
	// also caps it. The game derives each column's step from that count, so fewer, wider
	// columns still span the whole screen.
	Bytes haze;
	Bytes hazeColumns;
	int hazeColumnsAt;

	// HP7 part 1 waits, spinning on QueryPerformanceCounter, until 33.3 ms have passed since the
	// previous frame before each Present: a 30 frames per second limit of its own. `frameWait` is
	// the `je rel32` that skips the wait when no previous frame was timed, at `frameWaitAt`; made
	// unconditional, the wait never runs and FPSLimit sets the pace. The earlier fix only did it
	// while the 9 key was toggled.
	Bytes frameWait;
	int frameWaitAt;

	// FovKind::RadiansPerDegreeLoad: the `fld qword [fovAddress]` whose 32-bit address is at
	// `fovLoadAt` of `fovLoad`. The constant itself is read by dozens of other conversions.
	Bytes fovLoad;
	int fovLoadAt;
};

const float kHp4Aspects[] = { 16.0f / 9, 16.0f / 10, 64.0f / 27, 43.0f / 18, 12.0f / 5, 32.0f / 9 };
const float kHp56Aspects[] = { 16.0f / 10, 64.0f / 27, 43.0f / 18, 12.0f / 5, 32.0f / 9 };
// The earlier HP7 fix's `fullscreenaspectratio` 1..5 (its ini: 16:10, three 21:9, 32:10).
const float kHp7Aspects[] = { 16.0f / 10, 64.0f / 27, 43.0f / 18, 12.0f / 5, 32.0f / 10 };
const float kHp5Fovs[] = { 0.020f, 0.024f, 0.026f };
const float kHp8Fovs[] = { 0.025f }; // the earlier fix's [FOV] fov=1
const float kHp6Fovs[] = { 0.020f, 0.022f, 0.024f };
constexpr float kPiOver180 = 0.01745329238f;

const Profile* FindProfile(const char* exeName)
{
	static const Profile profiles[] = {
		{
			"gof_f.exe", "Goblet of Fire",
			// ... 600 ; mov dword ptr [0x0075F274], 800
			BYTES(0x58, 0x02, 0x00, 0x00, 0xC7, 0x05, 0x74, 0xF2, 0x75, 0x00, 0x20, 0x03, 0x00, 0x00), 10, 0,
			4.0f / 3, kHp4Aspects, 6,
			FovKind::Degrees, 0x007494FC, 114.591552734375f, nullptr, 0,
			// mov eax, [0x007C337C] ; push eax ; push 20.0f ; call
			BYTES(0xA1, 0x7C, 0x33, 0x7C, 0x00, 0x50, 0x68, 0x00, 0x00, 0xA0, 0x41, 0xE8), 7,
			{ nullptr, 0 }, 0,
			0, false, 120,
			// movss xmm0, [0x00749388] ; mov ebx, [0x00926744]
			BYTES(0xF3, 0x0F, 0x10, 0x05, 0x88, 0x93, 0x74, 0x00, 0x8B, 0x1D, 0x44, 0x67, 0x92, 0x00), 4,
			// jnz +0x7C1 ; mov eax, [0x0092FFD0] ; cmp eax, ecx ; je +0x0C ; cmp ...
			BYTES(0x0F, 0x85, 0xC1, 0x07, 0x00, 0x00, 0xA1, 0xD0, 0xFF, 0x92, 0x00, 0x3B, 0xC1, 0x74, 0x0C, 0x81),
			// cvttss2si edi, xmm1 ; add edi, 15 ; sar edi, 4 ; cmp ebx, 1
			BYTES(0xF3, 0x0F, 0x2C, 0xF9, 0x83, 0xC7, 0x0F, 0xC1, 0xFF, 0x04, 0x83, 0xFB, 0x01), 4,
		},
		{
			"hp.exe", "Order of the Phoenix",
			// mov dword ptr [ebp-0x80], 640 ; mov dword ptr [ebp-0x7C], 480
			BYTES(0x80, 0x80, 0x02, 0x00, 0x00, 0xC7, 0x45, 0x84, 0xE0, 0x01), 1, 8,
			16.0f / 9, kHp56Aspects, 5,
			FovKind::RadiansPerDegree, 0x008438B8, kPiOver180, kHp5Fovs, 3,
			{ nullptr, 0 }, 0,
			// mov dword ptr [0x00AEEA5C], 2
			BYTES(0xC7, 0x05, 0x5C, 0xEA, 0xAE, 0x00, 0x02, 0x00, 0x00, 0x00), 6,
			0x008E4C54, false, 120,
			{ nullptr, 0 }, 0,
			{ nullptr, 0 }, { nullptr, 0 }, 0,
		},
		{
			"hp6.exe", "Half-Blood Prince",
			// mov [ebp-0x84], edx ; mov dword ptr [ebp-0x80], 640 ; mov dword ptr [ebp-0x7C], 480
			BYTES(0xFF, 0xFF, 0x89, 0x95, 0x7C, 0xFF, 0xFF, 0xFF, 0xC7, 0x45, 0x80, 0x80, 0x02, 0x00, 0x00,
			      0xC7, 0x45, 0x84, 0xE0, 0x01), 11, 18,
			16.0f / 9, kHp56Aspects, 5,
			FovKind::RadiansPerDegree, 0x008482B8, kPiOver180, kHp6Fovs, 3,
			{ nullptr, 0 }, 0,
			// mov dword ptr [0x00BA9504], 2
			BYTES(0xC7, 0x05, 0x04, 0x95, 0xBA, 0x00, 0x02, 0x00, 0x00, 0x00), 6,
			0x008A4474, true, 120,
			{ nullptr, 0 }, 0,
			{ nullptr, 0 }, { nullptr, 0 }, 0,
		},
		{
			"hp7.exe", "Deathly Hallows Part 1",
			{ nullptr, 0 }, 0, 0,
			16.0f / 9, kHp7Aspects, 5,
			// The camera's set-up converts its FOV, SprintFOV and DementorFOV (degrees) with one pi/180
			// kept on the FPU stack; the earlier fix pointed that load at 0.03 for all players (x1.72).
			FovKind::RadiansPerDegreeLoad, 0x00697370, kPiOver180, nullptr, 0,
			{ nullptr, 0 }, 0,
			{ nullptr, 0 }, 0,
			0, false, 0,
			{ nullptr, 0 }, 0,
			{ nullptr, 0 }, { nullptr, 0 }, 0,
			// mov ecx, [0x007B24C0] ; or ecx, [0x007B24C4] ; je +0xB2
			BYTES(0x8B, 0x0D, 0xC0, 0x24, 0x7B, 0x00, 0x0B, 0x0D, 0xC4, 0x24, 0x7B, 0x00, 0x0F, 0x84, 0xB2, 0x00, 0x00, 0x00), 12,
			// fld qword [0x00697370] ; mov eax, "SprintFOV" ; fmul st(1), st ; push ecx ; fxch st(1)
			BYTES(0xDD, 0x05, 0x70, 0x73, 0x69, 0x00, 0xB8, 0xC4, 0x33, 0x68, 0x00, 0xDC, 0xC9, 0x51, 0xD9, 0xC9), 2,
		},
		{
			// SecuROM: the code is encrypted in the file and decrypted while the game starts, even
			// after Direct3D is created: the wait is looked for over the first frames
			// (ApplyLateGamePatches). Found in memory at 0x00434D41 (2026-09-25).
			"hp8.exe", "Deathly Hallows Part 2",
			{ nullptr, 0 }, 0, 0,
			0, nullptr, 0,
			// pi/180 as a double (rounded through a float), read by some 70 conversions; the earlier
			// fix's [FOV] fov=1 made it 0.025 (measured in the running game, 2026-09-25).
			FovKind::RadiansPerDegreeDouble, 0x007E69F0, kPiOver180, kHp8Fovs, 1,
			{ nullptr, 0 }, 0,
			{ nullptr, 0 }, 0,
			0, false, 0,
			{ nullptr, 0 }, 0,
			{ nullptr, 0 }, { nullptr, 0 }, 0,
			// mov ecx, [0x00935FB0] ; or ecx, [0x00935FB4] ; je +0xAA
			BYTES(0x8B, 0x0D, 0xB0, 0x5F, 0x93, 0x00, 0x0B, 0x0D, 0xB4, 0x5F, 0x93, 0x00, 0x0F, 0x84, 0xAA, 0x00, 0x00, 0x00), 12,
		},
	};
	for (const Profile& p : profiles)
		if (_stricmp(exeName, p.exe) == 0)
			return &p;
	return nullptr;
}

HMODULE g_exe = nullptr;
const Profile* g_profile = nullptr;

BYTE* Locate(const Bytes& b, const char* what)
{
	BYTE* at = b.data ? FindPattern(g_exe, b.data, b.size) : nullptr;
	if (!at && b.data)
		Log("Game: %s code not found, left unchanged\n", what);
	return at;
}

void PatchResolution(const Profile& p)
{
	if (g_cfg.width <= 0 || g_cfg.height <= 0 || g_cfg.width > 16384 || g_cfg.height > 16384)
		return;
	BYTE* at = Locate(p.resolution, "start-up resolution");
	if (!at)
		return;
	const DWORD w = static_cast<DWORD>(g_cfg.width), h = static_cast<DWORD>(g_cfg.height);
	// The immediates are 32-bit; the sequences only cover their low half where they end the
	// pattern, and a resolution never needs more than 16 bits.
	const bool ok = WriteMemory(at + p.widthAt, &w, 2) && WriteMemory(at + p.heightAt, &h, 2);
	Log("Game: start-up resolution %lux%lu %s\n", w, h, ok ? "set" : "NOT set");
}

void PatchAspect(const Profile& p)
{
	float ratio = g_cfg.aspectRatio;
	if (ratio <= 0 && g_cfg.legacyAspectIndex >= 1 && g_cfg.legacyAspectIndex <= p.oldAspectCount)
		ratio = p.oldAspects[g_cfg.legacyAspectIndex - 1];
	if (p.aspect <= 0 || ratio <= 0 || ratio == p.aspect)
		return; // p.aspect 0: the constant has not been found in this game
	BYTE* at = FindPattern(g_exe, reinterpret_cast<const BYTE*>(&p.aspect), sizeof(float));
	if (!at)
	{
		Log("Game: aspect ratio constant not found, left unchanged\n");
		return;
	}
	const bool ok = WriteMemory(at, &ratio, sizeof(float));
	Log("Game: aspect ratio %.4f at %p %s\n", ratio, at, ok ? "set" : "NOT set");
}

void PatchFov(const Profile& p)
{
	if (p.fov == FovKind::None)
		return;
	float value = 0;
	if (g_cfg.fovScale > 0)
		value = p.fovOriginal * g_cfg.fovScale;
	else if (g_cfg.legacyFov > 0)
	{
		if (p.oldFovs && g_cfg.legacyFov <= p.oldFovCount)
			value = p.oldFovs[g_cfg.legacyFov - 1];
		else if (!p.oldFovs && p.fov == FovKind::Degrees)
			value = static_cast<float>(g_cfg.legacyFov); // HP4 took degrees
	}
	if (value <= 0 || value == p.fovOriginal)
		return;
	if (p.fov == FovKind::RadiansPerDegreeLoad)
	{
		static double ours = 0;
		BYTE* site = Locate(p.fovLoad, "field of view load");
		DWORD address = 0;
		if (!site)
			return;
		memcpy(&address, site + p.fovLoadAt, sizeof(address));
		if (address != p.fovAddress || *reinterpret_cast<const double*>(p.fovAddress) != static_cast<double>(p.fovOriginal))
		{
			Log("Game: field of view load not as expected, left unchanged\n");
			return;
		}
		ours = static_cast<double>(value);
		const DWORD to = static_cast<DWORD>(reinterpret_cast<uintptr_t>(&ours));
		const bool ok = WriteMemory(site + p.fovLoadAt, &to, sizeof(to));
		Log("Game: camera field of view x%.3f %s\n", value / p.fovOriginal, ok ? "set" : "NOT set");
		return;
	}
	if (p.fov == FovKind::RadiansPerDegreeDouble)
	{
		auto* at = reinterpret_cast<double*>(p.fovAddress);
		const double was = p.fovOriginal, now = value;
		if (IsBadReadPtr(at, sizeof(double)) || *at != was)
		{
			Log("Game: field of view constant not where expected, left unchanged\n");
			return;
		}
		const bool ok = WriteMemory(at, &now, sizeof(double));
		Log("Game: field of view %g (was %g) %s\n", now, was, ok ? "set" : "NOT set");
		return;
	}
	auto* at = reinterpret_cast<float*>(p.fovAddress);
	// Only over the value the retail executable holds: anything else is a build we have not read.
	if (IsBadReadPtr(at, sizeof(float)) || *at != p.fovOriginal)
	{
		Log("Game: field of view constant not where expected, left unchanged\n");
		return;
	}
	const bool ok = WriteMemory(at, &value, sizeof(float));
	Log("Game: field of view %g (was %g) %s\n", value, p.fovOriginal, ok ? "set" : "NOT set");
}

void PatchAnimations(const Profile& p)
{
	if (!p.animations.data || g_cfg.animationRate <= 0 || g_cfg.animationRate > 240)
		return;
	BYTE* at = Locate(p.animations, "animation rate");
	if (!at)
		return;
	const float rate = static_cast<float>(g_cfg.animationRate);
	const bool ok = WriteMemory(at + p.animAt, &rate, sizeof(float));
	Log("Game: animations at %d fps %s\n", g_cfg.animationRate, ok ? "set" : "NOT set");
}

void PatchFrameInterval(const Profile& p)
{
	if (!p.unlock.data || g_cfg.unlockFrameRate == 0)
		return;
	BYTE* at = Locate(p.unlock, "frame interval");
	if (!at)
		return;
	const DWORD one = 1;
	const bool ok = WriteMemory(at + p.unlockAt, &one, sizeof(one));
	Log("Game: frame interval 1 (30 fps limit lifted) %s\n", ok ? "set" : "NOT set");
}

// Returns false while the code is not there yet (HP7 part 2 decrypts it late, see
// ApplyLateGamePatches); last logs its absence.
bool PatchFrameWait(const Profile& p, bool last)
{
	if (!p.frameWait.data || g_cfg.unlockFrameRate == 0)
		return true;
	BYTE* at = FindPattern(g_exe, p.frameWait.data, p.frameWait.size);
	if (!at)
	{
		if (last)
			Log("Game: 30 fps wait code not found, left unchanged\n");
		return false;
	}
	static const BYTE jump[] = { 0x90, 0xE9 }; // nop ; jmp rel32 (same target)
	const bool ok = WriteMemory(at + p.frameWaitAt, jump, sizeof(jump));
	Log("Game: 30 fps wait skipped (FPSLimit sets the pace, frame %ld) %s\n", g_frames, ok ? "set" : "NOT set");
	return true;
}
struct CapJob
{
	float* address;
	float value;
	bool onlyNonZero;
};

DWORD WINAPI HoldFrameRateCap(LPVOID param)
{
	const CapJob job = *static_cast<CapJob*>(param);
	for (;;)
	{
		const float now = *job.address;
		if (now != job.value && !(job.onlyNonZero && now == 0.0f))
			*job.address = job.value;
		Sleep(10);
	}
}

float g_frameReference = 0;

void RedirectFrameReference(const Profile& p, int cap)
{
	BYTE* at = Locate(p.capLoad, "frame-rate reference");
	if (!at)
		return;
	g_frameReference = static_cast<float>(cap);
	const DWORD address = static_cast<DWORD>(reinterpret_cast<uintptr_t>(&g_frameReference));
	const bool ok = WriteMemory(at + p.capLoadAt, &address, sizeof(address));
	Log("Game: frame-rate reference %d (was 60) %s\n", cap, ok ? "set" : "NOT set");
}

void HoldFrameRate(const Profile& p)
{
	const int cap = g_cfg.frameRateCap >= 0 ? g_cfg.frameRateCap : p.capDefault;
	if (cap <= 0 || cap > 1000)
		return;
	if (p.capLoad.data)
		RedirectFrameReference(p, cap);
	if (!p.capAddress)
		return;
	auto* at = reinterpret_cast<float*>(p.capAddress);
	MEMORY_BASIC_INFORMATION mbi = {};
	if (!VirtualQuery(at, &mbi, sizeof(mbi)) || !(mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE)))
	{
		Log("Game: frame-rate ceiling not writable, left unchanged\n");
		return;
	}
	static CapJob job;
	job = { at, static_cast<float>(cap), p.capOnlyNonZero };
	HANDLE t = CreateThread(nullptr, 0, HoldFrameRateCap, &job, 0, nullptr);
	Log("Game: frame-rate ceiling held at %d %s\n", cap, t ? "" : "(thread FAILED)");
	if (t)
		CloseHandle(t);
}

// Counted for the log (ReportHaze): whether the haze is drawn at all, and how wide it asked.
LONG g_hazeCalls = 0;
LONG g_hazeWidest = 0;

// Replaces `add edi, 15 ; sar edi, 4`: the number of 16-pixel columns across the screen, capped
// at the 127 column pairs (plus the closing one) the game's stack array holds. Flags are not
// read afterwards (the next instruction is a cmp). Called from the render thread only.
__declspec(naked) void HazeColumns()
{
	__asm {
		add edi, 15
		sar edi, 4
		inc dword ptr [g_hazeCalls]
		cmp edi, dword ptr [g_hazeWidest]
		jle counted
		mov dword ptr [g_hazeWidest], edi
	counted:
		cmp edi, 127
		jle fits
		mov edi, 127
	fits:
		ret
	}
}

// 0 = skipped (the earlier fix), 1 = drawn with the column cap, 2 = drawn as shipped (crashes
// on screens wider than 2048 pixels).
void PatchHaze(const Profile& p)
{
	const int mode = g_cfg.hazeOverlay >= 0 ? g_cfg.hazeOverlay : 0;
	if (!p.haze.data || mode == 2)
		return;
	if (mode == 1)
	{
		BYTE* site = Locate(p.hazeColumns, "haze column count");
		if (!site)
			return;
		BYTE* const from = site + p.hazeColumnsAt;
		BYTE call[6] = { 0xE8, 0, 0, 0, 0, 0x90 }; // call HazeColumns ; nop
		const LONG rel = static_cast<LONG>(reinterpret_cast<LONG_PTR>(&HazeColumns) - reinterpret_cast<LONG_PTR>(from + 5));
		memcpy(call + 1, &rel, sizeof(rel));
		const bool ok = WriteMemory(from, call, sizeof(call));
		Log("Game: haze drawn, columns capped at 127 %s\n", ok ? "" : "(NOT patched)");
		return;
	}
	BYTE* at = Locate(p.haze, "haze overlay");
	if (!at)
		return;
	static const BYTE jump[] = { 0x90, 0xE9 }; // nop ; jmp rel32 (same target)
	const bool ok = WriteMemory(at, jump, sizeof(jump));
	Log("Game: haze overlay skipped %s\n", ok ? "" : "(NOT patched)");
}
}

void ApplyGamePatches()
{
	g_exe = GetModuleHandleA(nullptr);
	char path[MAX_PATH];
	GetModuleFileNameA(g_exe, path, MAX_PATH);
	const char* slash = strrchr(path, '\\');
	const Profile* p = FindProfile(slash ? slash + 1 : path);
	if (!p)
	{
		Log("Game: %s is not a game this fix knows, no game change\n", slash ? slash + 1 : path);
		return;
	}
	Log("Game: %s\n", p->name);
	g_profile = p;
	PatchResolution(*p);
	PatchAspect(*p);
	PatchFov(*p);
	PatchAnimations(*p);
	PatchFrameInterval(*p);
	HoldFrameRate(*p);
	PatchHaze(*p);
}

// Once a frame: the first haze drawn, and the first one the cap had to shorten.
void ReportHaze()
{
	static LONG said = 0, cappedSaid = 0;
	if (!said && g_hazeCalls)
	{
		said = 1;
		Log("Game: haze drawn (frame %ld), %ld columns\n", g_frames, g_hazeWidest);
	}
	if (!cappedSaid && g_hazeWidest > 127)
	{
		cappedSaid = 1;
		Log("Game: haze of %ld columns capped at 127 (frame %ld)\n", g_hazeWidest, g_frames);
	}
}
// What can only be found once the game's own code runs: HP7 part 2's is encrypted in the file
// (SecuROM) and decrypted piece by piece while the game starts, after Direct3D is created.
// Tried at frames 1, 60, 600 and 3000 (a pattern search over the code costs a few ms).
void ApplyLateGamePatches(LONG frame)
{
	static bool done = false;
	if (done || !g_profile || (frame != 1 && frame != 60 && frame != 600 && frame != 3000))
		return;
	done = PatchFrameWait(*g_profile, frame == 3000) || frame == 3000;
}