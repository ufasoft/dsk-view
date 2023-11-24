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
class CsidosVolume : public BkVolume<Volume> {
	typedef BkVolume<Volume> base;
public:
	enum class EntryStatus : uint8_t {
		End = 0
		, Bad = 0xC9
		, DeletedDir = 0xCA
		, Empty = 0xFE
		, Deleted = 0xFF
	};

	static const size_t EntrySize = 20
		, MaxDirEntries = 8 * (500 / EntrySize) - 1;

	CsidosVolume() {
		RootDirId = 1;
		FirstDataCluster = 10;
	}
protected:
	vector<DirEntry> GetDirEntries(uint32_t dirId, bool bWithExtra) override {
		vector<DirEntry> r;
		vector<uint8_t> buf(BytesPerSector);
		for (int sector = 2; sector <= 9; ++sector) {
			Fs.Position = sector * BytesPerSector;
			Fs.ReadExactly(buf.data(), buf.size());
			const uint8_t* p = buf.data();
			if (p[0] != sector)					// Corrupted
				continue;
			p += 12;
			for (int i = 0; i < (BytesPerSector - 12) / EntrySize; ++i, p += EntrySize) {
				switch (EntryStatus status = (EntryStatus)p[0]) {
				case EntryStatus::End:
					goto LAB_END;
				case EntryStatus::Deleted:
				case EntryStatus::DeletedDir:
					break;
				default:
					if (p[0] < 0xC9 && (p[0] == dirId || bWithExtra)) {				// Normal
						DirEntry e;
						e.Aux2 = p[0];
						e.ReadOnly = p[1] & 0x80;
						e.IsDirectory = !p[10];
						auto fn = GetFilenamePart(Span(p + 2, 8));
						e.Aux1 = p[13];
						if (e.IsDirectory) {
							e.FileName = fn;
							e.FirstCluster = p[13];		// DirId, copy of Aux1
						} else {
							auto ext = GetFilenamePart(Span(p + 10, 3));
							e.FileName = ext != "   " ? fn + "." + ext : fn;
							e.Length = load_little_u16(p + 18);
							if (e.Length)
								e.AllocationSize = (e.Length | 511) + 1;
							e.FirstCluster = load_little_u16(p + 14);
						}
						e.Aux3 = load_little_u16(p + 16);			// Load Address
						r.push_back(e);
					}
				}
			}
		}
LAB_END:
		return r;
	}

	void Serialize(Stream& stm, const DirEntry& e) override {
		uint8_t buf[EntrySize] = { 0 };
		if (e.Empty)
			buf[0] = 0xFE;
		else {
			if (e.IsDirectory)
				buf[0] = (uint8_t)e.Aux2;
			buf[1] = e.ReadOnly ? 0xFF : 0;
			auto split = Path::SplitPath(e.FileName.c_wstr());
			if (split.m_ext.StartsWith("."))
				split.m_ext = split.m_ext.substr(1);
			strncpy((char*)buf + 1, split.m_fname, 8);
			if (e.IsDirectory)
				strncpy((char*)buf + 1, split.m_fname, 8);
			else {
				split.m_fname = split.m_fname.ToLower();
				split.m_ext = split.m_ext.ToLower();
				strncpy((char*)buf + 1, split.m_fname, 8);
				strncpy((char*)buf + 9, split.m_ext, 3);
				buf[13] = (uint8_t)e.Aux1;
				if (!e.IsDirectory)
					store_little_u16(buf + 14, (uint16_t)e.FirstCluster);
				store_little_u16(buf + 16, (uint16_t)e.Aux3);		// Load Address
				store_little_u16(buf + 18, (uint16_t)e.Length);
			}
		}
		stm.WriteBuffer(buf, sizeof(buf));
	}

	void SaveDirStreamToVolume(Stream& stm) override {
		for (int n = int(stm.Length + 499) / 500, i = 0; i < n; ++i) {
			uint8_t buf[500];
			ZeroStruct(buf);
			stm.Read(buf, sizeof(buf));
			Fs.Write((2 + i) * BytesPerSector + 12, buf);
		}
	}

	void SaveDirEntries(const CFiles& entries) override {
		if (entries.size() > MaxDirEntries)
			Throw(HRESULT_FROM_WIN32(ERROR_CANNOT_MAKE));
		base::SaveDirEntries(entries);
	}

	void RemoveFile(RCString filename) override {
		RemoveFileChecks(filename);
		auto& e = *GetEntry(filename);
		uint8_t deletedMark = (uint8_t)EntryStatus::Deleted;
		Fs.Write(e.DirEntryDiskOffset, Span(&deletedMark, 1));
	}

	void Init(const path& filepath) override {
		base::Init(filepath);

		uint8_t buf[512];
		Fs.Position = 1024;
		Fs.ReadExactly(buf, sizeof(buf));
		TotalSectors = load_little_u16(buf + 2);

		CurDirId = 1;
		Files = GetDirEntries(CurDirId, false);
	}

	int64_t FreeSpace() {
		uint64_t r = TotalSectors;
		for (const auto& e : Files)
			r -= CalcNumberOfClusters(e.Length);
		return r * BytesPerSector;
	}

	int MaxNameLength() override { return 12; }
};

static class CsidosVolumeFactory : public IVolumeFactory {
	const uint16_t c_sigCsiDos = 0123123;

	int IsSupportedVolume(RCSpan s) override {
		auto data = s.data();
		if (s.size() < 1536)
			return 0;
		if (load_little_u16(data + 02000) != 2 || load_little_u16(data + 02010) != c_sigCsiDos)
			return 0;
		int weight = 2;
		if (load_little_u16(data + 02004) == c_sigCsiDos && load_little_u16(data + 02006)== c_sigCsiDos)
			weight += 3;
		if (load_little_u16(data) == 0240)						// PDP-11 NOP opcode
			weight += 1;
		return weight;
	}

	unique_ptr<Volume> CreateInstance() override {
		return unique_ptr<Volume>(new CsidosVolume);
	}
} s_cisdosVolumeFactory;

} // U::FS
