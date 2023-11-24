// Copyright(c) 2023 Ufasoft  http://ufasoft.com  mailto:support@ufasoft.com,  Sergey Pavlov  mailto:dev@ufasoft.com
//
// SPDX-License-Identifier: GPL-2.0-or-later
//
//	Filesystem of MK-DOS for soviet computer BK-0010/0011
//	Based on description of Gid's Emulator BKDE source code

#include "pch.h"
#include "volume.h"
#include "bk-volume.h"

using namespace std;

namespace U::FS {

class MkdosVolume : public BkVolume<Volume> {
	typedef BkVolume<Volume> base;
public:
	enum class EntryStatus : uint8_t {
		Normal = 0
		, ReadOnly = 1
		, LogicalDisk = 2
		, Bad = 0x80
		, Deleted = 0xFF
	};

	static const size_t EntrySize = 24
		, MaxDirEntries = 173 - 1;

	uint16_t CurDirEntries
		, TotalUsedSectors
		, DirectorySector;

	void Init(const path& filepath) override {
		base::Init(filepath);

		array<uint8_t, 512> buf;
		auto data = buf.data();
		Fs.ReadExactly(buf.data(), buf.size());

		CurDirEntries = load_little_u16(data + 030);
		TotalUsedSectors = load_little_u16(data + 032);
		TotalSectors = load_little_u16(data + 0466);
		FirstDataCluster = load_little_u16(data + 0470);

		Files = GetDirEntries(0, false);
	}

	int MaxNameLength() override { return 14; }

	void RemoveFile(RCString filename) {
		EnsureWriteMode();
		auto it = GetEntry(filename);
		Fs.Position = it->DirEntryDiskOffset + 1;
		uint8_t statusDeleted = (uint8_t)EntryStatus::Deleted;
		Fs.WriteBuffer(&statusDeleted, 1);
		Files.erase(it);
	}
protected:
	vector<DirEntry> GetDirEntries(uint32_t cluster, bool bWithExtra) override {
		Fs.Position = 0500;
		vector<uint8_t> rootDir(MaxDirEntries * EntrySize);
		Fs.ReadExactly(rootDir.data(), rootDir.size());
		vector<DirEntry> r;
		auto p = rootDir.data();
		uint8_t dirId = 0;
		for (int i = 0, j = 0; j < CurDirEntries && i < MaxDirEntries; ++i, p += EntrySize) {
			DirEntry e;
			auto status = (EntryStatus)p[0];
			e.IsDirectory = p[2] == 0x7F;
			if (e.IsDirectory)
				e.Aux1 = ++dirId;
			switch (status) {
			case EntryStatus::LogicalDisk:
			case EntryStatus::Bad:
				++j;
			case EntryStatus::Deleted:
				break;
			default:
				++j;
				e.Aux2 = p[1];
				e.ReadOnly = status == EntryStatus::ReadOnly;
				if (e.Aux2 == CurDirId || bWithExtra) {
					Span spanName = Span(p + 2, 14);
					e.FileName = GetFilenamePart(e.IsDirectory ? spanName.subspan(1) : spanName);
					e.DirEntryDiskOffset = 0500 + (p - rootDir.data());
					e.FirstCluster = load_little_u16(p + 16);
					e.AllocationSize = (uint32_t)load_little_u16(p + 18) * BytesPerSector;
					e.Length = e.AllocationSize
						? (e.AllocationSize - BytesPerSector) | load_little_u16(p + 22)
						: 0;
					e.Aux3 = load_little_u16(p + 20);		// address
					r.push_back(e);
				}
				break;
			}
		}
		return r;
	}

	void Serialize(Stream& stm, const DirEntry& e) override {
		uint8_t buf[EntrySize] = { 0 };
		if (e.Empty)
			buf[0] = 0xFF;
		else {
			buf[0] = e.ReadOnly ? 1 : 0;
			buf[1] = (uint8_t)e.Aux2;

			strncpy((char*)buf + 2, e.FileName, 14);
			if (!e.IsDirectory) {
				buf[13] = (uint8_t)e.Aux1;
				store_little_u16(buf + 16, (uint16_t)e.FirstCluster);
				auto nSectors = (uint16_t)CalcNumberOfClusters(e.Length);
				store_little_u16(buf + 18, nSectors);
				store_little_u16(buf + 20, (uint16_t)e.Aux3);		// Load Address
				store_little_u16(buf + 22, (uint16_t)e.Length);
			}
		}
		stm.WriteBuffer(buf, sizeof(buf));
	}

	void SaveDirStreamToVolume(Stream& stm) override {
		Fs.Position = 0500;
		stm.CopyTo(Fs);
	}

	void SaveDirEntries(const CFiles& entries) override {
		if (entries.size() > MaxDirEntries)
			Throw(HRESULT_FROM_WIN32(ERROR_CANNOT_MAKE));
		base::SaveDirEntries(entries);
		uint16_t usedSectors = 0;
		for (const auto& e : entries)
			if (!e.Empty)
				usedSectors += (uint16_t)CalcNumberOfClusters(e.Length);
		uint8_t bufMetaData[4];
		store_little_u16(bufMetaData, (uint16_t)entries.size());
		store_little_u16(bufMetaData + 2, usedSectors);		
		Fs.Write(030, bufMetaData);
	}

	int64_t FreeSpace() {
		return uint32_t(TotalSectors - TotalUsedSectors) * BytesPerSector;
	}
};

static class MkdosVolumeFactory : public IVolumeFactory {
	const uint16_t c_sigMicroDos = 0123456
		, c_sigMkdos = 051414;

	int IsSupportedVolume(RCSpan s) override {
		auto data = s.data();
		if (s.size() < 1024)
			return 0;
		if (load_little_u16(data + 0400) != c_sigMicroDos || load_little_u16(data + 0402) != c_sigMkdos)
			return 0;
		int weight = 2;
		if (load_little_u16(data) == 0240)						// PDP-11 NOP opcode
			weight += 1;
		return weight;
	}

	unique_ptr<Volume> CreateInstance() override {
		return unique_ptr<Volume>(new MkdosVolume);
	}
} s_mkdosVolumeFactory;


} // U::FS
