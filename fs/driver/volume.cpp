// © 2023 Ufasoft https://ufasoft.com, Sergey Pavlov mailto:dev@ufasoft.com
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pch.h"

#include "volume.h"

using namespace std;
using namespace std::filesystem;

namespace U::FS {

const CodePageEncoding s_encodingKoi8(20866);


static const CodePageEncoding s_encodingOem(CP_OEMCP);

Volume::Volume() : Encoding(&s_encodingOem) {
}

Volume::~Volume() {
}

pair<vector<wchar_t>, vector<wchar_t>> Volume::ValidInvalidFilenameChars() {
	vector<wchar_t> invalid;
	invalid.push_back('/');
	invalid.push_back('\\');
	invalid.push_back(':');
	return make_pair(vector<wchar_t>(), invalid);
}

void Volume::Init(const path& filepath) {
	TRC(1, "Opening file " << filepath << "  this: " << this);

	filepath_ = filepath;
	Fs.Open(filepath_, FileMode::Open, FileAccess::Read, FileShare::Read);
	Filename = filepath_.filename().native();
}

String Volume::GetFilenamePart(const Span& s) {
	return Encoding->GetString(s).Trim();
}

Blob Volume::EncodeFilenamePart(RCString s) {
	return Encoding->GetBytes(s);
}

Volume::CFiles::iterator Volume::FindEntry(const String& filename) {
	auto it = Files.begin();
	for (; it != Files.end(); ++it) {
		if (CaseSensitive) {
			if (!it->FileName.compare(filename))
				return it;
		} else if (!it->FileName.CompareNoCase(filename))
			return it;
	}
	return it;
}

Volume::CFiles::iterator Volume::GetEntry(const String& filename) {
	auto it = FindEntry(filename);
	if (it == Files.end())
		Throw(errc::no_such_file_or_directory);
	return it;
}

void Volume::ChangeDirectory(RCString name) {
	if (name != "/")
		Throw(E_NOTIMPL);
}

void Volume::EnsureWriteMode() {
	if (!_openedForModifying) {
		Fs.Close();
		try {
			Fs.Open(filepath_, FileMode::Open, FileAccess::ReadWrite);
		} catch (exception&) {
			Fs.Open(filepath_, FileMode::Open, FileAccess::Read); 	// reopen in Read-only mode
		}
		_openedForModifying = true;
	}
}

uint64_t Volume::CalcNumberOfClusters(uint64_t len) {
	if (0 == len)
		return len;
	auto clusterSize = (uint32_t)BytesPerSector * SectorsPerCluster;
	return (len + clusterSize - 1) / clusterSize;
}

uint64_t Volume::FindFreeContiguousArea(uint64_t nClusters) {
	map<uint64_t, uint64_t> map;		// start/len pairs
	for (const auto& e : GetDirEntries(0, true))
		if (!e.Empty && !e.IsDirectory && e.FirstCluster)
			map.insert(make_pair(e.FirstCluster, CalcNumberOfClusters(e.Length)));
	auto cur = FirstDataCluster;
	for (const auto kv : map) {
		if (kv.first < cur)
			Throw(HRESULT_FROM_WIN32(ERROR_DISK_CORRUPT));
		if ((kv.first - cur) >= nClusters)
			return cur;
		cur = kv.first + kv.second;
	}
	if (TotalSectors - cur >= nClusters)
		return cur;
	return 0;
}

DirEntry Volume::AllocateFileEntry() {
	DirEntry r;
	r.CreationTime = Clock::now();
	return r;
}

void Volume::AddToDirEntries(CFiles& entries, const DirEntry& entry) {
	for (auto& e : entries)
		if (e.Empty) {
			e = entry;
			return;
		}
	entries.push_back(entry);
}

static const uint8_t c_endOfDir = 0;

void Volume::WriteEndOfDirectory(Stream& stm) {
	stm.WriteBuffer(&c_endOfDir, 1);
}

void Volume::SaveDirEntries(const CFiles& entries) {
	MemoryStream ms;
	for (const auto& e : entries)
		Serialize(ms, e);
	WriteEndOfDirectory(ms);
	ms.Position = 0;
	SaveDirStreamToVolume(ms);
}

void Volume::ModifyFile(RCString filename, uint64_t len, Stream& istm, const DateTime& creationTimestamp) {
	EnsureWriteMode();
	DirEntry e;
	len = AdjustLengthOnPut(istm, e, len);
	auto nClusters = CalcNumberOfClusters(len);
	uint64_t firstCluster = 0;
	if (nClusters) {
		firstCluster = FindFreeContiguousArea(nClusters);
		if (!firstCluster) {
			try {
				Defragment();
			} catch (Exception& ex) {
				if (ex.code() == error_code(E_NOTIMPL, hresult_category()))
					Throw(errc::no_space_on_device);
			}
			if (!(firstCluster = FindFreeContiguousArea(nClusters)))
				Throw(errc::no_space_on_device);
		}
		Fs.Position = (uint64_t)firstCluster * SectorsPerCluster * BytesPerSector;
		istm.CopyTo(Fs);
	}
	e.FileName = filename;
	e.FirstCluster = firstCluster;
	e.Length = len;
	auto entries = GetDirEntries(0, true);
	AddToDirEntries(entries, e);
	SaveDirEntries(entries);
	LoadCurDir();
}

void Volume::AddFile(const String& filename, uint64_t len, Stream& istm, const DateTime& creationTimestamp) {
	if (FindEntry(filename) != Files.end())
		Throw(errc::file_exists);
	ModifyFile(filename, len, istm, creationTimestamp);
}

void Volume::RemoveFileChecks(RCString filename) {
	EnsureWriteMode();
	auto& e = *GetEntry(filename);
	if (e.IsDirectory && !GetDirEntries((uint32_t)e.FirstCluster, false).empty())
		Throw(errc::directory_not_empty);
}

void Volume::Flush() {
	if (_openedForModifying)
		Fs.Flush();
}

IVolumeFactory* IVolumeFactory::FindBestFactory(const Span& s) {
	IVolumeFactory* bestFactory = nullptr;
	int bestWeight = 0;
	for (auto factory : IVolumeFactory::RegisteredFactories()) {
		auto weight = factory->IsSupportedVolume(s);
		if (weight > bestWeight) {
			bestWeight = weight;
			bestFactory = factory;
		}
	}
	return bestFactory;
}

unique_ptr<Volume> IVolumeFactory::Mount(const path& p) {
	vector<uint8_t> buf(128 * 1024);
	auto cb = FileStream(p, FileMode::Open, FileAccess::Read).Read(buf.data(), buf.size());
	if (auto factory = FindBestFactory(Span(buf.data(), cb))) {
		auto volume = factory->CreateInstance();
		volume->Init(p);
		return volume;
	}
	Throw(HRESULT_FROM_WIN32(ERROR_UNRECOGNIZED_VOLUME));
}

} // U::FS
