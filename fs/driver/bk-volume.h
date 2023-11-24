// Common functionality for BK-0010/0011 computer Filesystems

#pragma once

#include "volume.h"

namespace U::FS {

// Defines:
//	DirEntry.Aux1 as CurrentDirId
//	DirEntry.Aux2 as ParentDirId
//	DirEntry.Aux3 as Load Address


template <class T>
class BkVolume : public T {
	typedef T base;
protected:
	uint8_t RootDirId = 0, CurDirId = 0, ParentDirId = 0;

	uint8_t MaxDirId = 0xC8;

	BkVolume() {
		Encoding = &s_encodingKoi8;
	}

	void OptionalCopyHeader(const DirEntry& fileEntry, Stream& os) {
		if (!fileEntry.IsDirectory
			&& fileEntry.Aux3
			&& fileEntry.Aux3 != 0xFFFF
			&& Callback && Callback->Interactive && Callback->AskYesOrNo("Add .BIN Address/Length header?")
			) {
			uint8_t addressLen[4];
			store_little_u16(addressLen, (uint16_t)fileEntry.Aux3);
			store_little_u16(addressLen + 2, (uint16_t)fileEntry.Length);
			os.WriteBuffer(addressLen, sizeof(addressLen));
		}
	}

	void CopyFileTo(const DirEntry& fileEntry, Stream& os) override {
		OptionalCopyHeader(fileEntry, os);
		Fs.Position = (uint64_t)fileEntry.FirstCluster * BytesPerSector;
		array<byte, 512> buf;
		for (int64_t len = fileEntry.Length; len > 0; len -= buf.size()) {
			size_t cb = (size_t)min(len, (int64_t)buf.size());
			Fs.ReadExactly(buf.data(), cb);
			os.WriteBuffer(buf.data(), cb);
		}
	}

	// Detect and skip .BIN address/length header
	uint64_t AdjustLengthOnPut(Stream& istm, DirEntry& entry, uint64_t len) override {
		if (len >= 4) {
			uint64_t prevPos = istm.Position;
			uint8_t buf[4];
			istm.ReadExactly(buf, sizeof(buf));
			if (load_little_u16(buf + 2) == len - 4) {
				len -= 4;
				entry.Aux3 = load_little_u16(buf);				// Load Address
			} else
				istm.Position = prevPos;
		}
		return len;
	}

	DirEntry AllocateFileEntry() override {
		auto r = base::AllocateFileEntry();
		r.Aux2 = CurDirId;
		return r;
	}

	void MakeDirectory(RCString name) {
		EnsureWriteMode();
		if (FindEntry(name) != Files.end())
			Throw(errc::file_exists);

		unordered_set<uint8_t> freeDirIds;
		for (uint8_t i = 2; i < MaxDirId; ++i)
			freeDirIds.insert(i);
		auto entries = GetDirEntries(RootDirId, true);
		for (const auto& e : entries)
			freeDirIds.erase((uint8_t)e.Aux1);
		if (freeDirIds.empty())
			Throw(errc::no_space_on_device);
		DirEntry entry = AllocateFileEntry();
		entry.IsDirectory = true;
		entry.Aux1 = *freeDirIds.begin();
		AddToDirEntries(entries, entry);
		SaveDirEntries(entries);
		LoadCurDir();
	}

	void ChangeDirectory(RCString name) override {
		if (name == "/") {
			CurDirId = RootDirId;
			CurDirName = "";
		} else if (name == "..") {
			CurDirId = ParentDirId;
			if (CurDirId != RootDirId) {
				auto entries = GetDirEntries(RootDirId, true);
				for (auto& e : entries)
					if (e.Aux1 == CurDirId) {
						ParentDirId = (uint8_t)e.Aux2;
						CurDirName = e.FileName;
						break;
					}
			} else
				CurDirName = "";
		} else {
			auto& e = *GetEntry(name);
			if (!e.IsDirectory)
				Throw(E_FAIL);
			CurDirName = e.FileName;
			ParentDirId = CurDirId;
			CurDirId = (uint8_t)e.Aux1;
		}
		Files = GetDirEntries(CurDirId, false);
	}
};

} // U::FS::
