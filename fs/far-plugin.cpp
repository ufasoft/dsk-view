// © 2023 Ufasoft https://ufasoft.com, Sergey Pavlov mailto:dev@ufasoft.com
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Far Manager plugin
// Based on https://documentation.help/FarManagerPlugins-ru/


#include "pch.h"
#include "volume.h"

#include <far/plugin.hpp>
#include <far/msg.hpp>
#include <far/uuid.hpp>

//#undef _ATL_USE_WINAPI_FAMILY_DESKTOP_APP
#include <atlbase.h>
#include <atlcom.h>
using namespace ATL;

using namespace std;
using namespace std::filesystem;
using namespace U::FS;

#define PLUGIN_NAME _T(VER_PRODUCTNAME_STR)
#define WINDOW_HEAD L"Open windows list" // Our menu title

#if UCFG_PLATFORM_IX86
#	define FAR_EXPORT(fun) comment(linker, _EXT_STRINGIZE(/export:##fun=_Far##fun@4))
#else
#	define FAR_EXPORT(fun) comment(linker, _EXT_STRINGIZE(/export:##fun=Far##fun))
#endif

const Guid
	c_guidFsPlugin	= "55534654-3F35-46d9-0050-54BA20230001"_uuid
	, c_guidDefragment = "55534654-3F35-46d9-0051-54BA20230001"_uuid;

const Version c_pluginVersion(VER_PRODUCTVERSION_MAJOR, VER_PRODUCTVERSION_MINOR);

constexpr inline auto
	c_plugin_guid = "65642111-AA69-4B84-B4B8-9249579EC4FA"_uuid,
	c_plugin_menu_guid = "7BCFBA0E-4EF5-466D-B5B9-23523459D6AF"_uuid,
	c_plugin_config_guid = "EE3D5A10-3D86-4C8C-B26C-8887D9E07EBE"_uuid,
	c_password_dialog_guid = "761F3B4C-FC45-4A9D-A383-3F75D505A43B"_uuid,
	c_overwrite_dialog_guid = "83B02899-4590-47F9-B4C1-6BC66C6CA4F9"_uuid,
	c_extract_dialog_guid = "97877FD0-78E6-4169-B4FB-D76746249F4D"_uuid,
	c_update_dialog_guid = "CD57D7FA-552C-4E31-8FA8-73D9704F0666"_uuid,
	c_settings_dialog_guid = "08A1229B-AD54-451B-8B53-6D5FD35BCFAA"_uuid,
	c_attr_dialog_guid = "17D10388-46C7-4BBD-A3CA-9A977711F4E5"_uuid,
	c_sfx_options_dialog_guid = "0DCE48E5-B205-44A0-B8BF-96B28E2FD3B3"_uuid,
	c_delete_files_dialog_guid = "4FCA2D71-682C-442F-8635-1211BD4495FB"_uuid,
	c_overwrite_archive_dialog_guid = "E17D3FE5-FA3D-4A89-BD14-DE8B50122DD8"_uuid,
	c_progress_dialog_guid = "1C5D302E-4AB7-4402-9405-CBCEC987A285"_uuid,
	c_interrupt_dialog_guid = "1132D7C0-8137-4503-90E4-04CC59E29C1C"_uuid,
	c_retry_ignore_dialog_guid = "576BC2CC-5547-421C-ABF8-B6DF747E711C"_uuid,
	c_error_log_dialog_guid = "53FB8447-1C00-42CB-934C-9AB2B50A29A9"_uuid,
	c_delete_profile_dialog_guid = "0A5411E3-40F3-415F-8100-AD45E635CBD1"_uuid,
	c_test_ok_dialog_guid = "2AE9F593-46B0-41AE-AFF2-29BA0C458832"_uuid,
	c_extract_params_saved_dialog_guid = "2B27B493-E3D0-445F-AA0B-FDF51D1DDC23"_uuid,
	c_arc_path_eval_dialog_guid = "6DA788EA-813C-46A2-9E5A-E10473770948"_uuid,
	c_error_dialog_guid = "7C5D2FE2-14B8-47EF-BF35-D314E1E2FAB4"_uuid,
	c_format_menu_guid = "42C8F05D-4E95-4069-A984-0CFD59E1B442"_uuid,
	c_main_menu_guid = "EAABFBA1-D321-4D8C-9573-A2A06EC22684"_uuid,
	c_arccmd_menu_guid = "83AD575C-BBE4-45A1-A033-345A4DFB6B74"_uuid,
	c_create_dir_dialog_guid = "65593288-4F2A-4410-9E3A-7C4339816992"_uuid,
	c_save_profile_dialog_guid = "9B55A916-1599-4181-90A5-CA37DE345716"_uuid,
	c_generic_guid = "4756DC8E-15E2-41FA-AE6D-E972B5FB43D8"_uuid,
	c_update_params_saved_dialog_guid = "1C453287-733E-4B3E-B0E4-CE0D8FA97DB6"_uuid,
	c_multi_select_dialog_guid = "BCBC467B-4F16-4CF1-9895-92A616A9BABD"_uuid,
	c_format_library_info_dialog_guid = "223C2003-A7FF-4907-A4A3-6BF10DEA8432"_uuid,
	c_far_guid = "00000000-0000-0000-0000-000000000000"_uuid;

static const GUID PluginMenuGuids[1] = {
	c_guidDefragment
};

static const wchar_t* PluginMenuStrings[1]{
	L"Defragment"
};

static String s_author = UCFG_AUTHOR;

#pragma FAR_EXPORT(GetGlobalInfoW)
extern "C" void WINAPI FarGetGlobalInfoW(GlobalInfo& info) {
	info.StructSize = sizeof(struct GlobalInfo);
	info.MinFarVersion = MAKEFARVERSION(3, 0, 0, 4504, VS_RELEASE);
	info.Version = MAKEFARVERSION(c_pluginVersion.Major, c_pluginVersion.Minor, 0, 0, VS_RELEASE);
	info.Guid = c_guidFsPlugin;
	info.Title = L"DSKView";				// Name Requirements: https://api.farmanager.com/ru/structures/globalinfo.html
	info.Description = _T(VER_FILEDESCRIPTION_STR);
	info.Author = s_author.c_wstr();
}

static struct PluginStartupInfo_V3 Far; // Our plug-in info
static FarStandardFunctions_V3 FarFsf;

void ShowErrorMessage(RCString msg, bool bWarning = false) {
	FARMESSAGEFLAGS flags = FMSG_MB_OK | (bWarning ? FMSG_WARNING : 0);
	const wchar_t* messages[2] = { L"Error", msg};
	Far.Message(&c_guidFsPlugin, &c_error_dialog_guid, flags, nullptr, messages, 2, 0);
}

intptr_t ShowErrorMessage(const exception& ex) {
	ShowErrorMessage(ex.what(), true);
	return 0;
}

#pragma FAR_EXPORT(SetStartupInfoW)
extern "C" void WINAPI FarSetStartupInfoW(const struct PluginStartupInfo_V3& info) {
	if (info.StructSize >= sizeof(PluginStartupInfo_V3)
		&& info.FSF->StructSize >= sizeof(FarStandardFunctions_V3)) {
		Far = info;
		FarFsf = *info.FSF;
		Far.FSF = &FarFsf;
		_pAtlModule->Lock();
	}
}

#pragma FAR_EXPORT(ExitFARW)
extern "C" void WINAPI FarExitFARW(const ExitInfo& info) {
	_pAtlModule->Unlock();
}

#pragma FAR_EXPORT(GetPluginInfoW)
extern "C" void WINAPI FarGetPluginInfoW(PluginInfo& info) {
	info.StructSize = sizeof(PluginInfo); // Info structure size
	info.Flags = PF_PRELOAD;
	info.PluginMenu.Guids = PluginMenuGuids;
	info.PluginMenu.Strings = PluginMenuStrings;
	info.PluginMenu.Count = 0; // _countof(PluginMenuStrings);
}

#pragma FAR_EXPORT(CloseAnalyseW)
extern "C" void WINAPI FarCloseAnalyseW(const CloseAnalyseInfo& info) {
	delete static_cast<Volume*>(info.Handle);
}

#pragma FAR_EXPORT(ClosePanelW)
extern "C" void WINAPI FarClosePanelW(const ClosePanelInfo& info) {
	delete static_cast<Volume*>(info.hPanel);
}

#pragma FAR_EXPORT(OpenW)
extern "C" HANDLE WINAPI FarOpenW(const OpenInfo& info) {
	switch (info.OpenFrom) {
	case OPENFROM::OPEN_ANALYSE:
		return ((OpenAnalyseInfo*)info.Data)->Handle;
	default:
		if (*info.Guid == c_guidDefragment) {
			//!!!TODO
		}
	}
	return nullptr;
}

#pragma FAR_EXPORT(GetOpenPanelInfoW)
extern "C" void WINAPI FarGetOpenPanelInfoW(OpenPanelInfo& info) {
	info.StructSize = sizeof(OpenPanelInfo);
	auto& volume = *(Volume*)info.hPanel;
	info.PanelTitle = volume.Filename;
	info.CurDir = volume.CurDirName;
	info.FreeSize = volume.FreeSpace();
	info.Flags = OPIF_ADDDOTS | OPIF_USEFREESIZE;
}

#pragma FAR_EXPORT(GetFindDataW)
extern "C" intptr_t WINAPI FarGetFindDataW(GetFindDataInfo& info) {
	TRC(1, "");
	try {
		auto& volume = *(Volume*)info.hPanel;
		const auto& files = volume.Files;
		info.PanelItem = new PluginPanelItem[files.size()];
		memset(info.PanelItem, 0, sizeof(PluginPanelItem) * files.size());
		info.ItemsNumber = files.size();
		for (int i = 0; i < files.size(); ++i) {
			auto& file = files[i];
			auto& item = info.PanelItem[i];
			item.FileName = file.FileName;
			if (!file.AlternateFileName.empty())
				item.AlternateFileName = file.AlternateFileName;

			item.ChangeTime = item.LastAccessTime = item.LastWriteTime = item.CreationTime = file.CreationTime;
			if (file.LastWriteTime.Ticks)
				item.ChangeTime = item.LastWriteTime = file.LastWriteTime;
			if (file.LastAccessTime.Ticks)
				item.LastWriteTime = file.LastAccessTime;

			item.FileSize = file.Length;
			item.AllocationSize = file.AllocationSize ? file.AllocationSize : file.Length;
			item.NumberOfLinks = 1;

			item.FileAttributes =
				(file.IsArchive ? FILE_ATTRIBUTE_ARCHIVE : 0)
				| (file.Hidden ? FILE_ATTRIBUTE_HIDDEN : 0)
				| (file.ReadOnly ? FILE_ATTRIBUTE_READONLY : 0)
				| (file.IsDirectory ? FILE_ATTRIBUTE_DIRECTORY : 0);
		}
		return 1;
	} catch (exception&) {
		return 0;
	}
}

#pragma FAR_EXPORT(FreeFindDataW)
extern "C" void WINAPI FarFreeFindDataW(const FreeFindDataInfo& info) {
	TRC(1, "");
	if (info.StructSize >= sizeof(FreeFindDataInfo))
		delete[] (PluginPanelItem*)info.PanelItem;
}

#pragma FAR_EXPORT(SetDirectoryW)
extern "C" intptr_t WINAPI FarSetDirectoryW(const SetDirectoryInfo& info) {
	TRC(1, "");
	if (info.StructSize < sizeof(SetDirectoryInfo))
		return 0;
	try {
		auto& volume = *(Volume*)info.hPanel;
		volume.ChangeDirectory(info.Dir);
		return 1;
	} catch (exception& ex) {
		return ShowErrorMessage(ex);
	}
}

#pragma FAR_EXPORT(MakeDirectoryW)
extern "C" intptr_t WINAPI FarMakeDirectoryW(const MakeDirectoryInfo& info) {
	TRC(1, "");
	if (info.StructSize < sizeof(MakeDirectoryInfo))
		return 0;
	try {
		((Volume*)info.hPanel)->MakeDirectory(info.Name);
		return 1;
	} catch (exception& ex) {
		return ShowErrorMessage(ex);
	}
}

//#pragma FAR_EXPORT(DeleteFilesW)
extern "C" intptr_t WINAPI FarDeleteFilesW(const DeleteFilesInfo& info) {
	if (info.StructSize < sizeof(DeleteFilesInfo))
		return 0;
	auto& volume = *(Volume*)info.hPanel;
	try {
		bool bShowDialog = !(info.OpMode & (OPM_SILENT | OPM_FIND | OPM_VIEW | OPM_EDIT | OPM_QUICKVIEW));
		if (bShowDialog) {
			String msg = "Delete files?";
			const wchar_t* messages[2] = { L"Confirmation", msg };
			if (Far.Message(&c_guidFsPlugin, &c_delete_files_dialog_guid, FMSG_MB_OKCANCEL | FMSG_ALLINONE, nullptr, messages, size(messages), 0))
				return -1;
		}

		for (size_t i = 0; i < info.ItemsNumber; ++i)
			volume.RemoveFile(info.PanelItem[i].FileName);
		volume.Flush();
		return 1;
	} catch (exception& ex) {
		return ShowErrorMessage(ex);
	}
}

extern "C" void *g_pfnDeleteFilesW = FarDeleteFilesW;
extern "C" int	__stdcall TotalDeleteFilesW(WCHAR* PackedFile, WCHAR* DeleteList);

#if UCFG_PLATFORM_IX86

#pragma comment(linker, "/export:DeleteFilesW=_DeleteFilesProxy@0")
extern "C" __declspec(naked) void __stdcall DeleteFilesProxy() {
	__asm jmp g_pfnDeleteFilesW
}

#else

#pragma comment(linker, "/export:DeleteFilesW=DeleteFilesProxy")
extern "C" intptr_t __stdcall DeleteFilesProxy(void *p0, void *p1) {
	if (g_pfnDeleteFilesW == FarDeleteFilesW)
		return FarDeleteFilesW(*(DeleteFilesInfo*)p0);
	else
		return TotalDeleteFilesW((WCHAR*)p0, (WCHAR*)p1);
}

#endif // UCFG_PLATFORM_IX86

static const Guid c_guidCopyFiles = Guid::NewGuid(); //!!!?

#pragma FAR_EXPORT(PutFilesW)
extern "C" intptr_t WINAPI FarPutFilesW(const PutFilesInfo& info) {
	if (info.StructSize < sizeof(PutFilesInfo))
		return 0;
	auto& volume = *(Volume*)info.hPanel;
	try {
		/*
		bool bShowDialog = !(info.OpMode & (OPM_SILENT | OPM_FIND | OPM_VIEW | OPM_EDIT | OPM_QUICKVIEW));
		if (bShowDialog) {
			String msg = "Copy/Move files?";
			const wchar_t* messages[2] = { L"Confirmation", msg };
			if (Far.Message(&c_guidFsPlugin, &c_guidCopyFiles, FMSG_MB_OKCANCEL, nullptr, messages, size(messages), 0))
				return -1;
		}
		*/
		for (size_t i = 0; i < info.ItemsNumber; ++i) {
			auto& item = info.PanelItem[i];
			wchar_t srcPath[_MAX_PATH];
			wcscpy(srcPath, info.SrcPath);
			Far.FSF->AddEndSlash(srcPath);
			wcscat(srcPath, item.FileName);
			FileStream ifs(srcPath, FileMode::Open, FileAccess::Read);
			if (info.OpMode & OPM_EDIT)
				volume.ModifyFile(item.FileName, item.FileSize, ifs, item.CreationTime);
			else
				volume.AddFile(item.FileName, item.FileSize, ifs, item.CreationTime);
		}
		volume.Flush();
		return 1;
	} catch (Exception& ex) {
		if (ex.code() == errc::operation_canceled)
			return -1;
		throw;
	} catch (exception& ex) {
		return ShowErrorMessage(ex);
	}
}

vector<IVolumeFactory*>& IVolumeFactory::RegisteredFactories() {
	static vector<IVolumeFactory*> factories;
	return factories;
}

IVolumeFactory::IVolumeFactory() {
	RegisteredFactories().push_back(this);
}

static const Guid c_guidChoose = Guid::NewGuid(); //!!!?

static class FarVolumeCallback : public IVolumeCallback
{
	bool AskYesOrNo(RCString msg) override {
		const wchar_t* messages[2] = { L"Choose", msg };
		auto rc = Far.Message(&c_guidFsPlugin, &c_guidChoose, FMSG_MB_YESNOCANCEL, nullptr, messages, 2, 0);
		switch (rc) {
		case -1:
		case 2:
			Throw(errc::operation_canceled);
		case 0:
			return true;
		}
		return false;
	}
} s_farVolumeCallback;

#pragma FAR_EXPORT(AnalyseW)
extern "C" HANDLE WINAPI FarAnalyseW(const AnalyseInfo& info) {
	if (auto factory = IVolumeFactory::FindBestFactory(Span((const uint8_t*)info.Buffer, info.BufferSize))) {
		auto volume = factory->CreateInstance();
		try {
			volume->Callback = &s_farVolumeCallback;
			volume->Init(info.FileName);
			return volume.release();
		} catch (exception& ex) {
			ShowErrorMessage(ex);
		}
	}
	return nullptr;
}

#pragma FAR_EXPORT(GetFilesW)
extern "C" intptr_t WINAPI FarGetFilesW(GetFilesInfo& info) {
	if (info.StructSize < sizeof(GetFilesInfo))
		return 0;
	try {
		bool bShowDialog = !(info.OpMode & (OPM_SILENT | OPM_FIND | OPM_VIEW | OPM_EDIT | OPM_QUICKVIEW));
		s_farVolumeCallback.Interactive = bShowDialog;
		if (bShowDialog) {
			String msg = "Copy/Move files?";
			const wchar_t* messages[2] = { L"Confirmation", msg };
			if (Far.Message(&c_guidFsPlugin, &c_delete_files_dialog_guid, FMSG_MB_OKCANCEL, nullptr, messages, size(messages), 0))
				return -1;
		}

		auto& volume = *(Volume*)info.hPanel;
		const auto& files = volume.Files;
		bool bReadOnly = true;
		for (size_t i = 0; i < info.ItemsNumber; ++i) {
			auto& item = info.PanelItem[i];
			for (auto& file : files) {
				if (file.FileName == item.FileName) {
					String dstFilename = item.FileName;
					dstFilename.Replace("/", "_");
					dstFilename.Replace("\\", "_");
					path dstPath = path(info.DestPath) / dstFilename;
					{
						FileStream ofs(dstPath, FileMode::CreateNew, FileAccess::Write);
						volume.CopyFileTo(file, ofs);
					}
					/*!!!R
					FileSystemInfo fileInfo(path(destPath), false);
					fileInfo.CreationTime = file.CreationTime;
					fileInfo.LastWriteTime = file.CreationTime;
					*/
					if (info.Move) {
						volume.RemoveFile(item.FileName);
						bReadOnly = false;
					}
					break;
				}
			}
		}
		if (!bReadOnly)
			volume.Flush();
		return 1;
	} catch (Exception& ex) {
		if (ex.code() == errc::operation_canceled)
			return -1;
		throw;
	} catch (exception& ex) {
		return ShowErrorMessage(ex);
	}
}
