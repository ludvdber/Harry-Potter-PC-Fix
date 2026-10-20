// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Shaders of the game ([Accio.Shaders]): every vertex and pixel shader the game creates gets a
// fingerprint of its bytecode. Dump=1 writes each one to shaders_dump\ next to the game, named
// by that fingerprint; Replace=1 creates a shader from shaders\<same name> instead when such a
// file exists. No file, no change. Pure file and bytecode logic, testable without Direct3D
// (tests\shaders_test.cpp); the device side lives in direct3d.cpp.

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>

// Bytes of a D3D9 shader token stream, end token included: the version token, then comments
// skipped by their length and instructions by theirs (shader model 1: one token at a time),
// until the end token. 0 when the start is no shader or no end shows up within maxBytes.
size_t ShaderBytecodeBytes(const DWORD* code, size_t maxBytes);

// FNV-1a, 64 bits, over the whole bytecode.
unsigned long long ShaderFingerprint(const void* data, size_t bytes);

// "vs_<16 hex digits>.bin" or "ps_<16 hex digits>.bin".
void ShaderFileName(bool pixel, unsigned long long fingerprint, wchar_t (&name)[24]);

// Reads folder\(file name above) into `out`. True only for a well-formed replacement: whole
// DWORDs, at least a version and an end token, and the version token of the right kind (a
// vertex shader file cannot replace a pixel shader).
bool LoadShaderReplacement(const wchar_t* folder, bool pixel, unsigned long long fingerprint,
	std::vector<DWORD>& out);

// Writes the bytecode to folder\(file name above), creating the folder. False when the file is
// already there (the same shader is created again on every Reset) or cannot be written.
bool DumpShaderFile(const wchar_t* folder, bool pixel, unsigned long long fingerprint,
	const void* data, size_t bytes);
