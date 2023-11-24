// Copyright(c) 2023 Ufasoft  http://ufasoft.com  mailto:support@ufasoft.com,  Sergey Pavlov  mailto:dev@ufasoft.com
//
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Derived from AdfView code http://www.viksoe.dk/code/adfview.htm, Written by Bjarke Viksoe (bjarke@viksoe.dk)

// Windows Shell extension to mount Volume file

#include "pch.h"

#include <el/libext/win32/ext-win.h>
#include <el/gui/image.h>

#include <cguid.h>
#include <ShlGuid.h>
#include <shlobj.h>
#include <shellapi.h>
#include <StgProp.h>

//#undef _ATL_USE_WINAPI_FAMILY_DESKTOP_APP
#include <atlbase.h>
#include <atlcom.h>
using namespace ATL;

using namespace Ext::Gui;
using namespace Ext::Ole;
using namespace Ext::Win;

#include <afxres.h>
#include "resource.h"
#include "volume.h"
using namespace U::FS;

#include "ShellExt.h"


using namespace std;
using namespace std::chrono;
using namespace std::filesystem;
using namespace Ext;

namespace U::VolumeShell {

const int MAXNAMELEN = 10; //!!! for RT-11 only

#define MK_SILENT 0x80000000

enum {
	FILEOP_CANCEL = 0x00000001,
	FILEOP_YESTOALL = 0x00000002,
	FILEOP_UNIQUENAME = 0x00000004,
	FILEOP_DONTTRUNCATE = 0x00000008,
	FILEOP_SILENT = 0x00000010,
};

// Constants defining where in the shared ImageList control
// the icons are placed.
enum ICONINDEX {
	ICON_INDEX_FOLDER,
	ICON_INDEX_FOLDER_OPEN,
	ICON_INDEX_FILE,
	ICON_INDEX_LAST
};

static const String
	ColumnName = "Name"
	, ColumnSize = "Size"
	, ColumnTimestampModified = "Date Modified"
	, ColumnTimestampCreated = "Date Created"
	, ColumnTimestampAccessed = "Date Accessed"
	, ColumnType = "Type";

static const struct ColumnInfo {
	String Name;
	HorizontalAlignment HorizontalAlignment;
	int Width;
	SHCOLSTATEF Flags;        // Shell sort/setup flags
	Guid FormatId;
	DWORD Pid;
} s_columns[] = {
	{ ColumnName				, HorizontalAlignment::Left	, 300, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT		, FMTID_Storage, PID_STG_NAME }
	, { ColumnSize				, HorizontalAlignment::Right, 100, SHCOLSTATE_TYPE_INT | SHCOLSTATE_ONBYDEFAULT		, FMTID_Storage, PID_STG_SIZE }
	, { ColumnTimestampCreated	, HorizontalAlignment::Left	, 300, SHCOLSTATE_TYPE_DATE | SHCOLSTATE_ONBYDEFAULT	, FMTID_Storage, PID_STG_CREATETIME }
//	, { ColumnTimestampModified	, HorizontalAlignment::Left	, 300, SHCOLSTATE_TYPE_DATE | SHCOLSTATE_ONBYDEFAULT	, FMTID_Storage, PID_STG_WRITETIME }
//	, { ColumnTimestampAccessed	, HorizontalAlignment::Left	, 300, SHCOLSTATE_TYPE_DATE | SHCOLSTATE_ONBYDEFAULT	, FMTID_Storage, PID_STG_ACCESSTIME }
	, { ColumnType				, HorizontalAlignment::Left	, 160, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT		, FMTID_Storage, PID_STG_STORAGETYPE}
};

static const GUID SDefined_Unknown3 = // {CAD9AE9F-56E2-40F1-AFB6-3813E320DCFD}
{ 0xCAD9AE9F, 0x56E2, 0x40F1, {0xAF, 0xB6, 0x38, 0x13, 0xE3, 0x20, 0xDC, 0xFD} };

ShellPath CreateItem(const String& name) {
	char buf[sizeof(SHITEMID) + MAX_PATH + sizeof(SHITEMID)] = { 0 };
	SHITEMID* itemId = (SHITEMID*)buf;
	itemId->cb = sizeof(buf) - sizeof(SHITEMID);
	strcpy((char*)&(itemId->abID[0]), name);
	return ShellPath(LPCITEMIDLIST(itemId));
}

class ATL_NO_VTABLE CDropSource
	: public CComObjectRootEx<CComSingleThreadModel>
	, public IDropSource
{
public:
	void Init() {}

	// IDropSource
	STDMETHODIMP QueryContinueDrag(BOOL bEsc, DWORD dwKeyState) METHOD_BEGIN {
		return bEsc ? DRAGDROP_S_CANCEL
			: dwKeyState & MK_LBUTTON ? S_OK
			: DRAGDROP_S_DROP;
	} METHOD_END

	STDMETHODIMP GiveFeedback(DWORD) METHOD_BEGIN {
		return DRAGDROP_S_USEDEFAULTCURSORS;
	} METHOD_END

	BEGIN_COM_MAP(CDropSource)
		COM_INTERFACE_ENTRY_IID(IID_IDropSource, IDropSource)
	END_COM_MAP()
};

static HINSTANCE s_HInstance;
static String s_ModulePath;

class Images {
public:
	ImageList imagesSmall, imagesLarge, imagesHuge;
	HICON smallIndexFileIcon;

	// Avoid static dtors of ImageList objects
	static Images& Get() {
		static Images* s_pImages;
		if (!s_pImages) s_pImages = new Images();
		return *s_pImages;
	}
private:
	Images() {
		imagesSmall.m_imageSize = Icon::GetShellIconSize(SHGFI_SMALLICON);
		imagesSmall.ColorDepth = ColorDepth::Depth32Bit;
		imagesLarge.m_imageSize = Icon::GetShellIconSize(SHGFI_LARGEICON);
		imagesLarge.ColorDepth = ColorDepth::Depth32Bit;
		imagesHuge.m_imageSize = Size(256, 256);
		imagesHuge.ColorDepth = ColorDepth::Depth32Bit;

		String windowsDirectory = System.WindowsDirectory;

		imagesSmall.Add(Icon::GetShellIcon(windowsDirectory, SHGFI_ICON | SHGFI_SHELLICONSIZE | SHGFI_SMALLICON));
		imagesLarge.Add(Icon::GetShellIcon(windowsDirectory, SHGFI_ICON | SHGFI_SHELLICONSIZE));
		imagesHuge.Add(Icon::GetJumboIcon(windowsDirectory, SHGFI_ICON | SHGFI_SHELLICONSIZE));

		imagesSmall.Add(Icon::GetShellIcon(windowsDirectory, SHGFI_ICON | SHGFI_SHELLICONSIZE | SHGFI_OPENICON | SHGFI_SMALLICON));
		imagesLarge.Add(Icon::GetShellIcon(windowsDirectory, SHGFI_ICON | SHGFI_SHELLICONSIZE | SHGFI_OPENICON));
		imagesHuge.Add(Icon::GetJumboIcon(windowsDirectory, SHGFI_ICON | SHGFI_SHELLICONSIZE | SHGFI_OPENICON));

		SHFILEINFO sfi = { 0 };
		Win32Check(::SHGetFileInfo(s_ModulePath, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SHELLICONSIZE | SHGFI_SMALLICON));
		smallIndexFileIcon = sfi.hIcon;
		imagesSmall.Add(Icon::GetShellIcon(s_ModulePath, SHGFI_ICON | SHGFI_SHELLICONSIZE | SHGFI_SMALLICON));

		imagesLarge.Add(Icon::GetShellIcon(s_ModulePath, SHGFI_ICON | SHGFI_SHELLICONSIZE));
		imagesHuge.Add(Icon::GetJumboIcon(s_ModulePath, SHGFI_ICON | SHGFI_SHELLICONSIZE));
	}
};

static struct InitStatics {
	InitStatics() {
		GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCTSTR)&s_HInstance, &s_HInstance);
		TCHAR szModule[_MAX_PATH];
		Win32Check(::GetModuleFileName(s_HInstance, szModule, _MAX_PATH));
		s_ModulePath = szModule;

	}
} s_initStatics;

class ATL_NO_VTABLE CExtractIcon
	: public CComObjectRootEx<CComSingleThreadModel>
	, public IExtractIcon
{
public:
	BEGIN_COM_MAP(CExtractIcon)
		COM_INTERFACE_ENTRY_IID(IID_IExtractIcon, IExtractIcon)
	END_COM_MAP()

	ShellPath path;
	bool isFolder;

	void Init(const ShellPath& path, bool isFolder) {
		this->path = path;
		this->isFolder = isFolder;
	}

	// IExtractIcon

	STDMETHODIMP GetIconLocation(UINT uFlags, LPWSTR pszIconFile, UINT cchMax, LPINT piIndex, LPUINT puFlags) METHOD_BEGIN {
		*piIndex = !isFolder ? ICON_INDEX_FILE
			: uFlags & GIL_OPENICON ? ICON_INDEX_FOLDER_OPEN
			: ICON_INDEX_FOLDER;
		if (pszIconFile)
			::wnsprintfW(pszIconFile, cchMax, L"FS11_%d_%u", *piIndex, uFlags);
		*puFlags = GIL_NOTFILENAME | GIL_DONTCACHE;
	} METHOD_END

	STDMETHODIMP Extract(LPCWSTR, UINT nIconIndex, HICON* phIconLarge, HICON* phIconSmall, UINT nIconSize) METHOD_BEGIN {
		TRC(1, "nIconIndex: " << nIconIndex << ", nIconSize: " << hex << nIconSize);
		auto& images = Images::Get();
		ImageList& large = LOWORD(nIconSize) >= 256 ? images.imagesHuge : images.imagesLarge;
		if (phIconLarge)
			*phIconLarge = large.GetIcon(nIconIndex, ILD_TRANSPARENT).Detach();
		if (phIconSmall) {
			*phIconSmall = nIconIndex == ICON_INDEX_FILE
				? ::CopyIcon(images.smallIndexFileIcon)
				: images.imagesSmall.GetIcon(nIconIndex, ILD_TRANSPARENT).Detach();
		}
	} METHOD_END
};


ATL_NO_VTABLE
class FsShellExt
	: public CComObjectRootEx<CComMultiThreadModel>
	, public CComCoClass<FsShellExt, &__uuidof(FsShellExt)>
	, public IShellExt
	, public IShellExtInit
	, public IContextMenu
	, public IInitializeCommand
	, public IInitializeWithStream
	, public IExecuteCommand
	, public IExecuteCommandApplicationHostEnvironment
	, public IForegroundTransfer
	, public IObjectWithSelection
	, public IObjectWithSite
	, public IPersistFolder3
	, public IShellFolder2
	, public IItemNameLimits
	, public IShellFolderViewCB
	, public IFolderViewSettings
	, public IPreviewHandler
	, public IStorage
{
public:
	const int WM_CHANGENOTIFY = WM_APP + 400;

	FsShellExt* m_pFolder = this;
	String ClassName = "RT11View";
	String Description = "RT-11 Disk Volume Image";
	CComPtr<IDataObject> iDataObject;
private:
	ShellPath _pathRoot, _pathMonitor, _pathPath;
	String _path;

	unique_ptr<Volume> volume_;

	Volume& get_Volume() {
		if (!volume_) {
			volume_ = IVolumeFactory::Mount(_path);
		}
		return *volume_.get();
	}
	DEFPROP_GET(Volume&, Volume);

	vector<ptr<DirEntry>> dirEntries;

	CUnkPtr _site;
	CComPtr<IShellItemArray> _selection;
	CComQIPtr<IServiceProvider> _serviceProvider;

	POINT _ptView;
	HWND _hwnd;

public:
	FsShellExt() {
		TRC(1, "");

#if !UCFG_USE_SHELL_DEFVIEW
		_acceleratorTable = AcceleratorTable(s_HInstance, CResID((UINT)IDR_ACCELERATOR));
#endif
	}

	~FsShellExt() {
		TRC(1, "this: 0x" << this);

		if (Clipboard::IsCurrent(iDataObject))
			Clipboard::Flush();

		if (volume_) { TRC(1, "Volume was opened") }
	}

	// IShellExtInit
	STDMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject* pdtobj, HKEY hkeyProgID)	METHOD_BEGIN {
		FORMATETC ftc = { CF_HDROP, 0, DVASPECT_CONTENT, 0, TYMED_HGLOBAL };
		StorageMedium stg;
		OleCheck(pdtobj->GetData(&ftc, &stg));
		GlobalLocker lock(stg.hGlobal);
		HDROP hDrop = lock.Cast<HDROP>();
		UINT uNumFiles = Win32Check(::DragQueryFile(hDrop, 0xFFFFFFFF, 0, 0));
		TCHAR szPath[MAX_PATH];
		Win32Check(::DragQueryFile(hDrop, 0, szPath, MAX_PATH));
		_path = szPath;
		TRC(1, "Path: " << _path);
		try {
			IVolumeFactory::Mount(_path);
		} catch (Exception& e) {
			if (e.code() == error_code(ERROR_UNRECOGNIZED_VOLUME, hresult_category()))
				return S_FALSE;
			throw;
		}
	} METHOD_END

	// IContextMenu
	STDMETHODIMP QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) noexcept;

	STDMETHODIMP GetCommandString(UINT_PTR idCmd, UINT uType, UINT* pReserved, CHAR* pszName, UINT cchMax) METHOD_BEGIN {
		if (0 != idCmd || !(uType & GCS_HELPTEXT)) {
			Throw(E_INVALIDARG);
		}
		String text = "RT-11 File System volume";
		TRC(1, "Getting command string for idCmd=" << idCmd);
		if (uType & GCS_UNICODE) {
			lstrcpynW((LPWSTR)pszName, text, cchMax);
		} else
			lstrcpynA(pszName, text, cchMax);
	} METHOD_END

	STDMETHODIMP InvokeCommand(CMINVOKECOMMANDINFO* pici) METHOD_BEGIN {
		if ((intptr_t)pici->lpVerb != 0)
			Throw(E_INVALIDARG);
		TRC(3, "Verb: " << (HIWORD(pici->lpVerb) ? pici->lpVerb : Convert::ToString(LOWORD(pici->lpVerb))));
		volume_ = IVolumeFactory::Mount(_path);
	} METHOD_END

	// IInitializeCommand
	STDMETHODIMP Initialize(LPCWSTR pszCommandName, IPropertyBag* ppb) METHOD_BEGIN {
		TRC(1, "IInitializeCommand::Initialize: CommandName: " << pszCommandName);
	} METHOD_END

	// IInitializeWithStream
	STDMETHODIMP Initialize(IStream* pstream, DWORD grfMode) METHOD_BEGIN {
	} METHOD_END

	// IExecuteCommand
	STDMETHODIMP SetKeyState(DWORD grfKeyState) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP SetParameters(LPCWSTR pszParameters) METHOD_BEGIN {
		TRC(1, "Parameters: " << pszParameters);
	} METHOD_END

	STDMETHODIMP SetPosition(POINT pt) METHOD_BEGIN {
		TRC(1, "Position: (" << pt.x << ", " << pt.y << ")");
		_ptView = pt;
	} METHOD_END

	STDMETHODIMP SetShowWindow(int nShow) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP SetNoShowUI(BOOL fNoShowUI) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP SetDirectory(LPCWSTR pszDirectory) METHOD_BEGIN {
		TRC(1, "Directory: " << pszDirectory);
	} METHOD_END

	String _absoluteFilepath;

	// IObjectWithSelection
	STDMETHODIMP SetSelection(IShellItemArray* psia) METHOD_BEGIN {
		_selection = psia;

		DWORD count;
		OleCheck(_selection->GetCount(&count));
		TRC(1, "Items: ");
		for (DWORD i = 0; i < count; ++i) {
			CComPtr<IShellItem> item;
			OleCheck(_selection->GetItemAt(i, &item));
			COleString name;
			OleCheck(item->GetDisplayName(SIGDN::SIGDN_FILESYSPATH, &name));
			_absoluteFilepath = (String)name;
			TRC(1, "  " << _absoluteFilepath);
		}
	} METHOD_END

	STDMETHODIMP STDMETHODCALLTYPE GetSelection(REFIID riid, void** ppv) METHOD_BEGIN {
		return _selection
			? _selection->QueryInterface(riid, ppv)
			: E_FAIL;
	} METHOD_END

	// IObjectWithSite
	STDMETHODIMP SetSite(IUnknown* pUnkSite) METHOD_BEGIN {
		if (!pUnkSite) { TRC(1, "Site = NULL") }
		_site = pUnkSite;
		_serviceProvider = _site;
		TRC(1, (_serviceProvider ? "Non-NULL _serviceProvider" : "NULL _serviceProvider"));
	} METHOD_END

	STDMETHODIMP GetSite(REFIID riid, void** ppvSite) METHOD_BEGIN {
		return _site
			? _site->QueryInterface(riid, ppvSite)
			: E_FAIL;
	} METHOD_END

	STDMETHODIMP Execute() METHOD_BEGIN {
		SFV_CREATE param = { sizeof(SFV_CREATE), this };
		CComPtr<IShellView> shellView;
		OleCheck(::SHCreateShellFolderView(&param, &shellView));

		CComPtr<IShellBrowser> shellBrowser;
		OleCheck(_serviceProvider->QueryService(SID_SShellBrowser, &shellBrowser));

		TRC(1, (shellBrowser ? "Non-NULL shellBrowser" : "NULL shellBrowser"));

		CComQIPtr<IShellView2> shellView2 = shellView;
		FOLDERSETTINGS folderSettings = { (UINT)FOLDERVIEWMODE::FVM_DETAILS, 0 };
		RECT rcView = { _ptView.x, _ptView.y, _ptView.x + 100, _ptView.y + 100 };
		SV2CVW2_PARAMS paramSV2{ sizeof(SV2CVW2_PARAMS), 0, &folderSettings, shellBrowser, &rcView };
		TRC(1, "Calling CreateViewWindow()");
//		OleCheck(shellView2->CreateViewWindow2(&paramSV2));
		OleCheck(shellView->CreateViewWindow(0, &folderSettings, shellBrowser, &rcView, &_hwnd));
	} METHOD_END

	// IExecuteCommandApplicationHostEnvironment
	STDMETHODIMP GetValue(AHE_TYPE* pahe) METHOD_BEGIN {
		*pahe = AHE_DESKTOP;
	} METHOD_END

	// IForegroundTransfer
	STDMETHODIMP AllowForegroundTransfer(void* lpvReserved) METHOD_BEGIN {
	} METHOD_END

	// IPersist
	STDMETHODIMP GetClassID(CLSID* pClassID) METHOD_BEGIN {
		*pClassID = CLSID_Rt11Fs;
	} METHOD_END

	// IPersistFolder
	STDMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidl) METHOD_BEGIN {
		_pathRoot = pidl;
		_path = _pathRoot.ToString();
		TRC(1, "IPersistFolder::Initialize: " << _path);
		_pathMonitor = _pathRoot;
	} METHOD_END

	// IPersistFolder2
	STDMETHODIMP GetCurFolder(PIDLIST_ABSOLUTE* ppidl) METHOD_BEGIN {
		if (!ppidl) return E_INVALIDARG;
		*ppidl = (_pathRoot / _pathPath).Detach();
	} METHOD_END

	// IPersistFolder3
	STDMETHODIMP InitializeEx(IBindCtx* pbc, PCIDLIST_ABSOLUTE pidlRoot, const PERSIST_FOLDER_TARGET_INFO* ppfti) METHOD_BEGIN {
		return Initialize(pidlRoot);
	} METHOD_END

	STDMETHODIMP GetFolderTargetInfo(PERSIST_FOLDER_TARGET_INFO* ppfti) METHOD_BEGIN {
		ZeroStruct(*ppfti);
		Throw(E_NOTIMPL);
	} METHOD_END


	void CopyDroppedFile(Stream& streamFrom, RCString filenameTo, const DateTime& timestampCreation) {
		Volume.AddFile(filenameTo, streamFrom.Length, streamFrom, timestampCreation);
	}

	void CopyDroppedFile(RCString from, RCString to, DateTime timestampCreation = DateTime()) {
		if (!timestampCreation.Ticks)
			timestampCreation = FileSystemInfo(from, false).CreationTime;
		CopyDroppedFile(FileStream(from, FileMode::Open, FileAccess::Read), to, timestampCreation);
	}

	void MoveDroppedFile(RCString from, RCString to) {
		FileSystemInfo fsinfo(from, false);
		CopyDroppedFile(from, to, fsinfo.CreationTime);
	}

	void CreateDroppedDirectory(RCString name) {
		Volume.MakeDirectory(name);
	}


	void DoDropSync(IDataObject* iDataObject, DWORD& dropEffect, DWORD fileOpFlags) {
		StorageMedium stgmed;

		// Check for HDROP
		// Check for FILEDESCRIPTOR
		FORMATETC fe1 = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
		FORMATETC fe2 = { g_formatFileGroupDescriptor.Id, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
		HRESULT hr = S_OK;
		if (SUCCEEDED(iDataObject->GetData(&fe1, &stgmed))) {
			GlobalLocker gl(stgmed.hGlobal);
			auto hDrop = *gl.Cast<HDROP*>();
			UINT nFiles = Win32Check(::DragQueryFile(hDrop, (UINT)-1, NULL, 0));
			for (UINT i = 0; i < nFiles; ++i) {
				TCHAR szFileName[MAX_PATH] = { 0 };
				Win32Check(::DragQueryFile(hDrop, i, szFileName, size(szFileName)));	// Get filename...
				if (dropEffect == DROPEFFECT_MOVE)
					MoveDroppedFile(szFileName, szFileName);
				else
					CopyDroppedFile(szFileName, szFileName);
			}
		} else if (SUCCEEDED(iDataObject->GetData(&fe2, &stgmed))) {
			GlobalLocker gl(stgmed.hGlobal);
			auto& groupDesc = *gl.Cast<FILEGROUPDESCRIPTOR*>();
			for (int i = 0; i < groupDesc.cItems; ++i) {
				const auto& fd = groupDesc.fgd[i];
				DWORD attrs = fd.dwFlags & FD_ATTRIBUTES ? fd.dwFileAttributes : FILE_ATTRIBUTE_NORMAL;
				if (attrs & FILE_ATTRIBUTE_DIRECTORY)
					CreateDroppedDirectory(fd.cFileName);
				else {
					FORMATETC fe = { g_formatFileContents.Id, NULL, DVASPECT_CONTENT, i, TYMED_ISTREAM | TYMED_HGLOBAL };
					StorageMedium stgmedContent;
					OleCheck(iDataObject->GetData(&fe, &stgmedContent));
					CComPtr<IStream> iStream;
					switch (stgmedContent.tymed) {
					case TYMED_HGLOBAL:
						OleCheck(::CreateStreamOnHGlobal(stgmedContent.hGlobal, FALSE, &iStream));
						break;
					case TYMED_ISTREAM:
						iStream = stgmedContent.pstm;
						break;
					default:
						Throw(DV_E_TYMED);
					}
					CopyDroppedFile(CIStream(iStream), fd.cFileName, fd.ftCreationTime);
				}
			}
		} else
			hr = E_FAIL;

		// Tell the DataObject if we succeeded. If we failed or perhaps skipped files (S_FALSE)
		// we should just pretend no drop happend.
		if (hr == S_OK) {
			SetDWORD(iDataObject, g_formatPerformedDropEffect.Id, dropEffect);
			SetDWORD(iDataObject, g_formatLogicalPerformedDropEffect.Id, dropEffect);
		} else {
			dropEffect = DROPEFFECT_NONE;
		}

		UpdateDir();
	}

	void DoDrop(IDataObject* iDataObject, DWORD dropEffect, DWORD fileOpFlags = 0) {
		CWaitCursor cursor;

		BOOL bAsync = FALSE;
		if (CComQIPtr<IDataObjectAsyncCapability> iAsyncCap = iDataObject) {
			OleCheck(iAsyncCap->GetAsyncMode(&bAsync));
			TRC(1, "Async operation supported");

			if (bAsync && SUCCEEDED(iAsyncCap->StartOperation(nullptr))) {
				//TODO
				return;
			}
		}
		DoDropSync(iDataObject, dropEffect, fileOpFlags);
	}

public:
	bool IsClipDataAvailable(IDataObject* pDataObject, bool bAllowSelf) const {
		if (!pDataObject)
			return false;
		FORMATETC fe = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
		bool bClipboardCopy = SUCCEEDED(pDataObject->QueryGetData(&fe));
		if (!bClipboardCopy) {
			FORMATETC fe = { g_formatFileGroupDescriptor.Id, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
			bClipboardCopy = SUCCEEDED(pDataObject->QueryGetData(&fe));
		}
		//!!!T		if (!bAllowSelf && bClipboardCopy) {
		//!!!T			bClipboardCopy = !IsDroppedPathSame(pDataObject);
				//!!!T}
		return bClipboardCopy;
	}

	// IShellFolder
	STDMETHODIMP ParseDisplayName(HWND hwnd, IBindCtx* pbc, LPWSTR pszDisplayName, ULONG* pchEaten, PIDLIST_RELATIVE* ppidl, ULONG* pdwAttributes) METHOD_BEGIN {
		TRC(1, "pszDisplayName: " << pszDisplayName);
	} METHOD_END

	STDMETHODIMP EnumObjects(HWND hwnd, SHCONTF grfFlags, IEnumIDList** ppenumIDList) METHOD_BEGIN {
		vector<ShellPath> shellPaths;
		for (auto e : Volume.GetFiles())
			shellPaths.push_back(ShellPath::FromString(e.FileName));
		CreateComInstance<CPidlEnum>(ppenumIDList, shellPaths);
	} METHOD_END

	STDMETHODIMP BindToObject(PCUIDLIST_RELATIVE pidl, IBindCtx* pbc, REFIID riid, void** ppv) METHOD_BEGIN {
		if (riid == IID_IStream) {
			TRC(1, "riid: IID_IStream");

			auto sPath = ShellPath(pidl).ToSimpleString();
			TRC(1, "sPath: " << sPath);

			unique_ptr<MemoryStream> pMS(new MemoryStream);
			auto& entry = *Volume.GetEntry(sPath);
			Volume.CopyFileTo(entry, *pMS);
			pMS->Position = 0;
			CreateComInstance<CComStream>((IStream**)ppv, pMS.release(), entry.FileName, entry.CreationTime);
			volume_.reset();
		} else {
			if (riid == IID_IShellFolder) {
				TRC(1, "riid: IID_IShellFolder");
				auto sPath = ShellPath(pidl).ToSimpleString();
				TRC(1, "sPath: " << sPath);
			} else if (riid == __uuidof(IPropertyStore)) {
				TRC(1, "riid: IID_IPropertyStore");
			} else if (riid == __uuidof(IPropertyStoreFactory)) {
				TRC(1, "riid: IID_IPropertyStoreFactory");
			} else if (riid == __uuidof(IPropertyStoreCache)) {
				TRC(1, "riid: IID_IPropertyStoreCache");
			} else {
				TRC(1, "riid: " << riid);
			}
			return QueryInterface(riid, ppv);	//!!!D
		}
	} METHOD_END

	STDMETHODIMP BindToStorage(PCUIDLIST_RELATIVE pidl, IBindCtx* pbc, REFIID riid, void** ppv) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP CompareIDs(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2) METHOD_BEGIN {
		ShellPath a(pidl1), b(pidl2);
		String as = a.ToSimpleString(), bs = b.ToSimpleString();
		auto col = lParam & 0xFF;
		int r;
		if (col == 0)
			r = as.compare(bs);
		else {
			strong_ordering cmp;
			auto& ae = *Volume.GetEntry(as);
			auto& be = *Volume.GetEntry(bs);
			switch (col) {
			case 1:
				cmp = ae.Length <=> be.Length;
				break;
			case 2:
				cmp = ae.CreationTime <=> be.CreationTime;
				break;
			}
			r = cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
		}
		TRC(1, "lParam: " << hex << lParam << " " << as << ", " << bs << "  Returns: " << r);
		return MAKE_SCODE(0, 0, (uint16_t)r);
	} METHOD_END

	STDMETHODIMP CreateViewObject(HWND hwndOwner, REFIID riid, void** ppv) noexcept;

	const DirEntry& GetDirEntry(LPCITEMIDLIST pidl) {
		auto filename = ShellPath(pidl).ToSimpleString();
		TRC(1, filename);
		return *Volume.GetEntry(filename);
	}

	STDMETHODIMP GetAttributesOf(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, SFGAOF* rgfInOut) METHOD_BEGIN {
		TRC(1, "cidl: " << cidl);
		for (int i = 0; i < cidl; ++i) {
			DWORD dwAttribs = SFGAO_CANDELETE | SFGAO_CANRENAME | SFGAO_CANCOPY | SFGAO_CANMOVE;
			auto& entry = GetDirEntry(apidl[i]);
			if (entry.IsDirectory)
				dwAttribs |= SFGAO_FOLDER | SFGAO_BROWSABLE | SFGAO_DROPTARGET;
			else
				dwAttribs |= SFGAO_STREAM;
			if (entry.ReadOnly)
				dwAttribs |= SFGAO_READONLY;

			*rgfInOut &= dwAttribs;
		}
	} METHOD_END

	STDMETHODIMP GetUIObjectOf(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, UINT* rgfReserved, void** ppRetVal) METHOD_BEGIN {
		if (riid == IID_IExtractIcon) {
			TRC(1, "riid: IID_IExtractIcon");
			if (cidl != 1) return E_INVALIDARG;
			ShellPath path(apidl[0]);
			CreateComInstance<CExtractIcon>((IExtractIcon**)ppRetVal, path, Volume.GetEntry(path.ToSimpleString())->IsDirectory);
			volume_.reset();
			return S_OK;
/*
		} else if (riid == IID_IContextMenu) {
			TRC(1, "riid: IID_IContextMenu");
			return QueryInterface(riid, ppRetVal);
*/
		} else if (riid == IID_IDataObject) {
			TRC(1, "riid: IID_IDataObject, cidl: " << cidl);
			CreateComInstance<CDataObject>((IDataObject**)ppRetVal, this, (IShellFolder*)this, &Volume, hwndOwner, apidl, cidl);
			return S_OK;
		}
		TRC(1, "riid: " << riid);
		return E_NOINTERFACE;
	} METHOD_END

	STDMETHODIMP GetDisplayNameOf(PCUITEMID_CHILD pidl, SHGDNF uFlags, STRRET* pName) METHOD_BEGIN {
		auto filename = ShellPath(pidl).ToSimpleString();
		TRC(1, filename << ", uFlags" << uFlags);
		pName->pOleStr = filename.AllocOleString();
		pName->uType = STRRET_WSTR;
	} METHOD_END

	STDMETHODIMP SetNameOf(HWND hwnd, PCUITEMID_CHILD pidl, LPCWSTR pszName, SHGDNF uFlags, PITEMID_CHILD* ppidlOut) METHOD_BEGIN {
	} METHOD_END

	// IShellFolder2
	STDMETHODIMP GetDefaultSearchGUID(GUID* pguid) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP EnumSearches(IEnumExtraSearch** ppenum) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP GetDefaultColumn(DWORD dwRes, ULONG* pSort, ULONG* pDisplay) METHOD_BEGIN {
		if (pSort)
			*pSort = 0;			// 0 - Name column
		if (pDisplay)
			*pDisplay = 0;
	} METHOD_END

	STDMETHODIMP GetDefaultColumnState(UINT iColumn, SHCOLSTATEF* pcsFlags) METHOD_BEGIN {
		TRC(1, "iColumn: " << iColumn);
		if (iColumn >= size(s_columns)) return E_INVALIDARG;
		*pcsFlags = s_columns[iColumn].Flags;
	} METHOD_END

	STDMETHODIMP GetDetailsEx(PCUITEMID_CHILD pidl, const SHCOLUMNID* pscid, VARIANT* pv) METHOD_BEGIN {
		auto filename = ShellPath(pidl).ToSimpleString();
		TRC(1, filename << ", pid: " << pscid->pid);
		CComVariant v;
		switch (pscid->pid) {
		case PID_STG_LASTCHANGEUSN:
			v = int64_t(rand());			//!!!T
			return v.Detach(pv);
		case PID_STG_CLASSID:
			v = CLSID_NULL;
			return v.Detach(pv);
		}
		auto& entry = GetDirEntry(pidl);
		switch (pscid->pid) {
		case PID_STG_STORAGETYPE:
			v = entry.IsDirectory ? L"Folder" : L"File";
			break;
		case PID_STG_NAME:
			v = filename.c_wstr();
			break;
		case PID_STG_SIZE:
			v = entry.Length;
			break;
		case PID_STG_CREATETIME:
			v = entry.CreationTime.ToOADate();
			v.vt = VT_DATE;
			break;
		case PID_STG_WRITETIME:
			v = entry.LastWriteTime.ToOADate();
			v.vt = VT_DATE;
			break;
		case PID_STG_ACCESSTIME:
			v = entry.LastAccessTime.ToOADate();
			v.vt = VT_DATE;
			break;
		default:
			return E_INVALIDARG;
		}
		volume_.reset();
		return v.Detach(pv);
	} METHOD_END

	STDMETHODIMP GetDetailsOf(PCUITEMID_CHILD pidl, UINT iColumn, SHELLDETAILS* psd) METHOD_BEGIN {
		TRC(1, "iColumn: " << iColumn);
		if (iColumn >= size(s_columns)) return E_FAIL;
		const auto& column = s_columns[iColumn];
		String s;
		if (pidl) {
			TRC(1, "pidl: " << pidl);
		} else {
			s = column.Name;
		}
		psd->fmt = (int)column.HorizontalAlignment;
		psd->cxChar = column.Width / 10;
		psd->str.pOleStr = s.AllocOleString();
		psd->str.uType = STRRET_WSTR;
	} METHOD_END

	STDMETHODIMP MapColumnToSCID(UINT iColumn, SHCOLUMNID* pscid) METHOD_BEGIN {
		TRC(1, "iColumn: " << iColumn);
		if (iColumn >= size(s_columns)) return E_INVALIDARG;
		auto& c = s_columns[iColumn];
		pscid->fmtid = c.FormatId;
		pscid->pid = c.Pid;
	} METHOD_END

	static LPWSTR AllocChars(const vector<wchar_t>& vec) {
		auto p = (WCHAR*)::CoTaskMemAlloc(sizeof(WCHAR) * vec.size());
		if (!p) Throw(E_OUTOFMEMORY);
		for (int i = vec.size(); i-- > 0;)
			p[i] = vec[i];
		return p;
	}

	// IItemNameLimits
	STDMETHODIMP GetValidCharacters(LPWSTR* ppwszValidChars, LPWSTR* ppwszInvalidChars) METHOD_BEGIN {
		auto pp = Volume.ValidInvalidFilenameChars();
		*ppwszValidChars = pp.first.empty() ? nullptr : AllocChars(pp.first);
		*ppwszInvalidChars = pp.second.empty() ? nullptr : AllocChars(pp.second);
	} METHOD_END

	METHOD_SPEC GetMaxLength(LPCWSTR pszName, int* piMaxNameLen) METHOD_BEGIN {
		*piMaxNameLen = Volume.MaxNameLength();
	} METHOD_END

	// IShellFolderViewCB
	COM_DECLSPEC_NOTHROW STDMETHODIMP MessageSFVCB(UINT uMsg, WPARAM wParam, LPARAM lParam) METHOD_BEGIN {
	} METHOD_END

	// IFolderViewSettings
	METHOD_SPEC GetColumnPropertyList(REFIID riid, void** ppv) METHOD_BEGIN {
	} METHOD_END

	METHOD_SPEC GetGroupByProperty(PROPERTYKEY* pkey, BOOL* pfGroupAscending) METHOD_BEGIN {
	} METHOD_END

	METHOD_SPEC GetViewMode(FOLDERLOGICALVIEWMODE* plvm) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP GetIconSize(UINT* puIconSize) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP GetFolderFlags(FOLDERFLAGS* pfolderMask, FOLDERFLAGS* pfolderFlags) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP GetSortColumns(SORTCOLUMN* rgSortColumns, UINT cColumnsIn, UINT* pcColumnsOut) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP GetGroupSubsetCount(UINT* pcVisibleRows) METHOD_BEGIN {
	} METHOD_END

	// IPreviewHandler
	STDMETHODIMP SetWindow(HWND hwnd, const RECT* prc) METHOD_BEGIN {
	} METHOD_END

	CWnd wndPreview;

	STDMETHODIMP SetRect(const RECT* prc) METHOD_BEGIN {
		if (::IsWindow(wndPreview))
			wndPreview.Move(*prc);
	} METHOD_END

	STDMETHODIMP DoPreview() METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP Unload() METHOD_BEGIN {
		wndPreview.Destroy();
	} METHOD_END

	STDMETHODIMP SetFocus() METHOD_BEGIN {
		if (!::IsWindow(wndPreview))
			return S_FALSE;
		wndPreview.SetFocus();
	} METHOD_END

	STDMETHODIMP QueryFocus(HWND* phwnd) METHOD_BEGIN {
		Win32Check(bool(*phwnd = ::GetFocus()));
	} METHOD_END

	STDMETHODIMP IPreviewHandler::TranslateAccelerator(MSG* pmsg) METHOD_BEGIN {
	} METHOD_END

	// IStorage
	STDMETHODIMP STDMETHODCALLTYPE CreateStream(const OLECHAR* pwcsName, DWORD grfMode, DWORD reserved1, DWORD reserved2, IStream** ppstm) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP OpenStream(const OLECHAR* pwcsName, void* reserved1, DWORD grfMode, DWORD reserved2, IStream** ppstm) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP CreateStorage(const OLECHAR* pwcsName, DWORD grfMode, DWORD reserved1, DWORD reserved2, IStorage** ppstg) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP OpenStorage(const OLECHAR* pwcsName, IStorage* pstgPriority, DWORD grfMode, SNB snbExclude, DWORD reserved, IStorage** ppstg) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP CopyTo(DWORD ciidExclude, const IID* rgiidExclude, SNB snbExclude, IStorage* pstgDest) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP MoveElementTo(const OLECHAR* pwcsName, IStorage* pstgDest, const OLECHAR* pwcsNewName, DWORD grfFlags) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP Commit(DWORD grfCommitFlags) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP Revert() METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP EnumElements(DWORD reserved1, void* reserved2, DWORD reserved3, IEnumSTATSTG** ppenum) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP DestroyElement(const OLECHAR* pwcsName) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP RenameElement(const OLECHAR* pwcsOldName, const OLECHAR* pwcsNewName) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP SetElementTimes(const OLECHAR* pwcsName, const FILETIME* pctime, const FILETIME* patime, const FILETIME* pmtime) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP SetClass(REFCLSID clsid) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP SetStateBits(DWORD grfStateBits, DWORD grfMask) METHOD_BEGIN {
	} METHOD_END

	STDMETHODIMP Stat(STATSTG* pstatstg, DWORD grfStatFlag) METHOD_BEGIN {
	} METHOD_END

	LRESULT OnDeleteItem(UINT CtlID, LPNMHDR lpnmh, BOOL& bHandled) {
		auto dirEntry = (DirEntry*)((NM_LISTVIEW*)lpnmh)->lParam;
		for (auto it = dirEntries.begin(); it != dirEntries.end(); ++it)
			if (it->get() == dirEntry) {
				dirEntries.erase(it);
				break;
			}
		return 0;
	}

	void UpdateDir() override {
		ShellPath pathFolder = _pathRoot;
		pathFolder /= _pathPath;
		::SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_IDLIST | SHCNF_FLUSH, (PIDLIST_RELATIVE)pathFolder, NULL);
	}

	LRESULT OnNotifyDispInfo(UINT CtlID, LPNMHDR lpnmh, BOOL& bHandled) {
		LV_DISPINFO* lpdi = (LV_DISPINFO*)lpnmh;
		LPCITEMIDLIST pidl = reinterpret_cast<LPCITEMIDLIST>(lpdi->item.lParam);
		ATLASSERT(pidl);
		lpdi->item.mask |= LVIF_DI_SETITEM;   // dont ask us again
		// Item text is being requested?
		if ((lpdi->item.mask & LVIF_TEXT) != 0) {
			String someText = "Some Text";
			StrCpyN(lpdi->item.pszText, someText, lpdi->item.cchTextMax);
			//m_pFolder->_GetItemDetails(pidl, (UINT)lpdi->item.iSubItem, lpdi->item.pszText, lpdi->item.cchTextMax, false);
		}
		// Item icon is being requested?
		/*
		if ((lpdi->item.mask & LVIF_IMAGE) != 0) {
			CComPtr<IExtractIcon> spEI;
			if (SUCCEEDED(m_pFolder->GetUIObjectOf(m_hWnd, 1, &pidl, IID_IExtractIcon, NULL, (LPVOID*)&spEI))) {
				// The GetIconLoaction() will give us the index into our image list...
				UINT uFlags = 0;
				spEI->GetIconLocation(GIL_FORSHELL, NULL, 0, &lpdi->item.iImage, &uFlags);
				ATLASSERT((uFlags & GIL_NOTFILENAME) != 0);
			}
		}
		*/
		return 0;
	}


	DECLARE_NO_REGISTRY()

	BEGIN_COM_MAP(FsShellExt)
		COM_INTERFACE_ENTRY(IShellExtInit)
//!!!?		COM_INTERFACE_ENTRY(IContextMenu)
		COM_INTERFACE_ENTRY(IInitializeCommand)
		COM_INTERFACE_ENTRY(IInitializeWithStream)
		COM_INTERFACE_ENTRY(IForegroundTransfer)
		COM_INTERFACE_ENTRY(IObjectWithSite)
		COM_INTERFACE_ENTRY(IPersistFolder)
		COM_INTERFACE_ENTRY(IPersistFolder2)
		COM_INTERFACE_ENTRY(IPersistFolder3)
		COM_INTERFACE_ENTRY(IShellFolder)
		COM_INTERFACE_ENTRY(IShellFolder2)
		COM_INTERFACE_ENTRY(IItemNameLimits)
		COM_INTERFACE_ENTRY(IPreviewHandler)
		COM_INTERFACE_ENTRY(IShellFolderViewCB)
		COM_INTERFACE_ENTRY(IFolderViewSettings)
		COM_INTERFACE_ENTRY(IStorage)

		// Unnecessary for Namespace extension
		// COM_INTERFACE_ENTRY(IExecuteCommandApplicationHostEnvironment)
		// COM_INTERFACE_ENTRY(IExecuteCommand)
		// COM_INTERFACE_ENTRY(IObjectWithSelection)
	END_COM_MAP()

};


class ATL_NO_VTABLE CDropTarget
	: public CComObjectRootEx<CComSingleThreadModel>
	, public CComCoClass<CDropTarget>
	, public IDropTarget
{
	CComPtr<IDropTargetHelper> iDropTargetHelper;
	CComPtr<IDataObject> iDataObject;
	CComPtr<FsShellExt> folder;
	HWND hWndTarget;
	bool bAcceptFmt = false;
public:
	void Init(FsShellExt* folder, HWND hWnd, bool bUseHelper) {
		this->folder = folder;
		hWndTarget = hWnd;
		if (bUseHelper)
			iDropTargetHelper = CreateComObject(CLSID_DragDropHelper);
	}

	STDMETHODIMP DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) METHOD_BEGIN {
		if (iDropTargetHelper)
			OleCheck(iDropTargetHelper->DragEnter(hWndTarget, pDataObj, reinterpret_cast<POINT*>(&pt), *pdwEffect));
		iDataObject = pDataObj;
		bAcceptFmt = folder->IsClipDataAvailable(pDataObj, true);
		*pdwEffect = QueryDrop(grfKeyState, *pdwEffect);
	} METHOD_END

	STDMETHODIMP DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) METHOD_BEGIN {
		if (iDropTargetHelper)
			OleCheck(iDropTargetHelper->DragOver(reinterpret_cast<POINT*>(&pt), *pdwEffect));
		*pdwEffect = QueryDrop(grfKeyState, *pdwEffect);
	} METHOD_END

	STDMETHODIMP DragLeave() METHOD_BEGIN {
		if (iDropTargetHelper)
			OleCheck(iDropTargetHelper->DragLeave());
		iDataObject.Release();

	} METHOD_END

	STDMETHODIMP Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) METHOD_BEGIN {
		if (iDropTargetHelper)
			OleCheck(iDropTargetHelper->Drop(pDataObj, reinterpret_cast<POINT*>(&pt), *pdwEffect));
		iDataObject.Release();
		DWORD dwDropEffect = QueryDrop(grfKeyState, *pdwEffect);
		*pdwEffect = DROPEFFECT_NONE;                            // Default to failed/cancelled
		if (dwDropEffect != DROPEFFECT_NONE) {			// Did we just deny this drop?
			DWORD dwFlags = grfKeyState & MK_SILENT ? FILEOP_SILENT : 0;
			try {
				folder->DoDrop(/*m_hwndTarget,*/ pDataObj, dwDropEffect, dwFlags);
				SetDWORD(pDataObj, g_formatPasteSucceded.Id, *pdwEffect = dwDropEffect);
			} catch (exception&) {}
		}
	} METHOD_END

	BEGIN_COM_MAP(CDropTarget)
		COM_INTERFACE_ENTRY_IID(IID_IDropTarget, IDropTarget)
	END_COM_MAP()

private:
	DWORD GetDropEffectFromKeyState(DWORD grfKeyState) const {
		// The DROPEFFECT_COPY operation is the default.
		// We don't support DROPEFFECT_LINK operations.
		return grfKeyState & MK_SHIFT ? DROPEFFECT_MOVE: DROPEFFECT_COPY;
	}

	DWORD QueryDrop(DWORD grfKeyState, DWORD dwEffect) const {
		if (!bAcceptFmt) return DROPEFFECT_NONE;
		DWORD dwMask = GetDropEffectFromKeyState(grfKeyState);
		return dwEffect & dwMask ? dwEffect & dwMask
			: dwEffect & DROPEFFECT_COPY ? DROPEFFECT_COPY	// Map common alternatives
			: dwEffect & DROPEFFECT_MOVE ? DROPEFFECT_MOVE
			: DROPEFFECT_COPY;
	}
};


OBJECT_ENTRY_AUTO(CLSID_Rt11Fs, FsShellExt)

static ShellPath ToShellPath(const DirEntry& e) {
	MemoryStream ms;
	BinaryWriter wr(ms);
	wr << USHORT(0);
	wr << e;
	ms.Position = 0;
	wr << USHORT(ms.Length);
	return ShellPath((LPCITEMIDLIST)ms.AsSpan().data());
}

static const Guid s_IID_UnknownInCreateViewObject("{93F81976-6A0D-42C3-94DD-AA258A155470}");

STDMETHODIMP FsShellExt::CreateViewObject(HWND hwndOwner, REFIID riid, void** ppv) noexcept METHOD_BEGIN_IMP {
	//!!!? volume_.reset();	// Because may be opened in other Instance

	if (riid == IID_IDropTarget) {
		TRC(1, "riid: " << "IID_IDropTarget");
		CreateComInstance<CDropTarget>((IDropTarget**)ppv, this, hwndOwner, true);
		return S_OK;
	} else if (riid == IID_IShellView) {
		TRC(1, "riid: " << "IID_IShellView");

		CComPtr<IShellView> iShellView;
#if UCFG_USE_SHELL_DEFVIEW
		SFV_CREATE csfv = { .cbSize = sizeof(csfv), .pshf = this };
		OleCheck(::SHCreateShellFolderView(&csfv, &iShellView));
#else
		iShellView = CreateShellView(this);
#endif
		if (CComQIPtr<IFolderView> iFolderView = iShellView)
			OleCheck(iFolderView->SetCurrentViewMode(FVM_DETAILS));
		*ppv = iShellView.Detach();
		return S_OK;
	} else if (riid == SDefined_Unknown3) {
		TRC(1, "riid: " << "SDefined_Unknown3");
		return E_NOTIMPL;
	} else if (riid != s_IID_UnknownInCreateViewObject) {
		TRC(1, "riid: " << riid);
	}
	return QueryInterface(riid, ppv);
} METHOD_END

enum class ContextMenuId {
	IDM_OPEN = 0,
	IDM_COPY,
	IDM_CUT,
	IDM_PASTE,
	IDM_RENAME,
	IDM_DELETE,
	IDM_UNDELETE,
	IDM_PROPERTIES,
	IDM_VIEWLIST,
	IDM_VIEWDETAILS,
	IDM_NEWFOLDER,
	IDM_LAST,
	IDM_SEPARATOR,
};

struct NS_CONTEXTMENUINFO {
	ContextMenuId uCommand;  // Index / ID of menuitem
	LPCSTR pstrCommand;       // Internal/shell command string (verb)
	UINT nTextRes;            // Resource ID for display text (can include '&'-char for shortcut)
	UINT nStatusRes;          // Resource ID for statusbar text
	BOOL bIsDefault;          // Is this the default item?
	DWORD dwShellAttribs;     // Shell attributes needed to display this item (SFGAO_xxx)
	DWORD dwDisplayAttribs;   // Display control attribute for this item (SFGAO_xxx)
	UINT uMinCount;           // Minimum number of selected items needed
	UINT uMaxCount;           // Maximim number of selected items needed
};

static const NS_CONTEXTMENUINFO s_CtxItems[] =
{
   { ContextMenuId::IDM_OPEN,        "open",              IDS_OPEN,         ID_FILE_OPEN,       TRUE,  SFGAO_BROWSABLE, SFGAO_REMOVABLE, 1, 1 },
   { ContextMenuId::IDM_SEPARATOR,   "-",                 0, 0,                                 FALSE, 0, 0, 0, 0 },
   { ContextMenuId::IDM_COPY,        "copy",              IDS_COPY,         ID_EDIT_COPY,       FALSE, SFGAO_CANCOPY, 0, 1, 99999 },
   { ContextMenuId::IDM_CUT,         "cut",               IDS_CUT,          ID_EDIT_CUT,        FALSE, SFGAO_CANMOVE, 0, 1, 99999 },
   { ContextMenuId::IDM_PASTE,       "paste",             IDS_PASTE,        ID_EDIT_PASTE,      FALSE, SFGAO_BROWSABLE, 0, 0, 99999 },
   { ContextMenuId::IDM_SEPARATOR,   "-",                 0, 0,                                 FALSE, 0, 0, 0, 0 },
   { ContextMenuId::IDM_RENAME,      "rename",            IDS_RENAME,       ID_EDIT_RENAME,     FALSE, SFGAO_CANRENAME, SFGAO_REMOVABLE, 1, 1 },
   { ContextMenuId::IDM_DELETE,      "delete",            IDS_DELETE,       ID_EDIT_DELETE,     FALSE, SFGAO_CANDELETE, 0, 1, 99999 },
   { ContextMenuId::IDM_UNDELETE,    "undelete",          IDS_UNDELETE,     ID_EDIT_UNDELETE,   FALSE, SFGAO_GHOSTED, SFGAO_REMOVABLE, 1, 99999 },
   { ContextMenuId::IDM_SEPARATOR,   "-",                 0, 0,                                 FALSE, 0, 0, 0, 0 },
   { ContextMenuId::IDM_PROPERTIES,  "properties",        IDS_PROPERTIES,   ID_EDIT_PROPERTIES, FALSE, 0, 0, 0, 1 },
   { ContextMenuId::IDM_NEWFOLDER,   CMDSTR_NEWFOLDERA,   0, 0,                                 FALSE, 0, SFGAO_NONENUMERATED, 0, 0 },
   { ContextMenuId::IDM_VIEWLIST,    CMDSTR_VIEWLISTA,    0, 0,                                 FALSE, 0, SFGAO_NONENUMERATED, 0, 0 },
   { ContextMenuId::IDM_VIEWDETAILS, CMDSTR_VIEWDETAILSA, 0, 0,                                 FALSE, 0, SFGAO_NONENUMERATED, 0, 0 },
};

static bool s_bShowDeletedFiles = false;

STDMETHODIMP FsShellExt::QueryContextMenu(HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) noexcept METHOD_BEGIN_IMP {
	if (uFlags & CMF_DEFAULTONLY) return 0;
	Menu menu(hmenu, false);
	menu.Insert(indexMenu, MF_BYPOSITION, idCmdFirst, "Mount RT-11 Volume ContextMenu!!!D");
	return 1;
} METHOD_END


} // U::VolumeShell
