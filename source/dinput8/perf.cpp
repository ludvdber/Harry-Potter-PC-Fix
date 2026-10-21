// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
#include "perf.h"
#include <algorithm>

namespace perf
{
void FrameClock::Frame(double now)
{
	if (m_lastFrame)
	{
		const float ms = static_cast<float>((now - m_lastFrame) * 1000.0);
		m_graph[m_graphHead] = ms;
		m_graphHead = (m_graphHead + 1) % kGraph;
		m_window[m_windowHead] = ms;
		m_windowHead = (m_windowHead + 1) % kLowWindow;
		m_windowCount = std::min(m_windowCount + 1, kLowWindow);
	}
	m_lastFrame = now;
	if (!m_statsFrom)
		m_statsFrom = now;
	++m_statsFrames;
	const double span = now - m_statsFrom;
	if (span >= 0.5)   // the numbers change twice a second: readable, still current
	{
		m_fps = static_cast<float>(m_statsFrames / span);
		m_frameMs = static_cast<float>(span * 1000.0 / m_statsFrames);
		std::vector<float> sorted(m_window, m_window + m_windowCount);
		if (!sorted.empty())
		{
			// The 1 % low: the frame rate of the slowest 1 % of frames, i.e. the 99th percentile time.
			auto nth = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() * 99 / 100);
			std::nth_element(sorted.begin(), nth, sorted.end());
			m_low1 = *nth > 0 ? 1000.0f / *nth : 0;
		}
		m_statsFrom = now;
		m_statsFrames = 0;
	}
}

void Benchmark::Start(double now)
{
	m_recording = true;
	m_start = now;
	m_last = 0;
	m_frames.clear();
	m_frames.reserve(60 * 1000);
}

void Benchmark::Frame(double now)
{
	if (!m_recording)
		return;
	if (m_last)
		m_frames.push_back(static_cast<float>((now - m_last) * 1000.0));
	m_last = now;
}

Summary Benchmark::Stop(double now)
{
	m_recording = false;
	return Summarise(m_frames, now - m_start);
}

Summary Summarise(const std::vector<float>& ms, double seconds)
{
	Summary s;
	if (ms.empty() || seconds <= 0)
		return s;
	std::vector<float> sorted = ms;
	std::sort(sorted.begin(), sorted.end());
	const float p99 = sorted[sorted.size() * 99 / 100], p999 = sorted[sorted.size() * 999 / 1000];
	s.valid = true;
	s.seconds = seconds;
	s.frames = ms.size();
	s.avgFps = ms.size() / seconds;
	s.low1Fps = p99 > 0 ? 1000.0f / p99 : 0;
	s.low01Fps = p999 > 0 ? 1000.0f / p999 : 0;
	s.worstMs = sorted.back();
	return s;
}

void Describe(char* out, size_t size, const Summary& s)
{
	snprintf(out, size, "Benchmark %.0f s: avg %.0f FPS, 1%% low %.0f, 0.1%% low %.0f, worst frame %.1f ms",
		s.seconds, s.avgFps, s.low1Fps, s.low01Fps, s.worstMs);
}

void WriteCsv(FILE* f, const Summary& s, const std::vector<float>& ms)
{
	char line[160];
	Describe(line, sizeof(line), s);
	// fps_limit: the overlay of d3d9.dll writes its own limiter's value here; HP3 holds itself at
	// 60 in its engine (notes/HP3.md), nothing of ours limits it.
	fprintf(f, "# %s\n# fps_limit=0\nframe,ms\n", line);
	for (size_t i = 0; i < ms.size(); ++i)
		fprintf(f, "%zu,%.3f\n", i + 1, ms[i]);
}
}
