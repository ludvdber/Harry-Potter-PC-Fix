// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// The frame of HP1 and HP2: where the renderer (d3d11drv.dll) sends each finished picture to the
// screen, and what is counted there. The engine ends every frame with URenderDevice::Unlock(Blit),
// slot 29 of the renderer's virtual table, which calls IDXGISwapChain::Present when Blit is set
// (READ in both builds: the swap chain is held at the same place, RVA 0x216B4, and the function
// is "cmp [esp+4],0 / je / call Present / ret 4" in both). A relay in that slot sees every frame
// before it goes out, and from there puts the panel, the screenshot and the frame-rate limit of
// d3d9.dll on IDXGISwapChain::Present itself (overlay11.cpp says why: the renderer's own Present
// function post-processes into the back buffer first, over anything drawn at Unlock).
//
// Two builds of the renderer are known, same version 1.6.2, linked three minutes apart on
// 2020-12-29: HP2's (TimeDateStamp 0x5FEB7C4A, table at 0x1B7B0) and HP1's (0x5FEB7D07, table at
// 0x1B798). Another build is left as it is.
//
// Everything here is plain arithmetic on memory the caller owns: tests/winmm_test.cpp runs it on
// fake images.

#pragma once

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace frame
{
constexpr size_t kSwapChain = 0x216B4;   // IDXGISwapChain*, both builds
constexpr size_t kContext = 0x2172C;     // ID3D11DeviceContext*, both builds (lumos.h reads it too)
constexpr int kUnlockSlot = 29;
constexpr uint32_t kSizeOfImage = 0x26000;

struct Build
{
	const char* game;
	uint32_t timeDateStamp;
	size_t vtable;
};

constexpr Build kBuilds[] = {
	{ "HP2", 0x5FEB7C4A, 0x1B7B0 },
	{ "HP1", 0x5FEB7D07, 0x1B798 },
};

// Unlock's own code: cmp dword ptr [esp+4],0 / je +5 / call rel32 / ret 4.
constexpr uint8_t kUnlockHead[7] = { 0x83, 0x7C, 0x24, 0x04, 0x00, 0x74, 0x05 };
constexpr uint8_t kUnlockTail[3] = { 0xC2, 0x04, 0x00 };

// The build of a loaded d3d11drv.dll, or nullptr: the PE header, the size, and the function the
// Unlock slot points at, which must be the one read.
inline const Build* Recognised(const uint8_t* image)
{
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return nullptr;
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.SizeOfImage != kSizeOfImage)
		return nullptr;
	for (const Build& b : kBuilds)
	{
		if (nt->FileHeader.TimeDateStamp != b.timeDateStamp)
			continue;
		const auto* unlock = *reinterpret_cast<const uint8_t* const*>(image + b.vtable + kUnlockSlot * sizeof(void*));
		if (unlock < image || unlock >= image + kSizeOfImage - 16)
			return nullptr;
		if (memcmp(unlock, kUnlockHead, sizeof(kUnlockHead)) != 0 || memcmp(unlock + 12, kUnlockTail, sizeof(kUnlockTail)) != 0)
			return nullptr;
		return &b;
	}
	return nullptr;
}

// ---- What a frame costs ------------------------------------------------------------------------
// Frame times in milliseconds, as the panel shows them: the rate and the mean time over the last
// half second, the "1% low" over the last 3000 frames (the rate of the slowest 1 %), and a graph
// of the last 240 frames. Pure arithmetic: the caller brings the clock.

class Stats
{
public:
	static constexpr int kGraph = 240;
	static constexpr int kLowWindow = 3000;

	// One frame sent at `now` (seconds). The first call only starts the clock.
	void Count(double now)
	{
		if (m_last >= 0)
		{
			const float ms = static_cast<float>((now - m_last) * 1000.0);
			m_graph[m_graphHead] = ms;
			m_graphHead = (m_graphHead + 1) % kGraph;
			m_window[m_windowHead] = ms;
			m_windowHead = (m_windowHead + 1) % kLowWindow;
			m_windowCount = (std::min)(m_windowCount + 1, kLowWindow);   // macro-proof: winmm.cpp has Windows' min
		}
		m_last = now;
		if (m_from < 0)
			m_from = now;
		++m_frames;
		const double span = now - m_from;
		if (span >= 0.5)   // the numbers change twice a second: readable, still current
		{
			m_fps = static_cast<float>(m_frames / span);
			m_frameMs = static_cast<float>(span * 1000.0 / m_frames);
			m_low1 = Low(m_window, m_windowCount, 99);
			m_from = now;
			m_frames = 0;
		}
	}

	float Fps() const { return m_fps; }
	float FrameMs() const { return m_frameMs; }
	float Low1() const { return m_low1; }
	// The graph's bars, oldest first; 0 where no frame has been counted yet.
	float Bar(int i) const { return m_graph[(m_graphHead + i) % kGraph]; }

	// The rate of the slowest (100 - percentile) % of `count` frame times, 0 with no frames.
	static float Low(const float* ms, int count, int percentile)
	{
		if (count <= 0)
			return 0;
		std::vector<float> sorted(ms, ms + count);
		auto nth = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size()) * percentile / 100;
		if (nth == sorted.end())
			--nth;
		std::nth_element(sorted.begin(), nth, sorted.end());
		return *nth > 0 ? 1000.0f / *nth : 0;
	}

private:
	float m_graph[kGraph] = {};
	int m_graphHead = 0;
	float m_window[kLowWindow] = {};
	int m_windowHead = 0, m_windowCount = 0;
	double m_last = -1, m_from = -1;   // -1: no frame yet (a clock may start at 0)
	int m_frames = 0;
	float m_fps = 0, m_frameMs = 0, m_low1 = 0;
};

// ---- Benchmark ---------------------------------------------------------------------------------
// Every frame time between the key press that starts it and the one that stops it; the summary
// is what d3d9.dll's overlay says, word for word, so the two read the same.

class Benchmark
{
public:
	bool Recording() const { return m_recording; }
	double StartedAt() const { return m_start; }
	const std::vector<float>& Frames() const { return m_frames; }

	void Start(double now)
	{
		m_recording = true;
		m_start = now;
		m_lastFrame = -1;
		m_frames.clear();
		m_frames.reserve(60 * 1000);
	}

	// A frame sent at `now` while recording.
	void Count(double now)
	{
		if (!m_recording)
			return;
		if (m_lastFrame >= 0)
			m_frames.push_back(static_cast<float>((now - m_lastFrame) * 1000.0));
		m_lastFrame = now;
	}

	// Stops and writes the summary line; false when there is nothing to say.
	bool Stop(double now, char* summary, size_t size)
	{
		m_recording = false;
		const double seconds = now - m_start;
		if (m_frames.empty() || seconds <= 0)
			return false;
		std::vector<float> sorted = m_frames;
		std::sort(sorted.begin(), sorted.end());
		const float p99 = sorted[sorted.size() * 99 / 100], p999 = sorted[sorted.size() * 999 / 1000], worst = sorted.back();
		const double avg = m_frames.size() / seconds;
		snprintf(summary, size, "Benchmark %.0f s: avg %.0f FPS, 1%% low %.0f, 0.1%% low %.0f, worst frame %.1f ms",
			seconds, avg, 1000.0f / p99, 1000.0f / p999, worst);
		return true;
	}

private:
	bool m_recording = false;
	double m_start = 0, m_lastFrame = -1;
	std::vector<float> m_frames;
};

// ---- Frame-rate limit --------------------------------------------------------------------------
// The slot of the next frame, in clock ticks: one period after the last slot, or now when the
// game is behind by more than a period (no catching up in a burst). Returns the ticks still to
// wait; the caller sleeps them and then takes the slot.

struct Limiter
{
	LONGLONG next = 0;

	LONGLONG Due(LONGLONG now, LONGLONG period)
	{
		if (!next || now - next > period)
			next = now;
		return next - now;
	}

	void Taken(LONGLONG period) { next += period; }
};
}
