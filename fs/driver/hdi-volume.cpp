#include "pch.h"

#include "volume.h"

using namespace std;

namespace U::FS {

class HdiVolume : public Volume {
	typedef Volume base;
protected:
	int ReservedSectors = 0;

public:
	int Cylinders = -1, Heads = -1, Sectors = -1;
	int Partitions = -1;

	static bool CheckSum(RCSpan s) {
		if (s[510] != 0xA5)
			return false;
		uint8_t sum = 0;
		for (int i = 0; i < 512; ++i)
			sum += s[i];
		return !sum;
	}
protected:
	void Init(const path& filepath) override {
		base::Init(filepath);
		uint8_t buf[512];
		Fs.Position = 0;
		Fs.ReadExactly(buf, 512);
		if (CheckSum(buf))
			++ReservedSectors;					// .hdi
	}

	void Inverse(uint8_t buf[512]) {
		for (int i = 0; i < 512; ++i)
			buf[i] = ~buf[i];
	}

	void ReadSector(int sec, uint8_t buf[512]) {
		Fs.Position = (ReservedSectors + sec) * BytesPerSector;
		Fs.ReadExactly(buf, BytesPerSector);
		Inverse(buf);
	}

	int64_t FreeSpace() override {
		int64_t r = (int64_t)Cylinders * Heads * Sectors * BytesPerSector;
		for (auto& e : Files)
			r -= e.Length;
		return r;
	}

	int64_t CalcPosition(int64_t lba) {
		return (ReservedSectors + lba) * BytesPerSector;
	}

	void CopyFileTo(const DirEntry& fileEntry, Stream& os) override {
		Fs.Position = CalcPosition(fileEntry.FirstCluster);
		for (int64_t len = fileEntry.Length; len > 0; len -= 512) {
			uint8_t buf[512];
			Fs.ReadExactly(buf, 512);
			Inverse(buf);
			os.WriteBuffer(buf, 512);
		}
	}

	void ModifyFile(RCString filename, uint64_t len, Stream& istm, const DateTime& creationTimestamp) {
		EnsureWriteMode();
		auto& e = *GetEntry(filename);
		if (e.Length != len)
			throw invalid_argument("Partition size is not equal to size of the new contents");
		for (; len > 0; len -= 512) {
			uint8_t buf[512];
			istm.ReadExactly(buf, 512);
			Inverse(buf);
			Fs.WriteBuffer(buf, 512);
		}
	}
};

class AltProVolume : public HdiVolume {
	typedef HdiVolume base;

	void Init(const path& filepath) override {
		base::Init(filepath);
		uint8_t buf[512];
		ReadSector(7, buf);
		Cylinders = load_little_u16(buf + 510);
		Heads = buf[508];
		Sectors = load_little_u16(buf + 506);
		Partitions = buf[504];
		Files = GetDirEntries(0, false);
	}
private:
	vector<DirEntry> GetDirEntries(uint32_t cluster, bool bWithExtra) override {
		vector<DirEntry> r;
		uint8_t buf[512];
		ReadSector(7, buf);
		for (int i = 0; i < Partitions; ++i) {
			DirEntry e;
			int16_t cyl = (int16_t)load_little_u16(buf + 502 - i * 4);
			e.ReadOnly = cyl < 0;
			cyl = e.ReadOnly ? -cyl : cyl;
			int head = cyl & 0xF;
			cyl >>= 4;
			if (cyl >= Cylinders)
				Throw(HRESULT_FROM_WIN32(ERROR_DISK_CORRUPT));
			e.FileName = "Partition " + Convert::ToString(i + 1) + ".dsk";
			e.FirstCluster = (cyl * Heads + head) * Sectors;
			e.Length = (uint32_t)load_little_u16(buf + 500 - i * 4) * BytesPerSector;
			r.push_back(e);
		}
		return r;
	}
};

class HdiVolumeFactory : public IVolumeFactory {
protected:
	void ReadSector(RCSpan s, int sector, uint8_t buf[512]) {
		memcpy(buf, s.data() + sector * 512, 512);
		for (int i = 0; i < 512; ++i)
			buf[i] = ~buf[i];
	}
};

static class AltProVolumeFactory : public HdiVolumeFactory {
	int IsSupportedVolume(RCSpan s) override {
		if (s.size() < 8 * 512 )
			return false;
		int weight = 0;
		int reserved = 0;
		if (HdiVolume::CheckSum(s)) {
			weight += 2;
			++reserved;
		}
		uint8_t buf[512];
		ReadSector(s, reserved + 7, buf);
		int cyls = load_little_u16(buf + 510);
		int heads = buf[508];
		int sectors = load_little_u16(buf + 506);
		int parts = buf[504];

		uint16_t crc = 012701;
		for (int i = 0; i < parts * 2 + 4; ++i)
			crc += load_little_u16(buf + 510 - i * 2);
		if (load_little_u16(buf + 502 - parts * 4) != crc)
			return false;
		weight += 2;
		return weight >= 2 ? weight : 0;
	}

	unique_ptr<Volume> CreateInstance() override {
		return unique_ptr<Volume>(new AltProVolume);
	}
} s_altProVolumeFactory;

class SamaraVolume : public HdiVolume {
	typedef HdiVolume base;

	void Init(const path& filepath) override {
		base::Init(filepath);
		uint8_t buf[512];
		ReadSector(1, buf);
		Heads = buf[5] + 1;
		Sectors = buf[4];
		int cylVol = load_little_u16(buf + 2);
		Cylinders = int((Fs.Length / BytesPerSector - ReservedSectors + cylVol - 1) / cylVol);
		Files = GetDirEntries(0, false);
	}

	vector<DirEntry> GetDirEntries(uint32_t cluster, bool bWithExtra) override {
		vector<DirEntry> r;
		uint8_t buf[512];
		ReadSector(1, buf);
		for (int i = 0; i < 64; ++i) {
			auto lba = load_little_u32(buf + 6 + i * 4);
			if (!lba)
				break;
			uint8_t buf2[512];
			ReadSector(lba, buf2);
			DirEntry e;
			e.FileName = "Partition " + Convert::ToString(i + 1) + ".dsk";
			e.ReadOnly = buf2[4] & 2;
			e.FirstCluster = lba + 1;
			e.Length = (uint32_t)load_little_u16(buf2 + 2) * BytesPerSector;
			r.push_back(e);		
		}
		return r;
	}
};

static class SamaraVolumeFactory : public HdiVolumeFactory {
	int IsSupportedVolume(RCSpan s) override {
		if (s.size() < 8 * 512)
			return false;
		int weight = 0;
		int reserved = 0;
		if (HdiVolume::CheckSum(s)) {
			weight += 2;
			++reserved;
		}
		uint8_t buf[512];
		ReadSector(s, reserved + 1, buf);
		int cylVol = load_little_u16(buf + 2);
		int heads = buf[5] + 1;
		int sectors = buf[4];
		if (!cylVol || cylVol != heads * sectors)
			return false;
		int i;
		for (i = 0; i < 64; ++i) {
			auto lba = load_little_u32(buf + 6 + i * 4);
			if (!lba || (reserved + lba + 1) * 512 >= s.size())
				break;
			uint8_t buf2[512];
			ReadSector(s, (reserved + lba), buf2);
			if (load_little_u16(buf2) != i + 2)
				return false;
		}
		weight += i > 0 ? 1 : 0;
		return weight > 2 ? weight : 0;
	}

	unique_ptr<Volume> CreateInstance() override {
		return unique_ptr<Volume>(new SamaraVolume);
	}
} s_samaraVolumeFactory;


} // U::FS
