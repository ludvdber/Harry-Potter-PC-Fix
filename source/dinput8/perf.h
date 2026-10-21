// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Frame times and benchmarks, pure arithmetic (tests/dinput8_test.cpp): the frame counter and the
// recording of screen.cpp, in the numbers and the file layout of the d3d9.dll's overlay
// (overlay.cpp), so one benchmark reads like the other.
#pragma once
#include <cstddef>
#include <cstdio>
#include <vector>

namespace perf
{
// The frame rate as shown: refreshed twice a second, with the "1 % low" over the last 3000 frames.
class FrameClock
{
public:
	static constexpr int kGraph = 240;        // bars of the frame-time graph
	static constexpr int kLowWindow = 3000;   // frames behind the 1 % low

	// One frame presented at `now` (seconds, any origin).
	void Frame(double now);

	float Fps() const { return m_fps; }
	float FrameMs() const { return m_frameMs; }
	float Low1() const { return m_low1; }
	// The last kGraph frame times, oldest first (0 where none yet).
	float Graph(int i) const { return m_graph[(m_graphHead + i) % kGraph]; }
	bool Any() const { return m_lastFrame != 0; }

private:
	float m_graph[kGraph] = {};
	int m_graphHead = 0;
	float m_window[kLowWindow] = {};
	int m_windowHead = 0, m_windowCount = 0;
	double m_lastFrame = 0, m_statsFrom = 0;
	int m_statsFrames = 0;
	float m_fps = 0, m_frameMs = 0, m_low1 = 0;
};

struct Summary
{
	bool valid = false;      // at least one frame over a positive span
	double seconds = 0;
	double avgFps = 0;
	float low1Fps = 0;       // 1000 / the 99th percentile frame time
	float low01Fps = 0;      // 1000 / the 99.9th percentile
	float worstMs = 0;
	size_t frames = 0;
};

// A recording between two presses of the benchmark key.
class Benchmark
{
public:
	bool Recording() const { return m_recording; }
	double Since(double now) const { return now - m_start; }
	void Start(double now);
	// One frame presented at `now` while recording.
	void Frame(double now);
	Summary Stop(double now);
	const std::vector<float>& Frames() const { return m_frames; }

private:
	bool m_recording = false;
	double m_start = 0, m_last = 0;
	std::vector<float> m_frames;   // milliseconds
};

Summary Summarise(const std::vector<float>& ms, double seconds);
// "Benchmark 30 s: avg 60 FPS, 1% low 55, 0.1% low 40, worst frame 31.2 ms"
void Describe(char* out, size_t size, const Summary& s);
// The benchmarks\*.csv of the overlay: two comment lines, then frame,ms.
void WriteCsv(FILE* f, const Summary& s, const std::vector<float>& ms);
}
