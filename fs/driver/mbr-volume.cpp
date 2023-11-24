#include "pch.h"

#include "volume.h"

using namespace std;
using namespace std::chrono;
using namespace std::filesystem;


namespace U::FS {

class MbrVolume : public Volume {
	void Init(const path& filePath) override { Throw(E_NOTIMPL); }
	vector<DirEntry> GetDirEntries(uint32_t cluster, bool bWithExtra) override { Throw(E_NOTIMPL); }
	CFiles::iterator GetEntry(RCString filename) override { Throw(E_NOTIMPL); }
	void AddFile(const String& filename, uint64_t len, Stream& istm, const DateTime& creationTimestamp) override { Throw(E_NOTIMPL); }
	void CopyFileTo(const DirEntry& fileEntry, Stream& os) override { Throw(E_NOTIMPL); }
	void RemoveFile(RCString filename) override { Throw(E_NOTIMPL); }

	pair<vector<wchar_t>, vector<wchar_t>> ValidInvalidFilenameChars() override {
		vector<wchar_t> invalid;
		invalid.push_back('/');
		invalid.push_back('\\');
		invalid.push_back(':');
		return make_pair(vector<wchar_t>(), invalid);
	}

	int MaxNameLength() override { return 255; }
};

/*!!!?
static class MbrVolumeFactory : public IVolumeFactory {
	int IsSupportedVolume(RCSpan s) override {
		auto data = s.data();
		return s.size() >= 512
			&& ((const uint8_t*)data)[510] == 0x55		// Boot signature
			&& ((const uint8_t*)data)[511] == 0xAA;
	}

	unique_ptr<Volume> CreateInstance() override {
		return unique_ptr<Volume>(new MbrVolume);
	}
} s_mbrVolumeFactory;
*/


} // U::FS
