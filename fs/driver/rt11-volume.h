// Based on `RT–11 Volume and File Formats Manual` document
// http://bitsavers.informatik.uni-stuttgart.de/pdf/dec/pdp11/rt11/v5.6_Aug91/AA-PD6PA-TC_RT-11_Volume_and_File_Formats_Manual_Aug91.pdf

#pragma once

#include "volume.h"

namespace U::FS {
using namespace std;
using byte = std::byte;


class Rt11Volume : public Volume {
	typedef Volume base;
public:
	enum class DirectoryEntryStatus : uint16_t {
		Tenatative = 0400				// E.TENT
		, Empty = 01000					// E.MPTY
		, Permanant = 02000				// E.PERM
		, EndOfSegment = 04000			// E.EOS
		, Read = 040000					// E.READ
		, Protected = 0100000			// E.PROT
		, Prefix = 020					// E.PRE
	};

	uint16_t NumberOfBlocks = 0;

	static DateTime FromRt11DateFormat(uint16_t v);
	vector<DirEntry> GetDirEntries(uint32_t cluster, bool bWithExtra) override;
	void WriteDirectory();
	void InsertIntoFiles(const DirEntry& entry);
	int64_t FreeSpace() override;
	CFiles::iterator GetEntry(RCString filename) override;
	void AddFile(const String& filename, uint64_t len, Stream& istm, const DateTime& creationTimestamp) override;
	void ModifyFile(const String& filename, uint64_t len, Stream& istm, const DateTime& creationTimestamp) override;
	void CopyFileTo(const DirEntry& fileEntry, Stream& os) override;
	void RemoveFile(RCString filename) override;
	void Defragment() override;
	void MakeDirectory(RCString name) override { Throw(errc::not_supported); }
	pair<vector<wchar_t>, vector<wchar_t>> ValidInvalidFilenameChars() override;
	int MaxNameLength() override { return 10; }

	void Init(const path& filepath) override;
	Rt11Volume();
	~Rt11Volume();
private:

	struct DataBlock {
		byte Bytes[512];

		operator const byte*() const { return Bytes; }
	};

	void ReadBlock(int n, void *data);
	void WriteBlock(int n, const void *);

	DataBlock ReadBlock(int n) {
		DataBlock r;
		ReadBlock(n, r.Bytes);
		return r;
	}

	optional<DirEntry> Allocate(uint16_t nBlock);

	vector<DirEntry> GetDirEntries(bool bWithExtra) {
		return GetDirEntries(0, bWithExtra);
	}

	friend class DirectoryWriter;
};

} // U::FS::
