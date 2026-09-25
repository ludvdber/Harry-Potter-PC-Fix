// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// What direct3d.cpp learns about the game's rendering and effects.cpp needs.

#pragma once
#include "accio.h"

#ifndef D3DFMT_INTZ
// A depth format that can also be read as a texture; every Direct3D 10 class GPU has it.
#define D3DFMT_INTZ static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z'))
#endif

struct SceneDepth
{
	bool supported = false;              // INTZ available on this GPU
	IDirect3DTexture9* texture = nullptr; // our reference keeps it alive across the game's unbinds
	IDirect3DSurface9* surface = nullptr; // level 0 of `texture`, compared against, not owned
	UINT width = 0, height = 0;
	// The render target drawn with this depth. After a Reset the game can make its depth the size
	// of the window while the image keeps the size from the ini: the depth is then only filled in
	// its top-left part, and ambient occlusion must scale its coordinates by target / depth.
	UINT targetWidth = 0, targetHeight = 0;
	bool aoDoneThisFrame = false;
};

extern SceneDepth g_depth;
extern int g_backBufferWidth, g_backBufferHeight;
