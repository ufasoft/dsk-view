// Copyright(c) 2023 Ufasoft  http://ufasoft.com  mailto:support@ufasoft.com,  Sergey Pavlov  mailto:dev@ufasoft.com
//
// SPDX-License-Identifier: GPL-2.0-or-later
//
//	Filesystem of ANDOS for soviet computer BK-0010/0011
//	Based on
//		А.М.Надежин - Дисковая операционная система для БК0010, БК0011, БК0011М - 1997
//		https://forum.maxiol.com/index.php?showtopic=5558

#include "pch.h"
#include "fat-volume.h"
#include "bk-volume.h"

using namespace std;

namespace U::FS {

static const DateTime s_defaultDateTime(1992, 4, 1);		// ANDOS Date of Birth

class AndosVolume : public BkVolume<FatVolume> {
	typedef BkVolume<FatVolume> base;
public:
	int MaxNameLength() override { return 12; }
protected:
	void ReadDirEntry(DirEntry& entry, const uint8_t p[32]) override {
		base::ReadDirEntry(entry, p);
		entry.IsVolumeLabel = false;
		entry.CreationTime = entry.CreationTime.Date;
		entry.IsDirectory = p[20];
		entry.Aux1 = p[20];
		entry.Aux2 = p[21];
		entry.Aux3 = load_little_u16(p + 22);			// Loading address
	}

	static const uint8_t c_volumeAttr = 8;

	void Serialize(Stream& stm, const DirEntry& e) override {
		base::Serialize(stm, e);
		uint64_t pos = stm.Position;
		uint8_t buf[4] = { (uint8_t)e.Aux1, (uint8_t)e.Aux2 };
		store_little_u16(buf + 2, (uint16_t)e.Aux3);
		stm.Write(pos - 32 + 20, buf);
		if (e.IsDirectory)
			stm.Write(pos - 32 + 11, Span(&c_volumeAttr, 1));
		stm.Position = pos;
	}

	DateTime LoadCreationTime(const uint8_t p[32]) override {
		auto date = load_little_u16(p + 24);
		return !date
			? DateTime()
			: DateTime(
				1980 + (date >> 9)			// year
				, (date >> 5) & 0b1111		// month
				, date & 0b11111);			// day
	}

	vector<DirEntry> GetDirEntries(uint32_t dirId, bool bWithExtra) override {
		auto files = base::GetDirEntries(0, bWithExtra);
		for (auto it = files.begin(); it != files.end();) {
			it = !bWithExtra && it->Aux2 != dirId
				? files.erase(it)
				: ++it;
		}
		return files;
	}

	DirEntry AllocateFileEntry() override {
		auto r = base::AllocateDirectory();
		r.Aux2 = CurDirId;
		return r;
	}

	DirEntry AllocateDirectory() override {
		set<uint8_t> dirIds;
		for (int i = 1; i <= 255; ++i)
			dirIds.insert((uint8_t)i);
		for (auto& e : Files)
			dirIds.erase((uint8_t)e.Aux1);
		if (dirIds.empty())
			Throw(errc::no_space_on_device);
		auto r = base::AllocateDirectory();
		r.Aux1 = *dirIds.begin();				// First free Dir ID
		r.Aux2 = CurDirId;
		return r;
	}

	void CopyFileTo(const DirEntry& fileEntry, Stream& os) override {
		OptionalCopyHeader(fileEntry, os);
		FatVolume::CopyFileTo(fileEntry, os);
	}
};

static class AndosVolumeFactory : public FatVolumeFactory {
	typedef FatVolumeFactory base;

	int IsSupportedVolume(RCSpan s) override {
		if (auto weight = base::IsSupportedVolume(s)) {
			return !memcmp(s.data() + 4, "ANDOS  ", 7)			// OS for Soviet computer BK-0010)
				? weight + 2
				: 0;
		}
		return 0;
	}

	unique_ptr<Volume> CreateInstance() override {
		return unique_ptr<Volume>(new AndosVolume);
	}
} s_andosVolumeFactory;


} // U::FS
