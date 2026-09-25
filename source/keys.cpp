// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// [Accio.Keys]: the player's own keys for the game's actions. HP4 can only be played with the
// arrow keys and C, X, Z, S, and its Controller Configuration tool only covers game pads.
//
//   MoveUp=Z                 action = key, as printed on the player's keyboard
//   Charm=MouseLeft,J        several keys for one action
//
// The game still reads DirectInput; the keys it receives are rewritten on the way. A key given
// to an action stops doing what it did before (S moving down no longer casts Magicus Extremos);
// every other original key keeps working.

#define DIRECTINPUT_VERSION 0x0800
#include "accio.h"
#include <dinput.h>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace
{
struct Name
{
	const char* name;
	BYTE code;
};

// What the game reads: DirectInput key codes, i.e. positions on a US keyboard.
const Name kActionsHp4[] = {
	{ "MoveUp", DIK_UP }, { "MoveDown", DIK_DOWN }, { "MoveLeft", DIK_LEFT }, { "MoveRight", DIK_RIGHT },
	{ "Charm", DIK_C }, { "Jinx", DIK_X }, { "Accio", DIK_Z }, { "Extremos", DIK_S },
	{ "Pause", DIK_ESCAPE }, { "Confirm", DIK_RETURN }, { "Back", DIK_BACK },
};

// Keys named by what they are, not by a printed character: the same everywhere.
const Name kNamedKeys[] = {
	{ "Escape", DIK_ESCAPE }, { "Tab", DIK_TAB }, { "Enter", DIK_RETURN }, { "Return", DIK_RETURN },
	{ "Space", DIK_SPACE }, { "Backspace", DIK_BACK }, { "CapsLock", DIK_CAPITAL },
	{ "LShift", DIK_LSHIFT }, { "RShift", DIK_RSHIFT }, { "LCtrl", DIK_LCONTROL }, { "RCtrl", DIK_RCONTROL },
	{ "LAlt", DIK_LMENU }, { "RAlt", DIK_RMENU },
	{ "Up", DIK_UP }, { "Down", DIK_DOWN }, { "Left", DIK_LEFT }, { "Right", DIK_RIGHT },
	{ "Insert", DIK_INSERT }, { "Delete", DIK_DELETE }, { "Home", DIK_HOME }, { "End", DIK_END },
	{ "PageUp", DIK_PRIOR }, { "PageDown", DIK_NEXT },
	{ "F1", DIK_F1 }, { "F2", DIK_F2 }, { "F3", DIK_F3 }, { "F4", DIK_F4 }, { "F5", DIK_F5 }, { "F6", DIK_F6 },
	{ "F7", DIK_F7 }, { "F8", DIK_F8 }, { "F9", DIK_F9 }, { "F10", DIK_F10 }, { "F11", DIK_F11 }, { "F12", DIK_F12 },
	{ "Num0", DIK_NUMPAD0 }, { "Num1", DIK_NUMPAD1 }, { "Num2", DIK_NUMPAD2 }, { "Num3", DIK_NUMPAD3 },
	{ "Num4", DIK_NUMPAD4 }, { "Num5", DIK_NUMPAD5 }, { "Num6", DIK_NUMPAD6 }, { "Num7", DIK_NUMPAD7 },
	{ "Num8", DIK_NUMPAD8 }, { "Num9", DIK_NUMPAD9 }, { "NumEnter", DIK_NUMPADENTER },
};

const Name kMouse[] = {
	{ "MouseLeft", VK_LBUTTON }, { "MouseRight", VK_RBUTTON }, { "MouseMiddle", VK_MBUTTON },
	{ "Mouse4", VK_XBUTTON1 }, { "Mouse5", VK_XBUTTON2 },
};

struct Binding
{
	bool mouse;  // `source` is a mouse button (virtual-key code), else a DirectInput key code
	BYTE source;
	BYTE target; // DirectInput key code the game receives
};

constexpr int kMaxBindings = 64;
Binding g_bindings[kMaxBindings];
int g_count = 0;
bool g_consumed[256] = {}; // keys given to an action: their own meaning is gone

template <size_t N>
const Name* Lookup(const Name (&table)[N], const char* s)
{
	for (const Name& n : table)
		if (_stricmp(n.name, s) == 0)
			return &n;
	return nullptr;
}

// A key as printed on the player's keyboard: letters, digits and punctuation go through the
// current layout (on AZERTY, "Z" is the key marked Z, which DirectInput calls W).
bool SourceKey(const char* s, Binding& b)
{
	if (const Name* n = Lookup(kMouse, s))
	{
		b.mouse = true;
		b.source = n->code;
		return true;
	}
	if (const Name* n = Lookup(kNamedKeys, s))
	{
		b.mouse = false;
		b.source = n->code;
		return true;
	}
	if (s[0] && !s[1])
	{
		const SHORT vk = VkKeyScanA(s[0]);
		const UINT scan = vk == -1 ? 0 : MapVirtualKeyA(LOBYTE(vk), MAPVK_VK_TO_VSC);
		if (scan && scan < 0x80)
		{
			b.mouse = false;
			b.source = static_cast<BYTE>(scan);
			return true;
		}
	}
	return false;
}

bool Target(const char* action, BYTE& code)
{
	char exe[MAX_PATH];
	GetModuleFileNameA(nullptr, exe, MAX_PATH);
	const char* name = strrchr(exe, '\\');
	if (name && _stricmp(name + 1, "gof_f.exe") == 0)
		if (const Name* n = Lookup(kActionsHp4, action))
		{
			code = n->code;
			return true;
		}
	// Any game: "Key.X" is the game's own key X, named on a US keyboard.
	if (_strnicmp(action, "Key.", 4) == 0)
	{
		if (const Name* n = Lookup(kNamedKeys, action + 4))
		{
			code = n->code;
			return true;
		}
		const char c = static_cast<char>(toupper(static_cast<unsigned char>(action[4])));
		static const char kUsRows[] = "QWERTYUIOP\0ASDFGHJKL\0ZXCVBNM";
		static const BYTE kUsStarts[] = { DIK_Q, DIK_A, DIK_Z };
		for (int row = 0, i = 0; row < 3; row++, i++)
			for (int k = 0; kUsRows[i]; k++, i++)
				if (kUsRows[i] == c && !action[5])
				{
					code = static_cast<BYTE>(kUsStarts[row] + k);
					return true;
				}
	}
	return false;
}

void Trim(char*& s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char* e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
		*--e = '\0';
}
}

void LoadKeyMap(const char* iniPath)
{
	static char section[8192];
	const DWORD n = GetPrivateProfileSectionA("Accio.Keys", section, sizeof(section), iniPath);
	for (char* line = section; n && *line; line += strlen(line) + 1)
	{
		char buf[256];
		strncpy_s(buf, line, _TRUNCATE);
		char* eq = strchr(buf, '=');
		if (!eq || buf[0] == ';')
			continue;
		*eq = '\0';
		char* action = buf;
		Trim(action);
		BYTE target = 0;
		if (!Target(action, target))
		{
			Log("Keys: unknown action \"%s\", line ignored\n", action);
			continue;
		}
		char* context = nullptr;
		for (char* key = strtok_s(eq + 1, ",;", &context); key; key = strtok_s(nullptr, ",;", &context))
		{
			Trim(key);
			if (!*key)
				continue;
			Binding b = {};
			if (!SourceKey(key, b))
			{
				Log("Keys: unknown key \"%s\" for %s\n", key, action);
				continue;
			}
			if (g_count == kMaxBindings)
				break;
			b.target = target;
			g_bindings[g_count++] = b;
			if (!b.mouse && b.source != target)
				g_consumed[b.source] = true;
			Log("Keys: %s -> %s\n", key, action);
		}
	}
}

// The keyboard as the game will see it. `keys` is DirectInput's 256-byte state.
void RemapKeyboardState(BYTE* keys)
{
	if (!g_count)
		return;
	BYTE pressed[256];
	memcpy(pressed, keys, sizeof(pressed));
	for (int k = 0; k < 256; k++)
		if (g_consumed[k])
			keys[k] = 0;
	const bool mouseCounts = ProcessInForeground();
	for (int i = 0; i < g_count; i++)
	{
		const Binding& b = g_bindings[i];
		const bool down = b.mouse ? (mouseCounts && (GetAsyncKeyState(b.source) & 0x8000)) : (pressed[b.source] & 0x80) != 0;
		if (down)
			keys[b.target] = 0x80;
	}
}

// The same for keyboard events read one by one (a game reading the keyboard that way cannot be
// given mouse buttons: those are not keyboard events).
void RemapKeyboardEvents(DIDEVICEOBJECTDATA* events, DWORD count, DWORD size)
{
	if (!g_count || !events || size < sizeof(DIDEVICEOBJECTDATA_DX3))
		return;
	auto* bytes = reinterpret_cast<BYTE*>(events);
	for (DWORD e = 0; e < count; e++)
	{
		auto* ev = reinterpret_cast<DIDEVICEOBJECTDATA*>(bytes + e * size);
		const DWORD key = ev->dwOfs;
		if (key > 255)
			continue;
		for (int i = 0; i < g_count; i++)
			if (!g_bindings[i].mouse && g_bindings[i].source == key)
			{
				ev->dwOfs = g_bindings[i].target;
				break;
			}
	}
}
