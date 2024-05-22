// Copyright(c) 2023 Ufasoft  http://ufasoft.com  mailto:support@ufasoft.com,  Sergey Pavlov  mailto:dev@ufasoft.com
//
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Total Commander WCX plugin
// Based on https://ghisler.github.io/WCX-SDK/table_of_contents.htm


#include "pch.h"
#include "driver/fat-volume.h"

#include <total-commander/wcxhead.h>
using namespace U::FS;

#if UCFG_PLATFORM_IX86
#	define TOTAL_EXPORT0(fun) comment(linker, _STL_STRINGIZE(/export:##fun=_Total##fun@0))
#	define TOTAL_EXPORT1(fun) comment(linker, _STL_STRINGIZE(/export:##fun=_Total##fun@4))
#	define TOTAL_EXPORT2(fun) comment(linker, _STL_STRINGIZE(/export:##fun=_Total##fun@8))
#	define TOTAL_EXPORT3(fun) comment(linker, _STL_STRINGIZE(/export:##fun=_Total##fun@12))
#	define TOTAL_EXPORT4(fun) comment(linker, _STL_STRINGIZE(/export:##fun=_Total##fun@16))
#	define TOTAL_EXPORT5(fun) comment(linker, _STL_STRINGIZE(/export:##fun=_Total##fun@20))
#else
#	define TOTAL_EXPORT0(fun) comment(linker, _STL_STRINGIZE(/export:##fun=Total##fun))
#	define TOTAL_EXPORT1(fun) comment(linker, _STL_STRINGIZE(/export:##fun=Total##fun))
#	define TOTAL_EXPORT2(fun) comment(linker, _STL_STRINGIZE(/export:##fun=Total##fun))
#	define TOTAL_EXPORT3(fun) comment(linker, _STL_STRINGIZE(/export:##fun=Total##fun))
#	define TOTAL_EXPORT4(fun) comment(linker, _STL_STRINGIZE(/export:##fun=Total##fun))
#	define TOTAL_EXPORT5(fun) comment(linker, _STL_STRINGIZE(/export:##fun=Total##fun))
#endif

int ToErrorCode(Exception& ex) {
	auto ec = ex.code();
	if (ec == errc::not_enough_memory)
		return E_NO_MEMORY;
	else if (ec == errc::function_not_supported)
		return E_NOT_SUPPORTED;
	else if (ec == errc::too_many_files_open)
		return E_TOO_MANY_FILES;
	else if (ec == errc::operation_canceled)
		return E_EABORTED;
	else if (ec == errc::io_error)
		return E_EREAD;
	else
		return E_BAD_ARCHIVE;
}

#pragma TOTAL_EXPORT0(GetPackerCaps)
extern "C" int __stdcall TotalGetPackerCaps() {
	return PK_CAPS_MODIFY | PK_CAPS_MULTIPLE | PK_CAPS_DELETE | PK_CAPS_BY_CONTENT;
}

#pragma TOTAL_EXPORT1(CanYouHandleThisFileW)
extern "C" BOOL __stdcall TotalCanYouHandleThisFileW(WCHAR* FileName) {
	try {
		FileStream ifs(FileName, FileMode::Open, FileAccess::Read);
		uint8_t buf[128 * 1024];
		auto cb = ifs.Read(buf, sizeof(buf));
		return IVolumeFactory::FindBestFactory(Span(buf, cb)) ? TRUE : FALSE;
	} catch (Exception&) { return FALSE; }
}

class FileEnumerator {
	static const int MaxLevels = 100;

	vector<DirEntry> entries;
	int idxCur = -1;

	void CoolectEntries(RCString dir, int nLevel = 0) {
		if (nLevel > MaxLevels)
			Throw(ExtErr::RecursionTooDeep);
		for (size_t i = 0; i < Vol->Files.size(); ++i) {
			DirEntry e = Vol->Files[i];
			auto relative = e.FileName;
			e.FileName = !dir ? relative : dir + "\\" + relative;
			if (e.IsDirectory) {
				Vol->ChangeDirectory(relative);
				CoolectEntries(e.FileName, nLevel + 1);
				Vol->ChangeDirectory("..");
			} else
				entries.push_back(e);
		}
	}
public:
	unique_ptr<Volume> Vol;

	FileEnumerator(Volume *vol)
		: Vol(vol) {
		CoolectEntries(nullptr);
	}

	const DirEntry& Cur() { return entries[idxCur]; }

	bool Next(DirEntry& e) {
		if (idxCur >= (int)entries.size() - 1)
			return false;
		e = entries[++idxCur];
		return true;
	}
};

extern "C" int	__stdcall TotalDeleteFilesW(WCHAR * PackedFile, WCHAR * DeleteList);

#define MakePtr(cast, ptr, addValue) (cast)((uintptr_t)(ptr)+(DWORD)(addValue))


extern "C" {
	extern void* g_pfnDeleteFilesW;
}

static void PatchExportedPointers() {
	g_pfnDeleteFilesW = &TotalDeleteFilesW;
}

#pragma TOTAL_EXPORT1(OpenArchive)
extern "C" HANDLE __stdcall TotalOpenArchive(tOpenArchiveData * ArchiveData) {
	try {
		return new FileEnumerator(IVolumeFactory::Mount(ArchiveData->ArcName).release());
	} catch (Exception& ex) {
		ArchiveData->OpenResult = ToErrorCode(ex);
		return 0;
	}
}

#pragma TOTAL_EXPORT1(OpenArchiveW)
extern "C" HANDLE __stdcall TotalOpenArchiveW(tOpenArchiveDataW *ArchiveData)
{
	try {
		PatchExportedPointers();
		return new FileEnumerator(IVolumeFactory::Mount(ArchiveData->ArcName).release());
	} catch (Exception& ex) {
		ArchiveData->OpenResult = ToErrorCode(ex);
		return 0;
	}
}

#pragma TOTAL_EXPORT1(CloseArchive)
extern "C" int __stdcall TotalCloseArchive(HANDLE hArcData) {
	try {
		delete static_cast<FileEnumerator*>(hArcData);
	} catch (Exception& ex) { return ToErrorCode(ex); }
	return 0;
}

static int GetAttrs(const DirEntry& e) {
	return (e.ReadOnly ? FILE_ATTRIBUTE_READONLY : 0)
		| (e.Hidden ? FILE_ATTRIBUTE_HIDDEN : 0)
		| (e.IsArchive ? FILE_ATTRIBUTE_ARCHIVE : 0)
		| (e.IsSystem ? FILE_ATTRIBUTE_SYSTEM : 0)
		| (e.IsVolumeLabel ? 8 : 0)
		| (e.IsDirectory ? FILE_ATTRIBUTE_DIRECTORY : 0);
}

#pragma TOTAL_EXPORT2(ReadHeader)
extern "C" int __stdcall TotalReadHeader(HANDLE hArcData, tHeaderData* HeaderData) {
	FileEnumerator& fe = *static_cast<FileEnumerator*>(hArcData);
	try {
		DirEntry e;
		if (!fe.Next(e))
			return E_END_ARCHIVE;
		ZeroStruct(*HeaderData);
		auto dt = FatDateTime::Clamp(e.CreationTime);
		strncpy(HeaderData->FileName, e.FileName, size(HeaderData->FileName));
		HeaderData->FileTime = ((uint32_t)dt.Date << 16) | dt.Time;
		HeaderData->UnpSize = HeaderData->PackSize = (uint32_t)e.Length;
		HeaderData->FileAttr = GetAttrs(e);
	} catch (Exception& ex) { return ToErrorCode(ex); }
	return 0;
}

#pragma TOTAL_EXPORT2(ReadHeaderEx)
extern "C" int __stdcall TotalReadHeaderEx(HANDLE hArcData, tHeaderDataEx * HeaderData) {
	FileEnumerator& fe = *static_cast<FileEnumerator*>(hArcData);
	try {
		DirEntry e;
		if (!fe.Next(e))
			return E_END_ARCHIVE;
		ZeroStruct(*HeaderData);
		auto dt = FatDateTime::Clamp(e.CreationTime);
		strncpy(HeaderData->FileName, e.FileName, size(HeaderData->FileName));
		HeaderData->FileTime = ((uint32_t)dt.Date << 16) | dt.Time;
		HeaderData->UnpSize = HeaderData->PackSize = (uint32_t)e.Length;
		HeaderData->UnpSizeHigh = HeaderData->PackSizeHigh = (uint32_t)(e.Length >> 32);
		HeaderData->FileAttr = GetAttrs(e);
	} catch (Exception& ex) { return ToErrorCode(ex); }
	return 0;
}

#pragma TOTAL_EXPORT2(ReadHeaderExW)
extern "C" int __stdcall TotalReadHeaderExW(HANDLE hArcData, tHeaderDataExW* HeaderData) {
	FileEnumerator& fe = *static_cast<FileEnumerator*>(hArcData);
	try {
		DirEntry e;
		if (!fe.Next(e))
			return E_END_ARCHIVE;
		ZeroStruct(*HeaderData);
		auto dt = FatDateTime::Clamp(e.CreationTime);
		_tcsncpy(HeaderData->FileName, e.FileName, size(HeaderData->FileName));
		HeaderData->FileTime = ((uint32_t)dt.Date << 16) | dt.Time;
		HeaderData->UnpSize = HeaderData->PackSize = (uint32_t)e.Length;
		HeaderData->UnpSizeHigh = HeaderData->PackSizeHigh = (uint32_t)(e.Length >> 32);
		HeaderData->FileAttr = GetAttrs(e);			
	} catch (Exception& ex) { return ToErrorCode(ex); }
	return 0;
}

static int ProcessFile(HANDLE hArcData, int Operation, RCString DestPath, RCString DestName) {
	FileEnumerator& fe = *static_cast<FileEnumerator*>(hArcData);
	Volume& volume = *fe.Vol;
	switch (Operation) {
	case PK_TEST:
	case PK_EXTRACT: {
		path dest = !!DestPath ? path(DestPath.c_wstr()) / DestName : DestName.c_wstr();
		FileStream ofs(dest, FileMode::CreateNew, FileAccess::Write);
		auto e = fe.Cur();
		auto parts = e.FileName.Split("\\");
		volume.ChangeDirectory("/");
		int i;
		for (i = 0; i < parts.size() - 1; ++i)
			volume.ChangeDirectory(parts[i]);
		volume.CopyFileTo(*volume.GetEntry(parts[i]), ofs);
	}
	case PK_SKIP:
		break;
	default:
		return E_NOT_SUPPORTED;
	}
	return 0;
}

#pragma TOTAL_EXPORT4(ProcessFile)
extern "C" int __stdcall TotalProcessFile(HANDLE hArcData, int Operation, CHAR* DestPath, CHAR* DestName) {
	try {
		return ProcessFile(hArcData, Operation, DestPath, DestName);
	} catch (Exception& ex) { return ToErrorCode(ex); }
}

#pragma TOTAL_EXPORT4(ProcessFileW)
extern "C" int __stdcall TotalProcessFileW(HANDLE hArcData, int Operation, WCHAR* DestPath, WCHAR* DestName) {
	try {
		return ProcessFile(hArcData, Operation, DestPath, DestName);
	} catch (Exception& ex) { return ToErrorCode(ex); }
}

#pragma TOTAL_EXPORT5(PackFilesW)
extern "C" int	__stdcall TotalPackFilesW(WCHAR *PackedFile, WCHAR *SubPath, WCHAR *SrcPath, WCHAR *AddList, int Flags) {
	try {
		unique_ptr<Volume> vol = IVolumeFactory::Mount(PackedFile);
		if (SubPath)
			vol->ChangeDirectory(SubPath);
		String dest = path(SrcPath).filename();
		DateTime creationTime = FileSystemInfo(SrcPath, false).CreationTime;
		{
			FileStream ifs(SrcPath, FileMode::Open, FileAccess::Read);
			vol->ModifyFile(dest, ifs.Length, ifs, creationTime);
		}
		if (Flags & PK_PACK_MOVE_FILES)
			filesystem::remove(SrcPath);
	} catch (Exception& ex) { return ToErrorCode(ex); }
	return 0;
}

// Collision with Far Manager's plugin exported name
#pragma TOTAL_EXPORT2(DeleteFiles)
extern "C" int	__stdcall TotalDeleteFiles(CHAR *PackedFile, CHAR *DeleteList) {
	try {
		unique_ptr<Volume> vol = IVolumeFactory::Mount(PackedFile);
		for (CHAR* p = DeleteList; *p;) {
			vol->RemoveFile(p);
			while (*p++);
		}
	} catch (Exception& ex) { return ToErrorCode(ex); }
	return 0;
}

//#pragma TOTAL_EXPORT2(DeleteFilesW)
extern "C" int	__stdcall TotalDeleteFilesW(WCHAR* PackedFile, WCHAR* DeleteList) {
	try {
		unique_ptr<Volume> vol = IVolumeFactory::Mount(PackedFile);
		for (WCHAR* p = DeleteList; *p;) {
			vol->RemoveFile(p);
			while (*p++);
		}
	} catch (Exception& ex) { return ToErrorCode(ex); }
	return 0;
}

static HWND g_hwndParent;

#pragma TOTAL_EXPORT2(ConfigurePacker)
extern "C" void __stdcall TotalConfigurePacker(HWND Parent, HINSTANCE DllInstance) {
	g_hwndParent = Parent;
}

static tProcessDataProc s_ProcessDataProc;
static tProcessDataProcW s_ProcessDataProcW;

static tChangeVolProc s_pfnChangeVolProc;
static tChangeVolProcW s_pfnChangeVolProcW;

#pragma TOTAL_EXPORT2(SetProcessDataProc)
extern "C" void __stdcall TotalSetProcessDataProc(HANDLE hArcData, tProcessDataProc pProcessDataProc) {
	s_ProcessDataProc = pProcessDataProc;
}

#pragma TOTAL_EXPORT2(SetProcessDataProcW)
extern "C" void __stdcall TotalSetProcessDataProcW(HANDLE hArcData, tProcessDataProcW pProcessDataProc) {
	s_ProcessDataProcW = pProcessDataProc;
}

#pragma TOTAL_EXPORT2(SetChangeVolProc)
extern "C" void __stdcall TotalSetChangeVolProc(HANDLE hArcData, tChangeVolProc pChangeVolProc1) {
	s_pfnChangeVolProc = pChangeVolProc1;
}

#pragma TOTAL_EXPORT2(SetChangeVolProcW)
extern "C" void __stdcall TotalSetChangeVolProcW(HANDLE hArcData, tChangeVolProcW pChangeVolProc1) {
	s_pfnChangeVolProcW = pChangeVolProc1;
}
