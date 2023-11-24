#pragma once

#include "volume.h"

namespace U::FS {

enum class FatKind {
	Fat12
	, Fat16
	, Fat32
};

struct FatDateTime {
	static const DateTime Min, Max;

	uint16_t Time, Date;

	FatDateTime(const DateTime& dt);
	static FatDateTime Clamp(const DateTime& dt);
};

class FatVolume : public Volume {
	typedef Volume base;
public:
	static const size_t EntrySize = 32;
	static const int LongNameCharsPerEntry = 13;

	static const uint8_t ATTR_LONG_NAME = 0xF
		, LAST_LONG_ENTRY = 0x40;
private:
	FatKind Kind = FatKind::Fat12;

	// Wide to avoid extension before arithmetics
	uint64_t SectorsPerFat;
	uint32_t RootDirectoryEntries;

	// BPB
	uint32_t HiddenSectors
		, RootCluster;
	uint16_t ReservedSectors
		, PhysicalSectorsPerTrack
		, NumberOfHeads
		, ExtFlags = 0;
	uint8_t NumberOfFats
		, MediaDescriptor
		, PhysicalDriveNumber
		, BpbFlags;

	int CurDirCluster, CurDirEntries;

	uint32_t FinalCluster, MinFinalCluster;
	vector<uint32_t> Fat;

	void Init(const path& filepath) override;
	int64_t FreeSpace() override;
	void MakeDirectory(RCString name) override;
	CFiles::iterator FatVolume::FindFile(const String& filename);
	void RemoveFile(RCString filename) override;
	int MaxNameLength() override { return 255; }
	void LoadFat();
	void SaveFats();
	void ChangeDirectory(RCString name) override;

	// returns list of clusters
	vector<uint32_t> Allocate(int64_t size);

	uint64_t CalcDataSector(uint32_t cluster) {
		auto rootDirSectors = (RootDirectoryEntries * EntrySize + BytesPerSector - 1) / BytesPerSector;
		return ReservedSectors + NumberOfFats * SectorsPerFat + rootDirSectors + uint64_t(cluster - 2) * SectorsPerCluster;
	}

	uint64_t CalcDataOffset(uint32_t cluster) {
		return CalcDataSector(cluster) * BytesPerSector;
	}

	uint64_t GetFirstRootDirSector() {
		return ReservedSectors + NumberOfFats * SectorsPerFat;
	}

	void ModifyFile(const String& filename, uint64_t len, Stream& istm, const DateTime& creationTimestamp) override;
protected:
	virtual DirEntry AllocateDirectory();
	virtual DateTime LoadCreationTime(const uint8_t p[32]);
	virtual void ReadDirEntry(DirEntry& e, const uint8_t p[32]);
	vector<DirEntry> GetDirEntries(uint32_t cluster, bool bWithExtra) override;
	void Serialize(Stream& stm, const DirEntry& entry) override;
	void CopyFileTo(const DirEntry& fileEntry, Stream& os) override;

	uint64_t FindFreeContiguousArea(uint64_t nClusters) { Throw(E_NOTIMPL); }
	void LoadCurDir() override;
	vector<uint32_t> GetClusters(uint32_t cluster);
	void CreateChain(const vector<uint32_t>& clusters);

	// If firstCluster == 0: allocate from scratch
	// Returns: first cluster of contents
	virtual uint32_t SaveStreamContents(Stream& istm, uint32_t firstCluster);

	void SaveDirStreamToVolume(Stream& stm) override;

	String DecodeFilename(Span s);
};

class FatVolumeFactory : public IVolumeFactory {
public:
	int IsSupportedVolume(RCSpan s) override;
	unique_ptr<Volume> CreateInstance() override;
};

} // U::FS::
