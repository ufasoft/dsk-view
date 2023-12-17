// © 2023 Ufasoft https://ufasoft.com, Sergey Pavlov mailto:dev@ufasoft.com
// SPDX-License-Identifier: GPL-3.0-or-later
//
// DEC FILES-11 Filesystem driver
//
// Based on
//		Files-11 On-Disk Structure Specification - ODS-1
//		https://bitsavers.org/pdf/dec/pdp11/rsx11m_s/Files-11_ODS-1_Spec_Jun77.pdf

#include "pch.h"
#include "volume.h"

namespace U::FS {

static const char* const s_month[12]{
	"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};

int ParseMonth(RCString s) {
	for (int i = 0; i < 12; ++i)
		if (s == s_month[i])
			return i + 1;
	Throw(E_INVALIDARG);
}

static DateTime ParseFiles11DateTime(const char s[13]) {
	auto d = atoi(String(s, 2));
	auto month = ParseMonth(String(s + 2, 3));
	auto y = atoi(String(s + 5, 2));
	y += y < 70 ? 2000 : 1900;
	auto h = atoi(String(s + 7, 2));
	auto m = atoi(String(s + 9, 2));
	auto sec = atoi(String(s + 11, 2));
	return DateTime(y, month, d, h, m, sec);
}

// DirectoryEntry.Aux1: FileNum

class Files11Volume : public Volume {
	typedef Volume base;

	const int
		FileNumStorageBitmap = 2
		, FileNumMFD = 4;

	unordered_map<uint16_t, DirEntry> AllDirEntries;

	uint32_t BitmapLba = 0;
	uint16_t SectorsInBitmap = 0;
	uint16_t MaxNumberOfFiles = 0;
	int CurDirFileId = FileNumMFD;

	void Init(const path& filepath) override {
		base::Init(filepath);

		uint8_t home[512];
		Fs.Position = 512;
		Fs.ReadExactly(home, 512);

		SectorsInBitmap = load_little_u16(home);
		BitmapLba = ((uint32_t)load_little_u16(home + 2) << 16) | load_little_u16(home + 4);
		MaxNumberOfFiles = load_little_u16(home + 6);
		SectorsPerCluster = load_little_u16(home + 8);

		LoadAllDirEntries();
		Files = GetDirEntries(FileNumMFD, 0);
	}

	bool ReadHeaderSector(uint32_t sector, uint8_t data[512]) {
		Fs.Position = sector * 512;
		Fs.ReadExactly(data, 512);
		uint16_t sum = 0;
		for (int i = 0; i < 255; ++i)
			sum += load_little_u16(data + i * 2);
		if (sum != load_little_u16(data + 510)) {
			TRC(1, "Wrong checksum");
			return false;
		}
		return true;
	}

	String DecodeFileNameVer(const uint8_t d[10]) {
		String fn = (DecodeRadix50(load_little_u16(d)) + DecodeRadix50(load_little_u16(d + 2)) + DecodeRadix50(load_little_u16(d + 4))).Trim()
			+ "." + DecodeRadix50(load_little_u16(d + 6)).Trim();
		uint16_t ver = load_little_u16(d + 8);
		return ver > 1
			? fn + ";" + String(to_string(ver))
			: fn;
	}

	void LoadAllDirEntries() {
		AllDirEntries.clear();
		for (int i = 0; i < MaxNumberOfFiles; ++i) {
			uint8_t data[512];
			auto sector = BitmapLba + SectorsInBitmap + i;
			if (!ReadHeaderSector(sector, data))
				continue;
			uint16_t fnum = load_little_u16(data + 2);
			if (!fnum)
				continue;
			DirEntry e;
			uint8_t scha = data[13];
			const uint8_t * ident = data + data[0] * 2;
			e.FirstCluster = sector;
			e.Aux1 = fnum;
			e.FileName = DecodeFileNameVer(ident);
			e.IsDirectory = (scha & 0x20) || e.FileName.EndsWith(".DIR");
			const char *revDatetime = (const char*)ident + 12;
			if (*revDatetime)
				e.LastWriteTime = ParseFiles11DateTime(revDatetime);
			e.CreationTime = ParseFiles11DateTime((const char*)ident + 25);
			e.Length = (int64_t)GetFileSectors(e).size() * BytesPerSector;

			if (!AllDirEntries.insert(make_pair(fnum, e)).second)
				Throw(HRESULT_FROM_WIN32(ERROR_DISK_CORRUPT));
		}
	}

	const DirEntry& GetEntryByFileId(uint32_t fileId) {
		uint16_t fileNum = (uint16_t)fileId;
		auto it = AllDirEntries.find(fileNum);
		if (it != AllDirEntries.end())
			return it->second;
		Throw(errc::no_such_file_or_directory);
	}

	vector<uint32_t> GetFileSectors(const DirEntry& e) {
		vector<uint32_t> r;
		uint8_t data[512];
		Fs.Position = e.FirstCluster * BytesPerSector;
		Fs.ReadExactly(data, 512);
		const uint8_t *mapArea = data + data[1] * 2;
		uint8_t ctsz = mapArea[6], lbsz = mapArea[7];
		if ((ctsz + lbsz) & 1)
			Throw(HRESULT_FROM_WIN32(ERROR_DISK_CORRUPT));
		for (int off = 10, end = off + mapArea[8] * 2; off < end; off += ctsz + lbsz) {
			uint32_t lbn = load_little_u16(mapArea + off + 2);
			switch (lbsz) {
			case 3:
				lbn |= (uint32_t)mapArea[off] << 16;
				break;
			case 4:
				lbn = (lbn << 16) | load_little_u16(mapArea + off + 4);
				break;
			}
			for (int i = 0, count = 1 + (ctsz == 1 ? mapArea[off + 1] : load_little_u16(mapArea + off)); i < count; ++i)
				r.push_back(lbn + i);
		}
		return r;
	}

	void CopyFileTo(const DirEntry& fileEntry, Stream& os) override {
		auto sectors = GetFileSectors(fileEntry);
		for (auto sec : sectors) {
			uint8_t data[512];
			Fs.Position = (uint64_t)sec * 512;
			Fs.ReadExactly(data, 512);
			os.Write(data);
		}
	}

	vector<DirEntry> GetDirEntries(uint32_t fileNum, bool bWithExtra) override {
		MemoryStream ms;
		CopyFileTo(GetEntryByFileId(fileNum), ms);
		Span s = ms.AsSpan();
		vector<DirEntry> r;
		for (int off = 0; off < s.size(); off += 16) {
			const uint8_t *p = s.data() + off;
			if (uint16_t fn = load_little_u16(p)) {
				uint32_t fileId = fn | ((uint32_t)load_little_u16(p + 2) << 16);
				DirEntry e = GetEntryByFileId(fn);
				if (e.Aux1 != fileNum) {
					e.AlternateFileName = e.FileName;
					e.FileName = DecodeFileNameVer(p + 6);
					r.push_back(e);
				}
			}
		}
		return r;
	}

	void ChangeDirectory(RCString name) override {
		if (name == "/") {
			CurDirFileId = FileNumMFD;
			CurDirName = "/";
		}
		else if (name == "..") {
			CurDirFileId = FileNumMFD;
			CurDirName = "/";
		}
		else {
			CurDirFileId = GetEntry(name)->Aux1;
			CurDirName = name;
		}
		Files = GetDirEntries(CurDirFileId, false);
	}

	int64_t FreeSpace() override {
		MemoryStream ms;
		CopyFileTo(GetEntryByFileId(FileNumStorageBitmap), ms);
		Span s = ms.AsSpan();
		uint32_t sum = 0;
		for (int off = 512; off < s.size(); off += 4)
			sum += PopCount(load_little_u32(s.data() + off));
		return (int64_t)sum * SectorsPerCluster * BytesPerSector;
	}
};

static class Files11VolumeFactory : public IVolumeFactory {
	int IsSupportedVolume(RCSpan s) override {
		auto data = s.data();
		auto home = data + 512;
		if (s.size() < 1024)
			return 0;
		if (strncmp((const char*)home + 496, "DECFILE11A  ", 12)
			|| load_little_u32(home) != 1)
			return 0;
		int weights = 2;
		if (load_little_u16(data) == 0240)						// PDP-11 NOP opcode
			weights += 1;
		return weights > 1 ? weights : 0;
	}

	unique_ptr<Volume> CreateInstance() override {
		return unique_ptr<Volume>(new Files11Volume);
	}
} s_files11VolumeFactory;


} // U::FS
