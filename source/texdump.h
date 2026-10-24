// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Textures by fingerprint, for modders. TextureDump=1: every texture the game fills is saved once,
// as a .dds named after its content. TextureReplace=1: a .dds in accio_remplacements\ whose name
// ends with that fingerprint takes the texture's place (any supported format, or plain 32-bit RGBA,
// encoded to the game's format), and its mipmaps are rebuilt from it. Same size: written into the
// game's texture. Another size (an HD texture): a texture of its own, handed to the game at SetTexture
// instead of the original and released with it. Both off by default.
#pragma once
#include <d3d9.h>

namespace texdump
{
	// A texture worth looking at: filled by the game itself, not a render target or a per-frame buffer.
	bool Wanted(DWORD usage, D3DPOOL pool);
	// Called once, at the first fill of level 0: fingerprint, then save and/or replace as configured.
	void FirstFill(IDirect3DTexture9* tex, bool dump, bool replace);
	// The texture to bind in place of `tex` (its HD replacement), or `tex` itself.
	IDirect3DBaseTexture9* Swap(IDirect3DBaseTexture9* tex);
	// `tex` is gone: its HD replacement goes with it.
	void Released(IDirect3DTexture9* tex);
}
