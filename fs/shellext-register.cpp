#include "pch.h"

#include <el/libext/win32/ext-win.h>

#include <cguid.h>
#include <ShlGuid.h>
#include <shlobj.h>
#include <shellapi.h>

//#undef _ATL_USE_WINAPI_FAMILY_DESKTOP_APP
#include <atlbase.h>
#include <atlcom.h>
using namespace ATL;

using namespace Ext::Ole;
using namespace Ext::Win;

#include "volume.h"

// Windows Shell extension to mount Volume file

using namespace std;
using namespace std::chrono;
using namespace std::filesystem;
using namespace Ext;

CDllServer* Ext::CDllServer::I;

const Guid
	GuidPreviewHandler("{6d2b5079-2f0b-48dd-ab7f-97cec514d30b}")
	, GuidPreviewHandlerWow64("{534A1E02-D58F-44f0-B58B-36CBED287C7C}");


namespace U::VolumeShell {


#if UCFG_TRC
static bool InitTrace() {
	auto envLogDir = Environment::GetEnvironmentVariable("U_LOGDIR");
	path logPath = path(!!envLogDir ? path(envLogDir.c_wstr()) : temp_directory_path())
		/ path(String("rt11fs_" + to_string(::GetCurrentProcessId()) + ".log").c_wstr());
	Trace::SetOStream(new TraceStream(logPath, true));
	Trace::s_nLevel = 0x3F;
	//CStackTrace::Use = true;
	return true;
}
static bool s_bInitTrace = InitTrace();
#endif // UCFG_TRC

class CRt11FsModule : public CAtlDllModuleT<CRt11FsModule> {
public:
	CRt11FsModule() {
		TRC(1, "");
	}

	~CRt11FsModule() {
		TRC(1, "");
	}
} _AtlModule;

STDAPI DllCanUnloadNow() {
	TRC(1, "LockCount: " << _AtlModule.GetLockCount());
	return _AtlModule.DllCanUnloadNow();
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
	TRC(1, "CLSID: " << rclsid << ", IID: " << riid);
	return _AtlModule.DllGetClassObject(rclsid, riid, ppv);
}

STDAPI DllRegisterServer() {
//	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	try {
		return CDllServer::I->OnRegister();
	} catch (RCExc ex) {
		return HResultInCatch(ex);
	}
}

STDAPI DllUnregisterServer() {
//	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	try {
		return CDllServer::I->OnUnregister();
	} catch (RCExc ex) {
		return HResultInCatch(ex);
	}
}

const Guid CLSID_Rt11Fs = __uuidof(FsShellExt);
static const String sClsid = CLSID_Rt11Fs.ToString("B");
static const String s_ClassName = "DSKView";
static const String s_ClsidRegistryKeyName = "CLSID\\" + sClsid;

static const String s_approvedRegistryKeyName = "Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved";
static const String s_previewHandlersRegistryKeyName = "Software\\Microsoft\\Windows\\CurrentVersion\\PreviewHadlers";

static const String PluginFileExt = ".dsk";
static const string s_description = UCFG_MANUFACTURER " " VER_FILEDESCRIPTION_STR;
//static const String PluginClassName = UCFG_MANUFACTURER".RT11FS";

static struct VolumeShellDllServer : public CDllServer {
	HRESULT OnRegister() METHOD_BEGIN {
		String existingClassRef = RegistryKey(HKEY_CLASSES_ROOT, PluginFileExt).TryQueryValue(nullptr, nullptr);
		if (existingClassRef == nullptr || existingClassRef == s_ClassName) {
			HMODULE hModuleThis = 0;
			GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCTSTR)&_AtlModule, &hModuleThis);
			TCHAR szModule[_MAX_PATH];
			Win32Check(::GetModuleFileName(hModuleThis, szModule, _MAX_PATH));

			RegistryKey(HKEY_CLASSES_ROOT, PluginFileExt).SetValue(nullptr, s_ClassName);

			RegistryKey keyDskView(HKEY_CLASSES_ROOT, s_ClassName);
			keyDskView.SetValue(nullptr, "DSK File");
			RegistryKey(keyDskView, "CLSID").SetValue(nullptr, sClsid);

			RegistryKey keyClsid(HKEY_CLASSES_ROOT, s_ClsidRegistryKeyName);

			RegistryKey keyInprosServer(keyClsid, "InprocServer32");
			keyInprosServer.SetValue(nullptr, szModule);
			keyInprosServer.SetValue("ThreadingModel", "Apartment");

			BOOL bIsWow64;
			Win32Check(::IsWow64Process(::GetCurrentProcess(), &bIsWow64));
			keyClsid.SetValue("AppId", (bIsWow64 ? GuidPreviewHandlerWow64 : GuidPreviewHandler).ToString("B"));

			keyClsid.SetValue(nullptr, UCFG_MANUFACTURER " " VER_PRODUCTNAME_STR);
			keyClsid.SetValue("DisplayName", UCFG_MANUFACTURER " " VER_PRODUCTNAME_STR);
			keyClsid.SetValue("Icon", "shell32.dll,7");  // Floppy Icon

			RegistryKey(keyClsid, "DefaultIcon").SetValue(nullptr, "shell32.dll,7");  // Floppy Icon
			RegistryKey(keyClsid, "Implemented Categories\\" + Guid(CATID_BrowsableShellExt).ToString("B")).SetValue(nullptr, "Browsable Shell Extension");

			RegistryKey(keyClsid, "Shell\\Open").SetValue(nullptr, "Mount RT-11 Volume");

			RegistryKey(keyClsid, "Shell\\explore\\Command").SetValue(nullptr, "Explorer.exe /idlist,%I,%L");

			RegistryKey keyCommand(keyClsid, "Shell\\open\\Command");
			keyCommand.SetValue(nullptr, CRegistryValue("%SystemRoot%\\Explorer.exe /idlist,%I,%L", true));
//			keyCommand.SetValue("DelegateExecute", sClsid);

			// Make the class its own PreviewHandler
			RegistryKey(keyClsid, "ShellEx\\" + Guid(__uuidof(IPreviewHandler)).ToString("B")).SetValue(nullptr, sClsid);
			RegistryKey(HKEY_LOCAL_MACHINE, s_previewHandlersRegistryKeyName).SetValue(sClsid, s_description + " Preview Handler");

			DWORD attrs = SFGAO_DROPTARGET
				| SFGAO_HASPROPSHEET
				| SFGAO_STORAGEANCESTOR
				| SFGAO_FILESYSANCESTOR
				| SFGAO_FILESYSTEM
				| SFGAO_HASSUBFOLDER
				| SFGAO_FOLDER
				| SFGAO_BROWSABLE
				// | SFGAO_CANCOPY
				// | SFGAO_CANDELETE
				// SFGAO_UNDOCUMENTED_80
				;
			RegistryKey keyShellFolder(keyClsid, "ShellFolder");
			keyShellFolder.SetValue("Attributes", attrs);
			keyShellFolder.SetValue("PinToNameSpaceTree", "");

			RegistryKey(HKEY_LOCAL_MACHINE, s_approvedRegistryKeyName).SetValue(sClsid, s_description);
		}
	} METHOD_END

	HRESULT OnUnregister() METHOD_BEGIN {
		RegistryKey keyClassesRoot(HKEY_CLASSES_ROOT);
		if (RegistryKey(HKEY_CLASSES_ROOT, PluginFileExt).TryQueryValue(nullptr, nullptr) == s_ClassName) {
			keyClassesRoot.DeleteSubKey(PluginFileExt);
		}
		RegistryKey(HKEY_LOCAL_MACHINE, s_approvedRegistryKeyName).DeleteValue(sClsid, false);
		RegistryKey(HKEY_LOCAL_MACHINE, s_previewHandlersRegistryKeyName).DeleteValue(sClsid, false);
		keyClassesRoot.DeleteSubKeyTree(s_ClsidRegistryKeyName);
		keyClassesRoot.DeleteSubKeyTree(s_ClassName);
	} METHOD_END
} s_dllServer;

} // U::VolumeShell


