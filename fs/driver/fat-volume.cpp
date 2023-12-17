// © 2023 Ufasoft https://ufasoft.com, Sergey Pavlov mailto:dev@ufasoft.com
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Based on
//	Microsoft FAT Specification
//	https://academy.cba.mit.edu/classes/networking_communications/SD/FAT.pdf

#include "pch.h"

#include "fat-volume.h"

using namespace std;

namespace U::FS {

const DateTime
	FatDateTime::Min(1980, 1, 1)
	, FatDateTime::Max(2099, 12, 31, 23, 59, 59);

FatDateTime::FatDateTime(const DateTime& dt) {
	tm t = dt;
	Time = (t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec / 2);
	Date = ((t.tm_year + 1900 - 1980) << 9) | ((t.tm_mon + 1) << 5) | t.tm_mday;
}

FatDateTime FatDateTime::Clamp(const DateTime& dt) {
	return FatDateTime(DateTime(clamp(dt.Ticks, Min.Ticks, Max.Ticks)));
}

void FatVolume::LoadFat() {
	Fs.Position = (uint32_t)ReservedSectors * BytesPerSector;
	auto bytesPerFat = SectorsPerFat * BytesPerSector;
	auto bitsPerFatEntry = Kind == FatKind::Fat32 ? 32 : Kind == FatKind::Fat16 ? 16 : 12;
	uint32_t n = uint32_t(bytesPerFat * 8 / bitsPerFatEntry);
	Fat.clear();
	Fat.reserve(n);
	uint8_t prev;
	bool bAlignPos = true;
	for (uint32_t i = 0; i < n; ++i) {
		uint32_t e = 0;
		uint8_t buf[4];
		switch (Kind) {
		case FatKind::Fat12:
			if (bAlignPos) {
				Fs.ReadExactly(buf, 2);
				e = load_little_u16(buf) & 0xFFF;
				prev = buf[1] >> 4;
			} else {
				Fs.ReadExactly(buf, 1);
				e = prev + (int(buf[0]) << 4);
			}
			bAlignPos = !bAlignPos;
			break;
		case FatKind::Fat16:
			Fs.ReadExactly(buf, 2);
			e = load_little_u16(buf);
			break;
		case FatKind::Fat32:
			Fs.ReadExactly(buf, 4);
			e = load_little_u32(buf) & 0xFFFFFFF;
			break;
		}
		Fat.push_back(e);
	}
}

void FatVolume::SaveFats() {
	auto bytesPerFat = SectorsPerFat * BytesPerSector;
	if (bytesPerFat > SIZE_MAX)
		Throw(errc::not_enough_memory);
	vector<uint8_t> buf((size_t)bytesPerFat);
	if (Kind == FatKind::Fat32) {										// Pre-read FAT32 to keep high 4 bits of FAT items
		Fs.Position = (uint32_t)ReservedSectors * BytesPerSector;
		Fs.ReadExactly(buf.data(), buf.size());
	}

	uint8_t *p = buf.data();
	uint8_t t = 0;
	bool bOdd = false;
	for (auto x : Fat) {
		switch (Kind) {
		case FatKind::Fat12:
			if (bOdd) {
				*p++ = t | (uint8_t(x & 0xF) << 4);
				*p++ = uint8_t(x >> 4);
			} else {
				*p++ = (uint8_t)x;
				t = uint8_t(x >> 8);
			}
			bOdd = !bOdd;
			break;
		case FatKind::Fat16:
			store_little_u16(p, (uint16_t)x);
			p += 2;
			break;
		case FatKind::Fat32:
			store_little_u16(p, load_little_u32(p) & 0xF0000000 | x);
			p += 4;
			break;
		}
	}
	if (bOdd)
		*p = t;

	for (int i = 0; i < NumberOfFats; ++i)
		Fs.Write((ReservedSectors + SectorsPerFat * i) * BytesPerSector, buf);
}

DateTime FatVolume::LoadCreationTime(const uint8_t p[32]) {
	auto time = load_little_u16(p + 22);
	int hour = time >> 11
		, minute = (time >> 5) & 0b111111
		, second = (time & 0b11111) << 1;
	if (hour > 23 || minute > 59 || second > 59)
		hour = minute = second = 0;

	auto date = load_little_u16(p + 24);
	return DateTime(
		1980 + (date >> 9)			// year
		, (date >> 5) & 0b1111		// month
		, date & 0b11111			// day
		, hour, minute, second);
}

void FatVolume::ReadDirEntry(DirEntry& e, const uint8_t p[32]) {
	e.CreationTime = LoadCreationTime(p);
	auto a = p[11];
	e.IsArchive = a & FILE_ATTRIBUTE_ARCHIVE;
	e.Hidden = a & FILE_ATTRIBUTE_HIDDEN;
	e.ReadOnly = a & FILE_ATTRIBUTE_READONLY;
	e.IsSystem = a & FILE_ATTRIBUTE_SYSTEM;
	e.IsVolumeLabel = a & 8;
	e.IsDirectory = a & FILE_ATTRIBUTE_DIRECTORY;
	e.Length = (!e.IsDirectory && !e.IsVolumeLabel)? load_little_u32(p + 28) : 0;
	e.AllocationSize = 0 == e.Length
		? 0
		: (e.Length | (uint32_t(BytesPerSector) * SectorsPerCluster - 1)) + 1;
	e.FirstCluster = load_little_u16(p + 26);
	if (Kind == FatKind::Fat32)
		e.FirstCluster += (uint32_t)load_little_u16(p + 20) << 16;
}

static uint8_t ShortFilenameChecksum(const uint8_t shortFilename[11]) {
	uint8_t sum = 0;
	for (int i = 0; i < 11; ++i)
		sum = (sum & 1 ? 0x80 : 0) + (sum >> 1) + shortFilename[i];
	return sum;
}

String FatVolume::DecodeFilename(Span s) {
	auto fn = GetFilenamePart(s.subspan(0, s.size() - 3));
	auto ext = GetFilenamePart(s.subspan(s.size() - 3, 3));
	if (!ext.empty())
		fn += "." + ext;
	return fn;
}

vector<DirEntry> FatVolume::GetDirEntries(uint32_t cluster, bool bWithExtra) {
	vector<uint8_t> dirSegment(cluster ? (uint32_t)SectorsPerCluster * BytesPerSector : CurDirEntries * EntrySize);
	vector<DirEntry> r;
	auto p = dirSegment.data() + dirSegment.size();
	auto curCluster = cluster;
	uint64_t curSegmentOffset = 0;
	String longName = "";
	uint8_t prevOrd;
	uint8_t cksum;
	for (int i = 0; i < CurDirEntries; ++i, p += 32) {
		if (p >= dirSegment.data() + dirSegment.size()) {
			if (curCluster) {
				curSegmentOffset = CalcDataOffset(curCluster);
				curCluster = Fat[curCluster];
			} else
				curSegmentOffset = GetFirstRootDirSector() * BytesPerSector;
			Fs.Position = curSegmentOffset;
			Fs.ReadExactly(dirSegment.data(), dirSegment.size());
			p = dirSegment.data();
		}
		if (!p[0])
			break;
		switch (p[0]) {
		case 0xE5:			// Deleted file
			if (bWithExtra) {
				DirEntry entryEmpty;
				entryEmpty.Empty = true;
				entryEmpty.OriginalFilenamePresentation = Blob(p, 11);
				entryEmpty.FileName = "~" + DecodeFilename(Span(entryEmpty.OriginalFilenamePresentation).subspan(1));
				ReadDirEntry(entryEmpty, p);
				r.push_back(entryEmpty);
			}
			break;
		case 0x05:
			p[0] = 0xE5;	// Trick for Kanji
		default:
			uint8_t attrs = p[11];
			if ((attrs & 0xF) == ATTR_LONG_NAME
					&& ((p[0] & LAST_LONG_ENTRY) || !longName.empty())) {
				uint8_t ord = p[0] & 0xF;
				uint16_t unicodeBuf[LongNameCharsPerEntry];
				memcpy(unicodeBuf, p + 1, 10);
				memcpy(unicodeBuf + 5, p + 14, 12);
				memcpy(unicodeBuf + 11, p + 28, 4);
				String namePart;
				if (unicodeBuf[LongNameCharsPerEntry - 1] != 0xFFFF && unicodeBuf[LongNameCharsPerEntry - 1] != 0)
					namePart = String(unicodeBuf, size(unicodeBuf));
				else if (find(unicodeBuf, unicodeBuf + size(unicodeBuf), 0) != unicodeBuf + size(unicodeBuf))
					namePart = String(unicodeBuf);
				else {								// Corrupted, no NULL termination for Padded string
					longName = String();
					prevOrd = 0;
					break;
				}
				if (p[0] & LAST_LONG_ENTRY) {
					longName = namePart;
					cksum = p[13];
				}
				else if (ord == prevOrd - 1)
					longName = namePart + longName;
				else {								// Corrupted Long name entries
					longName = String();
					prevOrd = 0;
					break;
				}
				prevOrd = ord;
			} else {
				DirEntry entry;
				entry.Attrs = attrs;
				entry.OriginalFilenamePresentation = Blob(p, 11);
				auto fn = DecodeFilename(entry.OriginalFilenamePresentation);
				if (longName.empty())
					entry.FileName = fn;
				else {
					if (ShortFilenameChecksum(p) == cksum) {
						entry.FileName = exchange(longName, "").Trim();
						entry.AlternateFileName = fn;
					} else {
						TRC(1, "Directory corrupted: Long Filename checksum does not match");
						entry.FileName = fn;
					}
				}
				if (!bWithExtra && (entry.FileName == "." || entry.FileName == ".."))
					break;
				entry.DirEntryDiskOffset = p - dirSegment.data() + curSegmentOffset;
				ReadDirEntry(entry, p);
				r.push_back(entry);
			}
		}
	}
	return r;
}

vector<uint32_t> FatVolume::GetClusters(uint32_t cluster) {
	vector<uint32_t> r;
	if (cluster)
		for (; cluster < MinFinalCluster; cluster = Fat[MinFinalCluster])
			r.push_back(cluster);
	return r;
}

void FatVolume::CreateChain(const vector<uint32_t>& clusters) {
	for (uint32_t i = 0; i < clusters.size() - 1; ++i)
		Fat[clusters[i]] = clusters[i + 1];
	if (!clusters.empty())
		Fat[clusters.back()] = MinFinalCluster;
}

uint32_t FatVolume::SaveStreamContents(Stream& istm, uint32_t cluster) {
	auto bytesPerCluster = uint32_t(SectorsPerCluster) * BytesPerSector;
	vector<uint32_t> clusters;
	uint64_t len = istm.Length - istm.Position;
	auto needClusters = (len + bytesPerCluster - 1) / bytesPerCluster;
	if (cluster) {
		clusters = GetClusters(cluster);
		while (clusters.size() > needClusters) {
			Fat[clusters.back()] = 0;
			clusters.pop_back();
			if (!clusters.empty())
				Fat[clusters.back()] = MinFinalCluster;
		}
	} else
		clusters = Allocate(len);

	vector<uint8_t> buf(bytesPerCluster);
	for (auto c : clusters) {
		memset(buf.data(), 0, buf.size());
		auto sz = istm.Read(buf.data(), buf.size());
		Fs.Write(CalcDataOffset(c), Span(buf.data(), buf.size()));
	}

	return clusters.empty() ? 0 : clusters.front();
}

void FatVolume::SaveDirStreamToVolume(Stream& stm) {
	if (CurDirCluster)
		SaveStreamContents(stm, CurDirCluster);
	else if (stm.Length > RootDirectoryEntries * EntrySize)
		Throw(HRESULT_FROM_WIN32(ERROR_CANNOT_MAKE));
	else {
		Fs.Position = GetFirstRootDirSector() * BytesPerSector;
		stm.CopyTo(Fs);
	}
}

void FatVolume::Serialize(Stream& stm, const DirEntry& e) {
	uint8_t buf[EntrySize];

	String shortName = !e.AlternateFileName ? e.FileName : e.AlternateFileName;
	strcpy((char*)buf, "           ");
	memset(buf + 11, 0, EntrySize - 11);
	if (DecodeFilename(e.OriginalFilenamePresentation) == shortName)			// To keep filenames with multiple dots unmodified
		memcpy(buf, e.OriginalFilenamePresentation.constData(), e.OriginalFilenamePresentation.size());
	else {
		auto split = Path::SplitPath(shortName.c_wstr());
		if (split.m_ext.StartsWith("."))
			split.m_ext = split.m_ext.substr(1);
		Blob fname = EncodeFilenamePart(split.m_fname)
			, ext = EncodeFilenamePart(split.m_ext);
		strncpy((char*)buf, (const char*)fname.constData(), 8);
		strncpy((char*)buf + 8, (const char*)ext.constData(), 3);
	}

	if (!!e.AlternateFileName) {					// Write Long filename
		auto len = e.FileName.length();
		int n = (255 + LongNameCharsPerEntry - 1) / LongNameCharsPerEntry;
		uint16_t *utf16 = (uint16_t*)alloca(n * LongNameCharsPerEntry * sizeof(uint16_t));
		memset(utf16, 0xFF, sizeof(utf16));			// Padding with 0xFFFF
		auto *filename = e.FileName.c_wstr();
		for (int i = 0; i < len + 1; ++i)
			utf16[i] = (uint16_t)filename[i];		// with terminating NUL

		uint8_t bufLongname[EntrySize];
		bufLongname[12] = bufLongname[26] = bufLongname[27] = 0;
		bufLongname[13] = ShortFilenameChecksum(buf);
		for (int i = n; i; --i) {
			bufLongname[0] = i | (i == n ? LAST_LONG_ENTRY : 0);
			auto portion = &utf16[LongNameCharsPerEntry * (i - 1)];
			for (int j = 0; j < 5; ++j)
				store_little_u16(bufLongname + 1 + j * 2, *portion++);
			bufLongname[11] = e.IsDirectory ? ATTR_LONG_NAME | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_ARCHIVE : ATTR_LONG_NAME;
			for (int j = 0; j < 6; ++j)
				store_little_u16(bufLongname + 14 + j * 2, *portion++);
			store_little_u16(bufLongname + 28, *portion++);
			store_little_u16(bufLongname + 30, *portion++);
			stm.WriteBuffer(bufLongname, sizeof(bufLongname));
		}
	}

	if (e.Empty)
		buf[0] = 0xE5;
	else if (buf[0] == 0xE5)
		buf[0] = 0x05;			// Kanji
	buf[11] = (uint8_t)e.Attrs;
	buf[11] |= e.IsDirectory ? FILE_ATTRIBUTE_DIRECTORY : 0;
	if (e.CreationTime.Ticks) {
		FatDateTime dt(e.CreationTime);
		store_little_u16(buf + 22, dt.Time);
		store_little_u16(buf + 24, dt.Date);
	}
	store_little_u32(buf + 28, (uint32_t)e.Length);
	store_little_u16(buf + 26, (uint16_t)e.FirstCluster);
	store_little_u16(buf + 20, (uint16_t)(e.FirstCluster >> 16));
	stm.WriteBuffer(buf, sizeof(buf));
}

void FatVolume::LoadCurDir() {
	Files = GetDirEntries(CurDirCluster, false);
	for (auto it = Files.begin(); it != Files.end();) {
		it = it->IsVolumeLabel
			? Files.erase(it)
			: ++it;
	}
}

DirEntry FatVolume::AllocateDirectory() {
	DirEntry r;
	r.CreationTime = Clock::now();
	r.IsDirectory = true;
	return r;
}

void FatVolume::MakeDirectory(RCString name) {
	EnsureWriteMode();
	if (FindEntry(name) != Files.end())
		Throw(errc::file_exists);

	DirEntry entryNew = AllocateDirectory();
	entryNew.FileName = name;
	auto entries = GetDirEntries(0, true);
	for (auto& e : entries)
		if (e.Empty) {
			e = entryNew;
			goto LAB_ADDED;
		}
	entries.push_back(entryNew);
LAB_ADDED:
	SaveDirEntries(entries);
	LoadCurDir();
}

int64_t FatVolume::FreeSpace() {
	int64_t r = 0;
	for (auto e : Fat)
		if (!e)
			r += (uint32_t)SectorsPerCluster * BytesPerSector;
	return r;
}

void FatVolume::RemoveFile(RCString filename) {
	RemoveFileChecks(filename);
	auto& e = *GetEntry(filename);

	for (uint32_t cluster = (uint32_t)e.FirstCluster; cluster < MinFinalCluster;)
		cluster = exchange(Fat.at(cluster), (uint32_t)0);

	uint8_t deleteMark[1] = { 0xE5 }
		, zero[2] = { 0, 0 };
	Fs.Write(e.DirEntryDiskOffset, deleteMark);		// Mark as deleted
	Fs.Write(e.DirEntryDiskOffset + 20, zero);		// .FirstCluster = 0
	Fs.Write(e.DirEntryDiskOffset + 26, zero);

	LoadCurDir();
	SaveFats();
}

void FatVolume::Init(const path& filepath) {
	base::Init(filepath);

	array<uint8_t, 512> buf;
	auto data = buf.data();
	Fs.ReadExactly(buf.data(), buf.size());
	auto bpb = buf.data() + 11;
	BytesPerSector = load_little_u16(data + 11);
	SectorsPerCluster = data[13];
	ReservedSectors = load_little_u16(data + 14);
	NumberOfFats = bpb[5];
	RootDirectoryEntries = load_little_u16(bpb + 6);
	TotalSectors = load_little_u16(data + 19) ? load_little_u16(data + 19) : load_little_u32(data + 32);
	MediaDescriptor = bpb[0xA];
	SectorsPerFat = load_little_u16(data + 22) ? load_little_u16(data + 22) : load_little_u32(data + 36);
	PhysicalSectorsPerTrack = load_little_u16(bpb + 0xD);
	NumberOfHeads = load_little_u16(bpb + 0xF);
	HiddenSectors = load_little_u32(bpb + 0x11);

	auto rootSectors = (RootDirectoryEntries * 32 + BytesPerSector - 1) / BytesPerSector;
	uint64_t dataSectors = TotalSectors - ReservedSectors - NumberOfFats * SectorsPerFat - rootSectors;
	auto clusters = dataSectors / SectorsPerCluster;
	Kind = clusters < 4085 ? FatKind::Fat12
		: clusters < 65525 ? FatKind::Fat16
		: Kind = FatKind::Fat32;

	switch (Kind) {
	case FatKind::Fat12:
	case FatKind::Fat16:
		PhysicalDriveNumber = bpb[0x19];
		BpbFlags = bpb[0x1A];
		RootCluster = 0;
		break;
	case FatKind::Fat32:
		ExtFlags = load_little_u16(data + 40);
		RootCluster = load_little_u32(data + 44);
		break;
	}

	MinFinalCluster = Kind == FatKind::Fat12 ? 0xFF8
		: Kind == FatKind::Fat16 ? 0xFFF8
		: 0xFFFFFF8;
	FinalCluster = MinFinalCluster | 0xF;

	CurDirCluster = RootCluster;
	CurDirEntries = RootDirectoryEntries;

	LoadCurDir();
	LoadFat();
}

void FatVolume::CopyFileTo(const DirEntry& fileEntry, Stream& os) {
	if (uint32_t c = (uint32_t)fileEntry.FirstCluster) {
		vector<byte> buf(uint32_t(SectorsPerCluster) * BytesPerSector);
		for (int64_t len = (int64_t)fileEntry.Length; len > 0; c = Fat[c], len -= buf.size()) {
			if (c >= MinFinalCluster)
				Throw(HRESULT_FROM_WIN32(ERROR_DISK_CORRUPT));
			Fs.Position = CalcDataOffset(c);
			Fs.ReadExactly(buf.data(), buf.size());
			os.WriteBuffer(buf.data(), (size_t)std::min((int64_t)buf.size(), len));
		}
		if (c < MinFinalCluster)
			Throw(HRESULT_FROM_WIN32(ERROR_DISK_CORRUPT));
	}
}

void FatVolume::ChangeDirectory(RCString name) {
	if (name == "/") {
		CurDirCluster = RootCluster;
		CurDirName = "/";
	}
	else if (name != "..") {
		CurDirCluster = (uint32_t)GetEntry(name)->FirstCluster;
		CurDirName = name;
	}
	else if (CurDirCluster != RootCluster) {
		for (auto& e : GetDirEntries(CurDirCluster, true))
			if (e.FileName == "..") {
				CurDirCluster = (uint32_t)e.FirstCluster;
				CurDirName = CurDirCluster == RootCluster ? nullptr : "..";
				goto LAB_FOUND;
			}
		CurDirCluster = RootCluster;
	}
LAB_FOUND:
	if (!CurDirCluster)
		CurDirEntries = RootDirectoryEntries;
	else {
		CurDirEntries = 0;
		for (auto c = CurDirCluster; c < MinFinalCluster; c = Fat.at(c))
			CurDirEntries += uint32_t(SectorsPerCluster) * BytesPerSector / 32;
	}
	LoadCurDir();
}

vector<uint32_t> FatVolume::Allocate(int64_t size) {
	vector<uint32_t> r;
	if (size <= 0)
		return r;
	uint32_t bytesInCluster = (uint32_t)SectorsPerCluster * BytesPerSector;
	for (uint32_t i = 0; Fat.size(); ++i)
		if (!Fat[i]) {
			r.push_back(i);
			if ((size -= bytesInCluster) <= 0)
				return r;
		}
	Throw(errc::no_space_on_device);
}

void FatVolume::ModifyFile(const String& filename, uint64_t len, Stream& istm, const DateTime& creationTimestamp) {
	EnsureWriteMode();
	auto it = FindEntry(filename);
	if (it != Files.end()) {
		if (it->IsDirectory)
			Throw(errc::is_a_directory);
		uint8_t buf[4];
		len = AdjustLengthOnPut(istm, *it, len);
		store_little_u32(buf, SaveStreamContents(istm, (uint32_t)it->FirstCluster));
		Fs.Write(it->DirEntryDiskOffset + 26, Span(buf, 2));
		if (Kind == FatKind::Fat32)
			Fs.Write(it->DirEntryDiskOffset + 20, Span(buf + 2, 2));
	} else {
		DirEntry e = AllocateFileEntry();
		len = AdjustLengthOnPut(istm, e, len);
		e.FileName = filename.ToUpper();
		e.Length = len;
		e.FirstCluster = SaveStreamContents(istm, 0);
		auto entries = GetDirEntries(0, true);
		AddToDirEntries(entries, e);
		SaveDirEntries(entries);
	}
	SaveFats();
	LoadCurDir();
}

int FatVolumeFactory::IsSupportedVolume(RCSpan s) {
	auto data = s.data();
	if (s.size() < 512)
		return 0;
	int weight = 0;
	weight += (data[0] == 0xEB && data[2] == 0x90) || data[0] == 0xE9 ? 1 : 0;		// 8086 JMP instruction

	auto secSize = load_little_u16(data + 11);		// BytesPerSector
	if (!(secSize == 512 || secSize == 1024 || secSize == 2048 || secSize == 4096))
		return 0;

	weight += data[0x15] >= 0xF0 ? 1 : 0;						// Media ID
	weight += (load_little_u16(data + 510) == 0xAA55) ? 2 : 0;
	weight += !memcmp(data + 0x36, "FAT12   ", 8)
		|| !memcmp(data + 0x36, "FAT16   ", 8)
		|| !memcmp(data + 0x36, "FAT32   ", 8)
		? 3 : 0;

	return weight >= 3 ? weight : 0;
}

static FatVolumeFactory s_fatVolumeFactory;

unique_ptr<Volume> FatVolumeFactory::CreateInstance() {
	return unique_ptr<Volume>(new FatVolume);
}

} // U::FS
