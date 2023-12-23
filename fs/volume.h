// Copyright(c) 2023 Ufasoft  http://ufasoft.com  mailto:support@ufasoft.com,  Sergey Pavlov  mailto:dev@ufasoft.com
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace U::FS {
using namespace std;
using byte = std::byte;

extern const CodePageEncoding s_encodingKoi8;

String DecodeRadix50(uint16_t w);

class DirEntry : public Object, CPersistent {
public:
	typedef NonInterlockedPolicy interlocked_policy;

	String FileName;
	String AlternateFileName = nullptr;
	Blob OriginalFilenamePresentation;
	int64_t Length = 0, AllocationSize = 0;				// In bytes
	DateTime CreationTime, LastWriteTime, LastAccessTime;
	Blob ExtraData;

	uint64_t DirEntryDiskOffset = 0xFFFFFFFF;
	uint64_t FirstCluster = 0;
	uint32_t Aux1 = 0, Aux2 = 0, Aux3 = 0;
	int EntrySize = 0;
	uint16_t Attrs;
	bool Empty = false
		, IsDirectory = false
		, IsArchive = false
		, IsSystem = false
		, IsVolumeLabel = false
		, Hidden = false
		, ReadOnly = false;

	void Read(const BinaryReader& rd) override;
	void Write(BinaryWriter& wr) const override;

	static DirEntry FromSpan(RCSpan s) {
		CMemReadStream stm(s);
		BinaryReader rd(stm);
		DirEntry r;
		rd >> r;
		return r;
	}
};

interface IVolumeCallback {
	bool Interactive = false;

	virtual bool AskYesOrNo(RCString msg) = 0;
};

class Volume
{
public:
	typedef vector<DirEntry> CFiles;

	IVolumeCallback *Callback = nullptr;

	CFiles Files;
	String Filename;

	String CurDirName;
	vector<String> CurPath;
	vector<int> CurFidPath;

	virtual ~Volume();
	virtual void Init(const path& filepath);
	virtual int64_t FreeSpace() { Throw(E_NOTIMPL); }
	virtual vector<DirEntry> GetFiles() { return GetDirEntries(0, false); }
	virtual CFiles::iterator GetEntry(RCString filename);
	virtual int MaxNameLength() { Throw(E_NOTIMPL); }
	virtual pair<vector<wchar_t>, vector<wchar_t>> ValidInvalidFilenameChars();
	virtual void AddFile(const String& filename, uint64_t len, Stream& istm, const DateTime& creationTimestamp);
	virtual void ChangeDirectory(RCString name);
	virtual void CopyFileTo(const DirEntry& fileEntry, Stream& os) = 0;
	virtual void RemoveFile(RCString filename) { Throw(E_NOTIMPL); }
	virtual void ModifyFile(RCString filename, uint64_t len, Stream& istm, const DateTime& creationTimestamp);
	virtual void MakeDirectory(RCString name) { Throw(E_NOTIMPL); }
	virtual void Flush();
protected:
	FileStream Fs;
	path filepath_;
	const Encoding* Encoding;

	uint64_t TotalSectors = 0;
	uint64_t FirstDataCluster = numeric_limits<uint64_t>::max();	// or Sector
	uint16_t BytesPerSector = 512;
	uint16_t SectorsPerCluster = 1;

	bool CaseSensitive = false;
	bool _openedForModifying = false;

	Volume();
	String GetFilenamePart(const Span& s);
	Blob EncodeFilenamePart(RCString s);
	CFiles::iterator Volume::FindEntry(const String& filename);
	void EnsureWriteMode();

	// cluster == 0 means Root Directory
	virtual vector<DirEntry> GetDirEntries(uint32_t cluster, bool bWithExtra) = 0;

	virtual void LoadCurDir() { Files = GetFiles(); }

	uint64_t CalcNumberOfClusters(uint64_t len);

	// returns First Cluster/Sector or 0 if there is no space
	virtual uint64_t FindFreeContiguousArea(uint64_t nClusters);

	virtual void Defragment() { Throw(E_NOTIMPL); }
	virtual DirEntry AllocateFileEntry();
	virtual uint64_t AdjustLengthOnPut(Stream& istm, DirEntry& entry, uint64_t len) { return len; }
	virtual void AddToDirEntries(CFiles& entries, const DirEntry& entry);
	virtual void Serialize(Stream& stm, const DirEntry& entry) { Throw(E_NOTIMPL); }
	virtual void SaveDirStreamToVolume(Stream& stm) { Throw(E_NOTIMPL); }
	virtual void WriteEndOfDirectory(Stream& stm);
	virtual void SaveDirEntries(const CFiles& entries);
	virtual void RemoveFileChecks(RCString filename);
};

interface IVolumeFactory {
	static vector<IVolumeFactory*>& RegisteredFactories();

	static IVolumeFactory* FindBestFactory(const Span& s);
	static unique_ptr<Volume> Mount(const path& p);

	// returns weight
	virtual int IsSupportedVolume(RCSpan s) = 0;
	virtual unique_ptr<Volume> CreateInstance() = 0;
protected:
	IVolumeFactory();
};


} // U::FS::

namespace U::VolumeShell {

class __declspec(uuid("{9A7A94F5-FBF1-49A8-B0D9-44667635FE97}")) IShellSearchTargetStub;

class __declspec(uuid("{55534654-3F35-46d9-0044-54BA20230001}")) FsShellExt;
extern const Guid CLSID_Rt11Fs;

//ShellPath CreateItem(const String& name);
} // U::VolumeShell::
