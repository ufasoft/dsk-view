// © 2023 Ufasoft https://ufasoft.com, Sergey Pavlov mailto:dev@ufasoft.com
// SPDX-License-Identifier: GPL-3.0-or-later
//
// DEC FILES-11 ODS-2 Filesystem driver (VMS)
//
// Based on
//		Kirby McCoy - VMS File System Internals - 1990
//		http://www.bitsavers.org/pdf/dec/vax/vms/training/EY-F575E-DP_VMS_File_System_Internals_1990.pdf

#include "pch.h"
#include "files11-volume.h"
#include "files11-def.h"

using namespace U::FS::Files11;

namespace U::FS {

class Files11ods2Volume : public Files11ods1Volume {
	typedef Files11ods1Volume base;

	static inline const DateTime c_file11Epoch = { 1858, 11, 17 };

	int MaxNameLength() override { return 80; }

	void LoadHomeBlock(const uint8_t home[512]) override {
		uint32_t homeLbn = load_little_u32(home)
			, alHomeLbn = load_little_u32(home + 4);
		if (homeLbn != 1 || alHomeLbn == 0)
			Throw(HRESULT_FROM_WIN32(ERROR_DISK_CORRUPT));
		if (home[13] != 2)
			Throw(errc::not_supported);
		SectorsPerCluster = load_little_u16(home + 14);
		BitmapLba = load_little_u32(home + 24);
		MaxNumberOfFiles = load_little_u32(home + 28);
		SectorsInBitmap = load_little_u16(home + 32);
	}

	static DateTime ParseOds2DateTime(const uint8_t d[8]) {
		return DateTime(c_file11Epoch.Ticks + load_little_u64(d));
	}

	vector<uint32_t> GetFileSectors(const DirEntry& e) override {
		vector<uint32_t> r;
		uint8_t data[512];
		Fs.Position = e.FirstCluster * BytesPerSector;
		Fs.ReadExactly(data, 512);
		const uint8_t* mapArea = data + data[1] * 2;
		for (int off = 0, end = data[58] * 2; off < end;) {
			uint16_t wl = load_little_u16(mapArea + off)
				, wh = load_little_u16(mapArea + off + 2);
			uint32_t lbn, count = 0;
			switch (wl >> 14) {
			case 0:
				off += 2;
				Throw(errc::not_supported);
				break;
			case 1:
				count = (wl & 0xFF) + 1;
				lbn = wh | ((uint32_t)(wl & 0x3F00) << 8);
				off += 4;
				break;
			case 2:
				count = (wl & 0x3FFF) + 1;
				lbn = load_little_u32(mapArea + off + 2);
				off += 6;
				break;
			case 3:
				count = wh | ((uint32_t)(wl & 0x3FFF) << 16);
				lbn = load_little_u32(mapArea + off + 4);
				off += 8;
				break;
			}
			for (int i = 0; i < count; ++i)
				r.push_back(lbn + i);
		}
		return r;
	}

	void LoadFileHeader(int sector) {
		uint8_t data[512];
		Fs.Position = sector * 512;
		Fs.ReadExactly(data, 512);
		uint16_t checksum = load_little_u16(data + 510);
		uint16_t fnum = load_little_u16(data + 8);
		uint32_t fcha = load_little_u32(data + 52);
		if (!fnum && !checksum && (fcha & Attr::FH2$M_MARKDEL)
			|| data[7] != 2)		// FH2$W_STRUCLEV major
			return;		// Deleted entry
		uint16_t sum = 0;
		for (int i = 0; i < 255; ++i)
			sum += load_little_u16(data + i * 2);
		if (sum != checksum) {
			TRC(1, "Wrong checksum");
			return;
		}
		if (!fnum)
			return;
		DirEntry e;
		const uint8_t* ident = data + data[0] * 2;
		e.FirstCluster = sector;
		e.Aux1 = fnum;
		e.FileName = (String((const char*)ident, 20) + String((const char*)ident + 54, 66)).Trim();
		e.IsDirectory = fcha & Attr::FH2$M_DIRECTORY;
		e.CreationTime = ParseOds2DateTime(ident + 22);
		e.LastWriteTime = ParseOds2DateTime(ident + 30);
		e.ExpirationTime = ParseOds2DateTime(ident + 38);
		e.BackupTime = ParseOds2DateTime(ident + 46);
		e.Length = (int64_t)GetFileSectors(e).size() * BytesPerSector;

		if (!AllDirEntries.insert(make_pair(fnum, e)).second)
			Throw(HRESULT_FROM_WIN32(ERROR_DISK_CORRUPT));
	}

	void LoadAllDirEntries() override {
		AllDirEntries.clear();
		int i;
		for (i = 0; i < (min)((uint32_t)16, MaxNumberOfFiles); ++i)
			LoadFileHeader(BitmapLba + SectorsInBitmap + i);
		auto sectors = GetFileSectors(AllDirEntries[FileNumIndex]);
		int off = 4 * SectorsPerCluster + SectorsInBitmap;
		for (int n = (min)(MaxNumberOfFiles, uint32_t(sectors.size() - off)); i < n; ++i)
			LoadFileHeader(sectors[off + i]);
	}

	vector<DirEntry> GetDirEntries(uint32_t fileNum, bool bWithExtra) override {
		MemoryStream ms;
		CopyFileTo(GetEntryByFileId(fileNum), ms);
		Span s = ms.AsSpan();
		vector<DirEntry> r;
		for (int off = 0; off < s.size();) {
			const uint8_t* p = s.data() + off;
			uint16_t size = load_little_u16(p);
			if (size == 0)
				break;
			if (size == 0xFFFF) {
				off = (off + 512) & 0xFFE00;
				continue;
			}
			const uint8_t* end = p + size + 2;
			if (size & 1)
				Throw(HRESULT_FROM_WIN32(ERROR_DISK_CORRUPT));
			uint8_t nameLen = p[5];
			String name((const char*)p + 6, nameLen);
			p = p + 6 + ((nameLen + 1) & 0xFE);
			for (; p < end; p += 8) {
				String fn = name;
				uint16_t ver = load_little_u16(p);
				if (ver > 1)
					fn += ";" + String(to_string(ver));
				uint32_t fid = load_little_u24(p + 2);
				DirEntry e = GetEntryByFileId(fid);
				if (e.Aux1 != fileNum) {
					e.AlternateFileName = e.FileName;
					e.FileName = fn;
					r.push_back(e);
				}
			}
			off += size + 2;
		}
		return r;
	}
};

static class Files11Ods2VolumeFactory : public IVolumeFactory {
	int IsSupportedVolume(RCSpan s) override {
		auto data = s.data();
		auto home = data + 512;
		if (s.size() < 1024)
			return 0;
		if (strncmp((const char*)home + 496, "DECFILE11B  ", 12)
			|| load_little_u32(home) != 1
			|| home[13] != 2)
			return 0;
		int weights = 2;
		if (load_little_u16(data) == 0240)						// PDP-11 NOP opcode
			weights += 1;
		return weights > 1 ? weights : 0;
	}

	unique_ptr<Volume> CreateInstance() override {
		return unique_ptr<Volume>(new Files11ods2Volume);
	}
} s_files11Ods2VolumeFactory;


} // U::FS
