// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Textures by fingerprint. The fingerprint is FNV-1a, 64 bits, over the size, the format and the
// level-0 bytes row by row (pitch padding left out): the same image gets the same name on every PC
// and every run, which is what a replacement pack keys on. The algorithm is frozen; changing it
// would orphan every pack made with it.

#include "texdump.h"
#include "accio.h"
#include "mipmaps.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	struct Layout
	{
		DWORD fourCC = 0;       // DXTn, or 0 for an uncompressed format
		UINT  blockBytes = 0;   // bytes per 4x4 block (compressed)
		UINT  bits = 0;         // bits per pixel (uncompressed)
		DWORD r = 0, g = 0, b = 0, a = 0;
	};

	bool Describe(D3DFORMAT f, Layout& l)
	{
		switch (f)
		{
		case D3DFMT_DXT1: l.fourCC = MAKEFOURCC('D', 'X', 'T', '1'); l.blockBytes = 8; return true;
		case D3DFMT_DXT2: l.fourCC = MAKEFOURCC('D', 'X', 'T', '2'); l.blockBytes = 16; return true;
		case D3DFMT_DXT3: l.fourCC = MAKEFOURCC('D', 'X', 'T', '3'); l.blockBytes = 16; return true;
		case D3DFMT_DXT4: l.fourCC = MAKEFOURCC('D', 'X', 'T', '4'); l.blockBytes = 16; return true;
		case D3DFMT_DXT5: l.fourCC = MAKEFOURCC('D', 'X', 'T', '5'); l.blockBytes = 16; return true;
		case D3DFMT_A8R8G8B8: l.bits = 32; l.r = 0xFF0000; l.g = 0xFF00; l.b = 0xFF; l.a = 0xFF000000; return true;
		case D3DFMT_X8R8G8B8: l.bits = 32; l.r = 0xFF0000; l.g = 0xFF00; l.b = 0xFF; return true;
		case D3DFMT_R5G6B5:   l.bits = 16; l.r = 0xF800; l.g = 0x7E0; l.b = 0x1F; return true;
		case D3DFMT_A1R5G5B5: l.bits = 16; l.r = 0x7C00; l.g = 0x3E0; l.b = 0x1F; l.a = 0x8000; return true;
		case D3DFMT_A4R4G4B4: l.bits = 16; l.r = 0xF00; l.g = 0xF0; l.b = 0xF; l.a = 0xF000; return true;
		case D3DFMT_L8:       l.bits = 8;  l.r = 0xFF; return true;
		case D3DFMT_A8:       l.bits = 8;  l.a = 0xFF; return true;
		default: return false;
		}
	}

	UINT RowBytes(const Layout& l, UINT w) { return l.fourCC ? ((w + 3) / 4) * l.blockBytes : w * l.bits / 8; }
	UINT RowCount(const Layout& l, UINT h) { return l.fourCC ? (h + 3) / 4 : h; }

	std::mutex g_lock;
	std::set<uint64_t> g_saved;
	std::unordered_map<uint64_t, std::string> g_packs;   // fingerprint -> replacement file
	// Its own lock: a texture's Release runs through Released, and may happen while g_lock is held.
	std::mutex g_swapLock;
	// The game makes several textures of the same content: one HD texture serves them all, built once.
	struct Hd
	{
		IDirect3DTexture9* big = nullptr;
		int users = 0;
	};
	std::unordered_map<uint64_t, Hd> g_hd;   // fingerprint -> HD replacement
	struct Swapped
	{
		IDirect3DTexture9* big;
		uint64_t hash;
	};
	std::unordered_map<IDirect3DBaseTexture9*, Swapped> g_swaps;   // game texture -> its HD replacement

	// tex now shows big (one more user); false if tex already had a replacement.
	bool Attach(IDirect3DBaseTexture9* tex, uint64_t hash, IDirect3DTexture9* big)
	{
		std::lock_guard<std::mutex> swaps(g_swapLock);
		if (!g_swaps.emplace(tex, Swapped{ big, hash }).second)
			return false;
		Hd& hd = g_hd[hash];
		hd.big = big;
		++hd.users;
		return true;
	}

	// tex shows the HD texture already built for an earlier texture of the same content, if any.
	bool AttachShared(IDirect3DBaseTexture9* tex, uint64_t hash)
	{
		std::lock_guard<std::mutex> swaps(g_swapLock);
		const auto it = g_hd.find(hash);
		if (it == g_hd.end() || !g_swaps.emplace(tex, Swapped{ it->second.big, hash }).second)
			return false;
		++it->second.users;
		return true;
	}
	bool g_scanned = false;

	std::string GameFolder()
	{
		char exe[MAX_PATH] = {};
		GetModuleFileNameA(nullptr, exe, MAX_PATH);
		std::string dir(exe);
		return dir.substr(0, dir.find_last_of("\\/") + 1);
	}

	// Replacement files: any name ending in _<16 hex digits>.dds (the dump's own names work as they are).
	void ScanPacks()
	{
		g_scanned = true;
		const std::string dir = GameFolder() + "accio_remplacements\\";
		WIN32_FIND_DATAA f;
		HANDLE h = FindFirstFileA((dir + "*.dds").c_str(), &f);
		if (h == INVALID_HANDLE_VALUE)
			return;
		do
		{
			std::string n = f.cFileName;
			if (n.size() < 21)
				continue;
			const std::string hex = n.substr(n.size() - 20, 16);
			char* end = nullptr;
			const unsigned long long v = strtoull(hex.c_str(), &end, 16);
			if (end == hex.c_str() + 16)
				g_packs[v] = dir + n;
		} while (FindNextFileA(h, &f));
		FindClose(h);
		Log("Textures: %u replacement(s) found in %s\n", static_cast<unsigned>(g_packs.size()), dir.c_str());
	}

	void Fnv(uint64_t& h, const void* data, size_t n)
	{
		const auto* p = static_cast<const unsigned char*>(data);
		for (size_t i = 0; i < n; ++i)
		{
			h ^= p[i];
			h *= 0x100000001B3ull;
		}
	}

	void WriteDds(const std::string& name, const D3DSURFACE_DESC& d, const Layout& l, const D3DLOCKED_RECT& r)
	{
		FILE* f = nullptr;
		if (fopen_s(&f, name.c_str(), "wb") != 0 || !f)
			return;
		const UINT rows = RowCount(l, d.Height), rowBytes = RowBytes(l, d.Width);
		DWORD hdr[31] = {};
		hdr[0] = 124;
		hdr[1] = 0x1007 | (l.fourCC ? 0x80000 : 0x8);   // caps, height, width, pixel format + linear size or pitch
		hdr[2] = d.Height;
		hdr[3] = d.Width;
		hdr[4] = l.fourCC ? rowBytes * rows : rowBytes;
		hdr[18] = 32;                                    // pixel format: its own size
		if (l.fourCC)
			hdr[19] = 0x4;                               // FOURCC
		else if (l.r | l.g | l.b)
			hdr[19] = (l.r && !l.g ? 0x20000 : 0x40) | (l.a ? 0x1 : 0);   // LUMINANCE or RGB, + ALPHAPIXELS
		else
			hdr[19] = 0x2;                               // ALPHA only
		hdr[20] = l.fourCC;
		hdr[21] = l.bits;
		hdr[22] = l.r; hdr[23] = l.g; hdr[24] = l.b; hdr[25] = l.a;
		hdr[26] = 0x1000;                                // caps: texture
		fwrite("DDS ", 1, 4, f);
		fwrite(hdr, sizeof(hdr), 1, f);
		for (UINT y = 0; y < rows; ++y)
			fwrite(static_cast<const BYTE*>(r.pBits) + static_cast<size_t>(y) * r.Pitch, 1, rowBytes, f);
		fclose(f);
	}

	bool PeekSize(const std::string& path, UINT& w, UINT& h)
	{
		FILE* f = nullptr;
		if (fopen_s(&f, path.c_str(), "rb") != 0 || !f)
			return false;
		char magic[4] = {};
		DWORD hdr[31] = {};
		const bool ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, "DDS ", 4) == 0 && fread(hdr, sizeof(hdr), 1, f) == 1;
		fclose(f);
		w = hdr[3];
		h = hdr[2];
		return ok && w && h && w <= 8192 && h <= 8192;
	}

	// The levels below 0 of an HD texture, read from the file when it carries the whole chain in the
	// game's format (texconv writes it). False otherwise: the caller builds the chain itself.
	bool ReadLowerLevels(const std::string& path, IDirect3DTexture9* big, const Layout& l)
	{
		FILE* f = nullptr;
		if (fopen_s(&f, path.c_str(), "rb") != 0 || !f)
			return false;
		char magic[4] = {};
		DWORD hdr[31] = {};
		bool ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, "DDS ", 4) == 0 && fread(hdr, sizeof(hdr), 1, f) == 1;
		const UINT levels = big->GetLevelCount();
		ok = ok && (hdr[1] & 0x20000) && hdr[6] >= levels   // DDSD_MIPMAPCOUNT, enough levels
			&& (l.fourCC ? (hdr[19] & 0x4) && hdr[20] == l.fourCC : !(hdr[19] & 0x4) && hdr[21] == l.bits);
		ok = ok && fseek(f, static_cast<long>(RowCount(l, hdr[2]) * RowBytes(l, hdr[3])), SEEK_CUR) == 0;
		for (UINT i = 1; ok && i < levels; ++i)
		{
			D3DSURFACE_DESC d = {};
			D3DLOCKED_RECT r = {};
			ok = SUCCEEDED(big->GetLevelDesc(i, &d)) && SUCCEEDED(big->LockRect(i, &r, nullptr, 0));
			if (!ok)
				break;
			const UINT rows = RowCount(l, d.Height), rowBytes = RowBytes(l, d.Width);
			for (UINT y = 0; ok && y < rows; ++y)
				ok = fread(static_cast<BYTE*>(r.pBits) + static_cast<size_t>(y) * r.Pitch, 1, rowBytes, f) == rowBytes;
			big->UnlockRect(i);
		}
		fclose(f);
		return ok;
	}

	// A replacement read as the game's own format (same layout, copied as is) or as 32-bit RGBA
	// (encoded). False when the file is not one of those, or not the texture's size.
	bool ReadReplacement(const std::string& path, const D3DSURFACE_DESC& d, const Layout& l, D3DLOCKED_RECT& r)
	{
		FILE* f = nullptr;
		if (fopen_s(&f, path.c_str(), "rb") != 0 || !f)
			return false;
		char magic[4] = {};
		DWORD hdr[31] = {};
		bool ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, "DDS ", 4) == 0 && fread(hdr, sizeof(hdr), 1, f) == 1;
		ok = ok && hdr[2] == d.Height && hdr[3] == d.Width;   // the caller sized the target after PeekSize
		if (ok)
		{
			const bool sameCompressed = l.fourCC && (hdr[19] & 0x4) && hdr[20] == l.fourCC;
			const bool sameRaw = !l.fourCC && !(hdr[19] & 0x4) && hdr[21] == l.bits && hdr[22] == l.r && hdr[23] == l.g
				&& hdr[24] == l.b && hdr[25] == l.a;
			const bool rgba32 = !(hdr[19] & 0x4) && hdr[21] == 32 && hdr[22] == 0xFF0000 && hdr[23] == 0xFF00 && hdr[24] == 0xFF;
			if (sameCompressed || sameRaw)
			{
				const UINT rows = RowCount(l, d.Height), rowBytes = RowBytes(l, d.Width);
				for (UINT y = 0; ok && y < rows; ++y)
					ok = fread(static_cast<BYTE*>(r.pBits) + static_cast<size_t>(y) * r.Pitch, 1, rowBytes, f) == rowBytes;
			}
			else if (rgba32 && mips::Supported(d.Format))
			{
				std::vector<BYTE> raw(static_cast<size_t>(d.Width) * d.Height * 4);
				ok = fread(raw.data(), 1, raw.size(), f) == raw.size();
				std::vector<mips::Rgba> px(static_cast<size_t>(d.Width) * d.Height);
				const bool alpha = hdr[25] == 0xFF000000;
				for (size_t i = 0; ok && i < px.size(); ++i)   // B, G, R, A in the file
					px[i] = { raw[i * 4 + 2], raw[i * 4 + 1], raw[i * 4], alpha ? raw[i * 4 + 3] : uint8_t(255) };
				ok = ok && mips::Encode(d.Format, px, static_cast<int>(d.Width), static_cast<int>(d.Height), r.pBits, r.Pitch);
			}
			else
			{
				Log("Textures: %s is neither the texture's format nor 32-bit RGBA: not applied\n", path.c_str());
				ok = false;
			}
		}
		fclose(f);
		return ok;
	}
}

namespace texdump
{
	bool Wanted(DWORD usage, D3DPOOL pool)
	{
		return pool != D3DPOOL_DEFAULT
			&& !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL | D3DUSAGE_DYNAMIC));
	}

	IDirect3DBaseTexture9* Swap(IDirect3DBaseTexture9* tex)
	{
		std::lock_guard<std::mutex> guard(g_swapLock);
		if (g_swaps.empty())
			return tex;
		const auto it = g_swaps.find(tex);
		return it == g_swaps.end() ? tex : it->second.big;
	}

	void Released(IDirect3DTexture9* tex)
	{
		IDirect3DTexture9* big = nullptr;
		{
			std::lock_guard<std::mutex> guard(g_swapLock);
			const auto it = g_swaps.find(tex);
			if (it == g_swaps.end())
				return;
			const uint64_t hash = it->second.hash;
			g_swaps.erase(it);
			const auto hd = g_hd.find(hash);
			if (hd == g_hd.end() || --hd->second.users > 0)
				return;
			big = hd->second.big;
			g_hd.erase(hd);
		}
		big->Release();   // outside the lock: its own Release comes back through Released
	}

	void FirstFill(IDirect3DTexture9* tex, bool dump, bool replace)
	{
		D3DSURFACE_DESC d = {};
		Layout l;
		if (FAILED(tex->GetLevelDesc(0, &d)) || !Describe(d.Format, l))
			return;
		D3DLOCKED_RECT r = {};
		if (FAILED(tex->LockRect(0, &r, nullptr, D3DLOCK_READONLY)))
			return;
		uint64_t h = 0xCBF29CE484222325ull;
		const UINT head[3] = { d.Width, d.Height, static_cast<UINT>(d.Format) };
		Fnv(h, head, sizeof(head));
		const UINT rows = RowCount(l, d.Height), rowBytes = RowBytes(l, d.Width);
		for (UINT y = 0; y < rows; ++y)
			Fnv(h, static_cast<const BYTE*>(r.pBits) + static_cast<size_t>(y) * r.Pitch, rowBytes);

		std::lock_guard<std::mutex> guard(g_lock);
		if (dump && g_saved.insert(h).second)
		{
			static const std::string folder = [] {
				const std::string dir = GameFolder() + "accio_textures";
				CreateDirectoryA(dir.c_str(), nullptr);
				return dir;
			}();
			char name[MAX_PATH];
			snprintf(name, sizeof(name), "%s\\%ux%u_%d_%016llx.dds", folder.c_str(), d.Width, d.Height,
				static_cast<int>(d.Format), static_cast<unsigned long long>(h));
			if (GetFileAttributesA(name) == INVALID_FILE_ATTRIBUTES)
			{
				WriteDds(name, d, l, r);
				static int count = 0;
				if (++count % 100 == 1)
					Log("Textures: %d saved so far in %s\n", count, folder.c_str());
			}
		}
		tex->UnlockRect(0);

		if (!replace)
			return;
		if (!g_scanned)
			ScanPacks();
		const auto it = g_packs.find(h);
		if (it == g_packs.end())
			return;
		const mips::Filter filter = g_cfg.mipmapFilter == 1 ? mips::Filter::Sharp : mips::Filter::Tent;
		UINT pw = 0, ph = 0;
		bool ok = PeekSize(it->second, pw, ph);
		if (ok && (pw != d.Width || ph != d.Height) && AttachShared(tex, h))
			return;
		if (ok && pw == d.Width && ph == d.Height)
		{
			if (FAILED(tex->LockRect(0, &r, nullptr, 0)))
				return;
			ok = ReadReplacement(it->second, d, l, r);
			tex->UnlockRect(0);
			if (ok && tex->GetLevelCount() > 1)
				mips::FillChain(tex, filter, g_cfg.mipmapCoverage);
		}
		else if (ok)
		{
			// An HD texture: its own texture, full chain, same format as the game's.
			IDirect3DDevice9* dev = nullptr;
			IDirect3DTexture9* big = nullptr;
			ok = SUCCEEDED(tex->GetDevice(&dev)) && dev && mips::Supported(d.Format)
				&& SUCCEEDED(dev->CreateTexture(pw, ph, 0, 0, d.Format, D3DPOOL_MANAGED, &big, nullptr)) && big;
			if (ok)
			{
				D3DSURFACE_DESC bd = {};
				D3DLOCKED_RECT br = {};
				ok = SUCCEEDED(big->GetLevelDesc(0, &bd)) && SUCCEEDED(big->LockRect(0, &br, nullptr, 0));
				if (ok)
				{
					ok = ReadReplacement(it->second, bd, l, br);
					big->UnlockRect(0);
				}
				if (ok && big->GetLevelCount() > 1 && !ReadLowerLevels(it->second, big, l))
					mips::FillChain(big, filter, g_cfg.mipmapCoverage);
				if (!ok || !Attach(tex, h, big))
					big->Release();
			}
			if (dev)
				dev->Release();
		}
		Log("Textures: %016llx %s from %s (%ux%u for %ux%u)\n", static_cast<unsigned long long>(h),
			ok ? "replaced" : "NOT replaced", it->second.c_str(), pw, ph, d.Width, d.Height);
	}
}
