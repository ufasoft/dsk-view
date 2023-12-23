// © 2023 Ufasoft https://ufasoft.com, Sergey Pavlov mailto:dev@ufasoft.com
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "volume.h"

namespace U::FS {

// DirectoryEntry.Aux1: FileNum

class Files11ods1Volume : public Volume {
	typedef Volume base;

	int MaxNameLength() override { return 13; }
	void Init(const path& filepath) override;
	String DecodeFileNameVer(const uint8_t d[10]);
	vector<DirEntry> GetDirEntries(uint32_t fileNum, bool bWithExtra) override;
	void ChangeDirectory(RCString name) override;
	int64_t FreeSpace() override;
protected:
	static const int
		FileNumIndex = 1
		, FileNumStorageBitmap = 2
		, FileNumBadBlocks = 3
		, FileNumMFD = 4;

	static inline const unordered_set<int> c_systemFileNums = {
		FileNumIndex, FileNumStorageBitmap, FileNumBadBlocks, FileNumMFD
	};

	unordered_map<uint16_t, DirEntry> AllDirEntries;
	int CurDirFileId = FileNumMFD;
	uint32_t MaxNumberOfFiles = 0;
	uint32_t BitmapLba = 0;
	uint16_t SectorsInBitmap = 0;

	virtual bool ReadHeaderSector(uint32_t sector, uint8_t data[512]);
	virtual void LoadHomeBlock(const uint8_t home[512]);
	virtual void LoadAllDirEntries();
	virtual vector<uint32_t> GetFileSectors(const DirEntry& e);
	virtual void LoadFileHeader(int sector, const uint8_t data[512]);
	const DirEntry& GetEntryByFileId(uint32_t fileId);
	void CopyFileTo(const DirEntry& fileEntry, Stream& os) override;
};

} // U::FS::
