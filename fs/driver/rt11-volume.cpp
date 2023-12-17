// © 2023 Ufasoft https://ufasoft.com, Sergey Pavlov mailto:dev@ufasoft.com
// SPDX-License-Identifier: GPL-3.0-or-later
//
// DEC RT-11 Filesystem driver
// 
// Based on
//	RT–11 Volume and File Formats Manual
//	http://www.bitsavers.org/pdf/dec/pdp11/rt11/v5.6_Aug91/AA-PD6PA-TC_RT-11_Volume_and_File_Formats_Manual_Aug91.pdf

#include "pch.h"

#include "rt11-volume.h"

using namespace std;
using namespace std::chrono;
using namespace std::filesystem;


namespace U::FS {

static const DateTime s_defaultDate(1972, 1, 1);

static const char Radix50Chars[41] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789";

uint16_t ToRadix50(wchar_t c) {
	unsigned ch = (unsigned)toupper(c);
	if (ch < 127)
		if (auto p = strchr(Radix50Chars, (char)ch))
			return int(p - Radix50Chars);
	throw exception("Invalid chars in filename");
}

uint16_t ToRadix50(const char chars[3]) {
	return ToRadix50(chars[0]) * 40 * 40 + ToRadix50(chars[1]) * 40 + ToRadix50(chars[2]);
}

void EncodeRadix50(const String& s, uint16_t filename[3]) {
	filename[0] = filename[1] = filename[2] = 0;
	int i = 0;
	for (int n = min((int)s.length(), 6); i < n; ++i) {
		auto ch = s[i];
		if (ch == '.')
			break;
		uint16_t w = ToRadix50(ch);
		auto qr = div(i, 3);
		switch (qr.rem) {
		case 0:
			w *= 40 * 40;
			break;
		case 1:
			w *= 40;
		}
		filename[qr.quot] += w;
	}
	if (s.length() > i) {
		if (s.length() > i + 4 || s[i] != '.')
			throw exception("Filename cannot be converted into RADIX-50");
		filename[2] = uint16_t(
			(i < s.length() ? ToRadix50(s[i+1]) : 0) * 40 * 40)
			+ (i + 1 < s.length() ? ToRadix50(s[i + 2]) : 0) * 40
			+ (i + 2 < s.length() ? ToRadix50(s[i + 3]) : 0);
	}
}

String DecodeRadix50(uint16_t w) {
	char r[3];
	for (int i = 3; i-- > 0;) {
		auto qr = div(w, 40);
		r[i] = Radix50Chars[qr.rem];
		w = (uint16_t)qr.quot;
	}
	if (w)
		throw invalid_argument("Invalid RADIX-50 value");
	return String(r, 3);
}

static const char rt11BootSectorMessage[10] = "\n?BOOT-U-";
static const char * const rt11VolumeIdentification = "RT11A       ";
static const char* const rt11SystemIdentification = "DECRT11A    ";

DateTime Rt11Volume::FromRt11DateFormat(uint16_t date) {
	return date
		? DateTime(
			1972 + (((date & 0xC000) >> 9) | (date & 0b11111))
			, (date & 0b11110000000000) >> 10
			, (date & 0b1111100000) >> 5)
		: s_defaultDate;
}

vector<DirEntry> Rt11Volume::GetDirEntries(uint32_t cluster, bool bWithExtra) {
	vector<DirEntry> r;
	uint8_t homeBlock[512];
	ReadBlock(1, homeBlock);
	uint8_t segmentSectors[1024];
	auto blkSegment = load_little_u16(homeBlock + 0724);
	for (uint16_t nextSegment = 1; nextSegment; blkSegment += 2) {
		ReadBlock(blkSegment, segmentSectors);
		ReadBlock(blkSegment + 1, segmentSectors + 512);
		nextSegment = load_little_u16(segmentSectors + 2);
		auto extraBytes = load_little_u16(segmentSectors + 6);
		auto fileDataBlock = load_little_u16(segmentSectors + 8);
		if (extraBytes & 1)
			throw exception("Invalid odd extra bytes field value in directory entry");
		int entrySize = 14 + extraBytes;
		auto nEntry = 507 * 2 / entrySize;
		for (int i = 0; i < nEntry; ++i) {
			const uint8_t* entry = segmentSectors + 10 + entrySize * i;
			auto status = (DirectoryEntryStatus)load_little_u16(entry);
			DirEntry dirEntry;
			switch (status) {
			case DirectoryEntryStatus::EndOfSegment:
				goto LAB_EOS;
			case DirectoryEntryStatus::Empty:
				dirEntry.Empty = true;
				break;
			default:
				dirEntry.FileName = (DecodeRadix50(load_little_u16(entry + 2)) + DecodeRadix50(load_little_u16(entry + 4))).Trim()
					+ "." + DecodeRadix50(load_little_u16(entry + 6)).Trim();
				dirEntry.CreationTime = FromRt11DateFormat(load_little_u16(entry + 12));
				dirEntry.LastWriteTime = dirEntry.LastAccessTime = dirEntry.CreationTime;
			}
			dirEntry.ReadOnly = (uint16_t)status & (uint16_t)DirectoryEntryStatus::Protected;
			dirEntry.DirEntryDiskOffset = (uint32_t)blkSegment * 512 + int(entry - segmentSectors);
			dirEntry.EntrySize = entrySize;
			auto len = load_little_u16(entry + 8);
			dirEntry.FirstCluster = fileDataBlock;
			fileDataBlock += len;
			NumberOfBlocks = max(NumberOfBlocks, fileDataBlock);
			dirEntry.Length = len * 512;
			dirEntry.ExtraData = Blob(entry + 14, extraBytes);
			if (!dirEntry.Empty || bWithExtra)
				r.push_back(dirEntry);
		}
	LAB_EOS:
		;
	}
	return r;
}

class DirectoryWriter {
	Rt11Volume& volume;
	uint8_t segmentSectors[1024];
	int curEntry = 0;
	uint16_t curDataSector;
	uint16_t curSegmentId = 1;
	uint16_t totalSegments;
	uint16_t secDirectory, secCurSegment;

	void LoadSegment() {
		volume.ReadBlock(secCurSegment, segmentSectors);
		volume.ReadBlock(secCurSegment + 1, segmentSectors + 512);
		if (secCurSegment == secDirectory) {
			totalSegments = load_little_u16(segmentSectors);
			if (totalSegments < 1 || totalSegments > 31)
				Throw(HRESULT_FROM_WIN32(ERROR_DISK_CORRUPT));
		}
		else
			store_little_u16(segmentSectors + 8, curDataSector);
		curDataSector = load_little_u16(segmentSectors + 8);
		curEntry = 0;
	}

	void SaveSegment(bool bLast) {
		auto entrySize = 14 + load_little_u16(segmentSectors + 6);
		store_little_u16(segmentSectors + 2, bLast ? 0 : curSegmentId + 1);
		store_little_u16(segmentSectors + 10 + curEntry * entrySize, uint16_t(Rt11Volume::DirectoryEntryStatus::EndOfSegment));
		volume.WriteBlock(secCurSegment, segmentSectors);
		volume.WriteBlock(secCurSegment + 1, segmentSectors + 512);
		secCurSegment += 2;
		if (bLast) {
			uint8_t highestSegmentInUse[2];
			store_little_u16(highestSegmentInUse, curSegmentId);
			volume.Fs.Position = secDirectory * 512 + 4;
			volume.Fs.WriteBuffer(highestSegmentInUse, sizeof(highestSegmentInUse));
		}

		curSegmentId = curSegmentId + 1;
	}
public:
	DirectoryWriter(Rt11Volume& volume)
		: volume(volume) {
		uint8_t homeBlock[512];
		volume.ReadBlock(1, homeBlock);
		secCurSegment = secDirectory = load_little_u16(homeBlock + 0724);
		LoadSegment();
	}

	~DirectoryWriter() {
		if (curDataSector < volume.NumberOfBlocks)
			WriteEmptyEntry(volume.NumberOfBlocks - curDataSector);
		SaveSegment(true);
	}

	void WriteEntry(const DirEntry & entry) {
		if ((secCurSegment - secDirectory) / 2 >= totalSegments)
			throw exception("There is no more directory segments");
		auto extraBytes = load_little_u16((const uint8_t*)segmentSectors + 6);
		auto entrySize = 14 + extraBytes;

		/*!!!R
		if (10 + (curEntry + 1) * entrySize > 1024) {
			if ((secCurSegment + 2 - secDirectory) / 2 >= totalSegments)
				throw exception("There is no more directory segments");
			store_little_u16(segmentSectors + 2, curSegmentId + 1);
			SaveSegment();
			LoadSegment();
		}*/

		uint8_t *buf = segmentSectors + 10 + curEntry * entrySize;
		memset(buf, 0, entrySize);
		uint16_t status = uint16_t(entry.Empty ? Rt11Volume::DirectoryEntryStatus::Empty : Rt11Volume::DirectoryEntryStatus::Permanant);
		if (entry.ReadOnly)
			status |= uint16_t(Rt11Volume::DirectoryEntryStatus::Protected);
		store_little_u16(buf, status);
		uint16_t filename[3];
		EncodeRadix50(entry.FileName, filename);
		store_little_u16(buf + 2, filename[0]);
		store_little_u16(buf + 4, filename[1]);
		store_little_u16(buf + 6, filename[2]);
		uint16_t nFileSizeInBlocks = uint16_t(entry.Length / 512);
		store_little_u16(buf + 8, nFileSizeInBlocks);
		auto y = (int)entry.CreationTime.Year - 1972;
		store_little_u16(buf + 12, uint16_t(
			y & 0x1F | ((y & 0x60) << 9)
			| ((unsigned)entry.CreationTime.Month << 10)
			| ((unsigned)entry.CreationTime.Day << 5)
		));
		memcpy(buf + 14, entry.ExtraData.constData(), std::min((uint16_t)entry.ExtraData.size(), extraBytes));
		curDataSector += nFileSizeInBlocks;
		++curEntry;
		if (507 * 2 - curEntry * entrySize <= entrySize) {
			SaveSegment(false);
			LoadSegment();
		}
	}

	void WriteEmptyEntry(uint16_t nBlock) {
		DirEntry entryEmpty;
		entryEmpty.Empty = true;
		entryEmpty.FileName = "EMPTY.FIL";
		entryEmpty.CreationTime = s_defaultDate;
		entryEmpty.Length = (int64_t)nBlock * 512;
		WriteEntry(entryEmpty);
	}

	void WritePermanentEntry(const DirEntry& entry) {
		if (entry.FirstCluster > curDataSector) {
			WriteEmptyEntry(uint16_t(entry.FirstCluster - curDataSector));
		}
		WriteEntry(entry);
	}
};

void Rt11Volume::WriteDirectory() {
	DirectoryWriter w(*this);
	for (const auto& entry : Files)
		w.WritePermanentEntry(entry);
}

int64_t Rt11Volume::FreeSpace() {
	int64_t freeSpace = 0;
	for (const auto& e : GetDirEntries(true))
		if (e.Empty)
			freeSpace += e.Length;
	return freeSpace;
}

void Rt11Volume::InsertIntoFiles(const DirEntry& entry) {
	for (auto it = Files.begin(); it != Files.end(); ++it) {
		if (entry.FirstCluster < it->FirstCluster) {
			Files.insert(it, entry);
			return;
		}
	}
	Files.push_back(entry);
}

void Rt11Volume::CopyFileTo(const DirEntry& fileEntry, Stream& os) {
	for (int i = 0, nBlocks = int((fileEntry.Length + 511) / 512); i < nBlocks; ++i) {
		byte block[512];
		ReadBlock(uint32_t(fileEntry.FirstCluster + i), block);
		os.WriteBuffer(block, 512);
	}
}

Rt11Volume::CFiles::iterator Rt11Volume::GetEntry(RCString filename) {
	for (auto it = Files.begin(); it != Files.end(); ++it)
		if (it->FileName == filename)
			return it;
	Throw(errc::no_such_file_or_directory);
}

void Rt11Volume::RemoveFile(RCString filename) {
	EnsureWriteMode();
	auto it = GetEntry(filename);
	Fs.Position = it->DirEntryDiskOffset;
	uint16_t status = (uint16_t)DirectoryEntryStatus::Empty;
	char buf[2] = { (char)status, (char)(status >> 8) };
	Fs.WriteBuffer(buf, 2);
	Files.erase(it);
}

// Squeeze
void Rt11Volume::Defragment() {
	EnsureWriteMode();
	uint16_t curFreeDataSector = (uint16_t)GetDirEntries(true)[0].FirstCluster;
	for (auto& entry : Files) {
		uint16_t nSizeInBlocks = uint16_t(entry.Length / 512);
		if (entry.FirstCluster > curFreeDataSector) {
			for (int i = 0; i < nSizeInBlocks; ++i)
				WriteBlock(curFreeDataSector + i, ReadBlock(uint32_t(entry.FirstCluster + i)));
			entry.FirstCluster = curFreeDataSector;
		}
		curFreeDataSector = uint32_t(entry.FirstCluster + nSizeInBlocks);
	}
	WriteDirectory();
	Flush();
}

pair<vector<wchar_t>, vector<wchar_t>> Rt11Volume::ValidInvalidFilenameChars() {
	vector<wchar_t> valid;
	for (const char *p = "ABCDEFGHIGKLMNOPQRSTUVWXYZ0123456789."; *p; ++p)
		valid.push_back(*p++);
	return make_pair(valid, vector<wchar_t>());
}

void Rt11Volume::AddFile(const String& filename, uint64_t len, Stream& istm, const DateTime& creationTimestamp) {
	auto uppercaseFilename = filename.ToUpper();
	for (auto& e : Files) {
		if (e.FileName == uppercaseFilename)
			Throw(errc::file_exists);
	}
	ModifyFile(filename, len, istm, creationTimestamp);
}

void Rt11Volume::ModifyFile(const String& filename, uint64_t len, Stream& istm, const DateTime& creationTimestamp) {
	EnsureWriteMode();
	auto uppercaseFilename = filename.ToUpper();
	for (auto& e : Files) {
		if (e.FileName == uppercaseFilename) {
			RemoveFile(filename);
			break;
		}
	}

	auto nSector = uint16_t((len + 511) / 512);
	auto o = Allocate(nSector);
	if (!o) {
		Defragment();
		o = Allocate(nSector);
		if (!o)
			Throw(errc::no_space_on_device);
	}
	DirEntry entry = *o;
	entry.Length = nSector * 512;
	entry.FileName = uppercaseFilename;
	entry.Empty = false;
	entry.CreationTime = creationTimestamp;
	if (nSector) {
		byte buf[512];
		ZeroStruct(buf);
		Fs.Position = (entry.FirstCluster + nSector - 1) * BytesPerSector;
		Fs.WriteBuffer(buf, 512);			// Zero last block to avoid garbage if istm size is not multiple of 512
		Fs.Position = (uint64_t)entry.FirstCluster * BytesPerSector;
		istm.CopyTo(Fs);
	}
	Files = GetFiles();
	InsertIntoFiles(entry);
	WriteDirectory();
}

void Rt11Volume::Init(const path& filepath) {
	base::Init(filepath);
	Files = GetFiles();
}

Rt11Volume::Rt11Volume()
{
}

Rt11Volume::~Rt11Volume() {
	TRC(1, "");
}

void Rt11Volume::ReadBlock(int n, void* data) {
	Fs.Position = n * 512;
	Fs.ReadExactly(data, 512);
}

void Rt11Volume::WriteBlock(int n, const void* data) {
	EnsureWriteMode();
	Fs.Position = n * 512;
	Fs.WriteBuffer(data, 512);
}

optional<DirEntry> Rt11Volume::Allocate(uint16_t nBlock) {
	for (auto& entry : GetDirEntries(true)) {
		if (entry.Empty && entry.Length >= nBlock * 512)
			return entry;
	}
	return optional<DirEntry>();
}

void DirEntry::Read(const BinaryReader& rd) {
	rd >> FileName >> Length >> CreationTime;
}

void DirEntry::Write(BinaryWriter& wr) const {
	wr << FileName << Length << CreationTime;
}

static const uint16_t
	c_sysVerV3A = ToRadix50("V3A")
	, c_sysVerV05A = ToRadix50("V05");

static class Rt11VolumeFactory : public IVolumeFactory {
	int IsSupportedVolume(RCSpan s) override {
		auto data = s.data();
		if (s.size() < 1024)
			return 0;
		int weights = 0;
		if (load_little_u16(data) == 0240)						// PDP-11 NOP opcode
			weights += 1;
		if (!memcmp(data + 031, rt11BootSectorMessage, sizeof(rt11BootSectorMessage) - 1))
			weights += 2;
		auto sysVer = load_little_u16(data + 01726);
		if (sysVer == c_sysVerV3A || sysVer == c_sysVerV05A)
			weights += 1;
		if (!memcmp(data + 01730, rt11VolumeIdentification, 12))
			weights += 2;
		if (!memcmp(data + 01760, rt11SystemIdentification, 12))
			weights += 2;
		uint16_t checksum = 0;
		for (int off = 512; off < 1022; off += 2)
			checksum += load_little_u16(data + off);
		if (checksum == load_little_u16(data + 1022))
			weights += 2;
		return weights > 3 ? weights : 0;
	}

	unique_ptr<Volume> CreateInstance() override {
		return unique_ptr<Volume>(new Rt11Volume);
	}
} s_rt11VolumeFactory;


} // U::FS
