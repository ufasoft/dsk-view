// Windows Shell extension to mount Volume file


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

//using namespace Ext::Gui;
using namespace Ext::Ole;
using namespace Ext::Win;

#include <afxres.h>
#include "resource.h"
#include "volume.h"
using namespace U::FS;

#include "ShellExt.h"

using namespace std;
using namespace std::filesystem;
using namespace Ext;

namespace U::VolumeShell {

const DataFormats::Format
	g_formatFileContents = DataFormats::GetFormat(DataFormats::FileContents)
	, g_formatFileGroupDescriptor = DataFormats::GetFormat(DataFormats::FileGroupDescriptior)
	, g_formatFileGroupDescriptorA = DataFormats::GetFormat(DataFormats::FileGroupDescriptiorA)
	, g_formatPasteSucceded = DataFormats::GetFormat(DataFormats::PasteSucceeded)
	, g_formatLogicalPerformedDropEffect = DataFormats::GetFormat(DataFormats::LogicalPerformedDropEffect)
	, g_formatOleClipboardPersistOnFlush = DataFormats::GetFormat(DataFormats::OleClipboardPersistOnFlush)
	, g_formatPerformedDropEffect = DataFormats::GetFormat(DataFormats::LogicalPerformedDropEffect)
	, g_formatPreferredDropEffect = DataFormats::GetFormat(DataFormats::PreferredDropEffect)
	, g_formatShellIdList = DataFormats::GetFormat(DataFormats::ShellIdList)
	, g_formatTargetClsid = DataFormats::GetFormat(DataFormats::TargetClsid);


DWORD GetDWORD(IDataObject* iDataObject, CLIPFORMAT cf) {
	FORMATETC fmte = { cf, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
	StorageMedium medium;
	OleCheck(iDataObject->GetData(&fmte, &medium));
	return *GlobalLockerT<DWORD>(medium.hGlobal);
}

void SetDWORD(IDataObject* iDataObject, CLIPFORMAT cf, DWORD val) {
	CGlobalAlloc ga(sizeof(DWORD), GMEM_MOVEABLE | GMEM_SHARE | GMEM_DISCARDABLE);
	*GlobalLockerT<DWORD>(ga) = val;
	FORMATETC fmte = { cf, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
	STGMEDIUM medium = { TYMED_HGLOBAL };
	medium.hGlobal = ga;
	OleCheck(iDataObject->SetData(&fmte, &medium, TRUE));
	ga.Detach(); // ownership transferred
}

STDMETHODIMP CDataObject::GetData(FORMATETC* pFormatetc, STGMEDIUM* pMedium) METHOD_BEGIN_IMP {
	TRC(1, "cfFormat: " << DataFormats::GetFormat(pFormatetc->cfFormat).Name);

	if (m_aFiles.empty())
		return E_UNEXPECTED;

	ZeroStruct(*pMedium);

	// There are a few clipboard formats that we support natively and generate
	// data for on the fly.

	if (pFormatetc->cfFormat == g_formatFileGroupDescriptor.Id) {
		// The CFSTR_FILEDESCRIPTOR format will be queried by the Shell before
		// the CFSTR_FILECONTENTS.
		// It returns descriptions of the files the IDataObject contains.
		if ((pFormatetc->tymed & TYMED_HGLOBAL) == 0) return DV_E_TYMED;

		FILEGROUPDESCRIPTOR fgd = { 0 };
		const DWORD fgd_size = sizeof(fgd) - sizeof(FILEDESCRIPTOR);
		fgd.cItems = m_aFiles.size();

		DWORD dwTotalSize = fgd_size + (sizeof(FILEDESCRIPTOR) * m_aFiles.size());
		HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE | GMEM_ZEROINIT | GMEM_DISCARDABLE, dwTotalSize);
		if (hMem == NULL)
			return E_OUTOFMEMORY;
		GlobalLocker gl(hMem);
		BYTE* p = gl.Cast<BYTE*>();
		memcpy(p, &fgd, fgd_size);
		memcpy(p + fgd_size, m_aFiles.data(), sizeof(FILEDESCRIPTOR) * m_aFiles.size());

		pMedium->hGlobal = hMem;
		pMedium->tymed = TYMED_HGLOBAL;
		return S_OK;
	} else if (pFormatetc->cfFormat == g_formatFileContents.Id) {
		// The CFSTR_FILECONTENTS gets called for each file.
		// It must return the IStream for the file.
		if ((pFormatetc->tymed & TYMED_ISTREAM) == 0) return DV_E_TYMED;

		LONG iIndex = pFormatetc->lindex;
		if (iIndex < 0 || iIndex >= (LONG)m_aFiles.size()) return DV_E_LINDEX;

		const FILEDESCRIPTOR& fd = m_aFiles[iIndex];

		unique_ptr<MemoryStream> pMS(new MemoryStream);
		auto& entry = *Vol->GetEntry(fd.cFileName);
		Vol->CopyFileTo(entry, *pMS);
		pMS->Position = 0;

		// Let's initialize the Stream...
		CreateComInstance<CComStream>(&pMedium->pstm, pMS.release(), entry.FileName, entry.CreationTime);
		pMedium->tymed = TYMED_ISTREAM;
		return S_OK;
	} else if (pFormatetc->cfFormat == g_formatShellIdList.Id) {
		/*
		// The Common Dialog "FileOpen"-dialog will query this.
		// So will Vista drag'n'drop. It is a list of PIDLs.
		if ((pFormatetc->tymed & TYMED_HGLOBAL) == 0) return DV_E_TYMED;
		CPidl pidlFolder;
		pidlFolder.Construct(m_pFolder->m_pidlRoot, m_pFolder->m_pidlPath);
		UINT dwSize = sizeof(CIDA) + (m_pidls.GetCount() * sizeof(UINT));
		UINT uOffset = dwSize;
		dwSize += pidlFolder.GetByteSize();
		for (UINT i = 0; i < m_pidls.GetCount(); i++) dwSize += CPidl::PidlGetByteSize(m_pidls[i]);
		HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE | GMEM_ZEROINIT | GMEM_DISCARDABLE, dwSize);
		if (hMem == NULL) return STG_E_MEDIUMFULL;
		LPBYTE p = (LPBYTE) ::GlobalLock(hMem);
		if (p == NULL) return STG_E_MEDIUMFULL;
		CIDA* cida = (CIDA*)p;
		cida->cidl = m_pidls.GetCount();
		cida->aoffset[0] = uOffset;
		::CopyMemory(p + uOffset, pidlFolder.GetData(), pidlFolder.GetByteSize());
		uOffset += pidlFolder.GetByteSize();
		for (UINT j = 0; j < m_pidls.GetCount(); j++) {
			cida->aoffset[j + 1] = uOffset;
			::CopyMemory(p + uOffset, static_cast<LPCITEMIDLIST>(m_pidls[j]), CPidl::PidlGetByteSize(m_pidls[j]));
			uOffset += CPidl::PidlGetByteSize(m_pidls[j]);
		}
		::GlobalUnlock(hMem);
		pMedium->hGlobal = hMem;
		pMedium->tymed = TYMED_HGLOBAL;
		*/
		return S_OK;
	}

	// Find it in our own collection and return that...
	int iIndex = FindDataObject(pFormatetc);
	if (iIndex < 0)
		return DV_E_FORMATETC;
	AddRefStgMedium(pMedium, &m_aDataItems[iIndex].StgMedium, &m_aDataItems[iIndex].Formatetc);
} METHOD_END

METHOD_SPEC CDataObject::Load(IStream* pStream) METHOD_BEGIN_IMP {
	Throw(E_NOTIMPL);
	/*
	HRESULT Hr;
	DWORD dwBytesRead = 0;

	HWND hWnd = NULL;
	HR(pStream->Read(&hWnd, sizeof(HWND), &dwBytesRead));
	if (!::IsWindow(hWnd)) hWnd = ::GetDesktopWindow();

	CPidl pidlRoot;
	CPidl pidlPath;
	HR(pidlRoot.ReadFromStream(pStream));
	HR(pidlPath.ReadFromStream(pStream));

	CPidlList pidls;
	HR(pidls.ReadFromStream(pStream));

	CComObject<CFolder>* pFolder = NULL;
	HR(CComObject<CFolder>::CreateInstance(&pFolder));
	CComPtr<IUnknown> spKeepAlive = pFolder->GetUnknown();
	HR(pFolder->Initialize(pidlRoot));
	pFolder->m_pidlPath.Copy(pidlPath);

	HR(Init(pFolder, hWnd, pidls, pidls.GetCount()));

	DWORD dwPreferredDropEffect = DROPEFFECT_NONE;
	HR(pStream->Read(&dwPreferredDropEffect, sizeof(DWORD), &dwBytesRead));
	if (dwPreferredDropEffect != DROPEFFECT_NONE) {
		DataObj_SetDWORD(this, _Module.m_CFSTR_PREFERREDDROPEFFECT, dwPreferredDropEffect);
	}
	*/
} METHOD_END

STDMETHODIMP CDataObject::Save(IStream* pStream, BOOL /*fClearDirty*/) METHOD_BEGIN_IMP {
	Throw(E_NOTIMPL);
	/*
	if (m_pFolder == NULL) return STG_E_CANTSAVE;
	if (m_aFiles.size() == 0) return STG_E_CANTSAVE;

	HRESULT Hr;
	DWORD dwBytesWritten = 0;

	HR(pStream->Write(&m_hWnd, sizeof(HWND), &dwBytesWritten));

	HR(m_pFolder->m_pidlRoot.WriteToStream(pStream));
	HR(m_pFolder->m_pidlPath.WriteToStream(pStream));

	HR(m_pidls.WriteToStream(pStream));

	DWORD dwPreferredDropEffect = DROPEFFECT_NONE;
	/*!!!T
	DataObj_GetDWORD(this, _Module.m_CFSTR_PREFERREDDROPEFFECT, &dwPreferredDropEffect);
	HR(pStream->Write(&dwPreferredDropEffect, sizeof(DWORD), &dwBytesWritten));
	*/
} METHOD_END

STDMETHODIMP CDataObject::SetData(FORMATETC* pFormatetc, STGMEDIUM* pMedium, BOOL fRelease) METHOD_BEGIN_IMP {
	TRC(1, "cfFormat: " << DataFormats::GetFormat(pFormatetc->cfFormat).Name);
	if (pFormatetc->ptd)
		return DV_E_DVTARGETDEVICE;

	CComPtr<IUnknown> spKeepAlive = GetUnknown();

	DataObject Data = { 0 };
	Data.Formatetc = *pFormatetc;
	Data.StgMedium = *pMedium;
	if (!fRelease) {
		CopyStgMedium(&Data.StgMedium, pMedium, pFormatetc);
	} else if (CComPtr<IUnknown>(GetUnknown()).IsEqualObject(Data.StgMedium.pUnkForRelease)) {
		CopyStgMedium(&Data.StgMedium, pMedium, pFormatetc);
		Release();
	}
	int iIndex = FindDataObject(pFormatetc);
	if (iIndex < 0) {
		m_aDataItems.push_back(Data);
	} else {
		::ReleaseStgMedium(&m_aDataItems[iIndex].StgMedium);
		m_aDataItems[iIndex] = Data;
	}

	if (pFormatetc->cfFormat == g_formatPasteSucceded.Id)
	{
		DWORD dwPasteSucceeded = GetDWORD(this, g_formatPasteSucceded.Id);
		DWORD dwPerformedDropEffect = GetDWORD(this, g_formatPerformedDropEffect.Id);
		DWORD dwLogicalPerformedDropEffect = GetDWORD(this, g_formatLogicalPerformedDropEffect.Id);

		// To handle Delete-on-Paste, we check these guys...
		DWORD dwEffect = ((dwPasteSucceeded == DROPEFFECT_MOVE && dwPerformedDropEffect == DROPEFFECT_MOVE) || dwLogicalPerformedDropEffect == DROPEFFECT_MOVE)
			&& !m_bAsyncStarted ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
		PasteSucceeded(dwEffect);
	}

	if (pFormatetc->cfFormat == g_formatTargetClsid.Id) {
		m_clsidTarget = *GlobalLocker(pMedium->hGlobal).Cast<CLSID*>();
	}
} METHOD_END

void CDataObject::AddDataObjectOnHGLOBAL(CLIPFORMAT cfFormat, DWORD dwValue) {
	CGlobalAlloc ga(sizeof(DWORD), GMEM_MOVEABLE | GMEM_SHARE | GMEM_ZEROINIT | GMEM_DISCARDABLE);
	*GlobalLockerT<DWORD>(ga) = dwValue;
	DataObject Data = { 0 };
	Data.Formatetc.cfFormat = cfFormat;
	Data.Formatetc.tymed = TYMED_HGLOBAL;
	Data.Formatetc.lindex = -1;
	Data.Formatetc.dwAspect = DVASPECT_CONTENT;
	Data.StgMedium.tymed = TYMED_HGLOBAL;
	Data.StgMedium.hGlobal = ga.Detach();
	m_aDataItems.push_back(Data);
}

void CDataObject::AddDataObjectPlaceholder(CLIPFORMAT cfFormat, DWORD tymed) {
	DataObject Data = { 0 };
	Data.Formatetc.cfFormat = cfFormat;
	Data.Formatetc.tymed = tymed;
	Data.Formatetc.lindex = -1;
	Data.Formatetc.dwAspect = DVASPECT_CONTENT;
	Data.StgMedium.tymed = tymed;
	m_aDataItems.push_back(Data);
}

void CDataObject::CollectFile(RCString name) {
	TRC(1, "name: " << name);
	auto& entry = *Vol->GetEntry(name.c_wstr());
	FILEDESCRIPTOR fd = { 0 };
	fd.dwFlags = FD_FILESIZE | FD_CREATETIME | FD_WRITESTIME | FD_ATTRIBUTES;
	wcsncpy(fd.cFileName, name, size(fd.cFileName));
	fd.nFileSizeLow = DWORD(entry.Length);
	fd.nFileSizeHigh = DWORD(entry.Length >> 32);
	fd.ftCreationTime = entry.CreationTime;
	fd.ftLastWriteTime = entry.LastWriteTime;
	if (entry.ReadOnly)
		fd.dwFileAttributes |= FILE_ATTRIBUTE_READONLY;
	if (fd.dwFileAttributes == 0)
		fd.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
	m_aFiles.push_back(fd);
}

void CDataObject::Init(IShellExt* shellExt, CUnkPtr iShellExt, Volume* vol, HWND hWnd, LPCITEMIDLIST* pPidls, int nCount) {
	TRC(1, "");
	if (nCount == 0)
		Throw(E_INVALIDARG);

	m_hWnd = hWnd;
	ShellExt = shellExt;
	this->iShellExt = iShellExt;
	Vol = vol;

	for (int i = 0; i < nCount; ++i) {
		ShellPath sp = pPidls[i];
		m_pidls.push_back(sp);
		CollectFile(sp.ToSimpleString());
	}


	/*

	// Collect information about the files and folders
	HRESULT _CollectFiles(LPCITEMIDLIST* pidls, int nCount) {
		ATLASSERT(pidls);
		CAdfDevice dev;
		CAdfVolume vol;
		HRESULT Hr;
		HR(m_pFolder->_OpenAmigaDevice(m_pFolder->m_pidlRoot, TRUE, TRUE, m_pFolder->m_pidlPath, dev, vol));
		for (int i = 0; i < nCount; i++) {
			TCHAR szName[MAXNAMELEN + 1] = { 0 };
			PidlGetName(szName, pidls[i]);
			_CollectFile(vol, _T(""), szName);
			if (PidlGetType(pidls[i]) == PT_FOLDER) _CollectFolder(vol, _T(""), szName);
		}
		return S_OK;
	}


	HR(_CollectFiles(m_pidls, m_pidls.GetCount()));

	HR(_AddDataObjectOnHGLOBAL(_Module.m_CFSTR_ADFID, m_pFolder->_GetFolderHash()));

	   */

	TRC(1, "Assigning Clipboard data");
	AddDataObjectOnHGLOBAL(g_formatOleClipboardPersistOnFlush.Id, 0L);
	AddDataObjectPlaceholder(g_formatFileGroupDescriptor.Id, TYMED_HGLOBAL);
	AddDataObjectPlaceholder(g_formatFileContents.Id, TYMED_ISTREAM);
	AddDataObjectPlaceholder(g_formatShellIdList.Id, TYMED_HGLOBAL);

	iShellThread = ShellThreadRef::get_Value();
}

int CDataObject::FindDataObject(const FORMATETC* pFormatetc) const {
	for (int i = 0; i < this->m_aDataItems.size(); ++i) {
		auto& o = m_aDataItems[i];
		if (o.Formatetc.cfFormat == pFormatetc->cfFormat
			&& (o.Formatetc.tymed & pFormatetc->tymed) != 0
			&& (o.Formatetc.dwAspect & pFormatetc->dwAspect) != 0
			&& o.Formatetc.lindex == pFormatetc->lindex)
		{
			return i;
		}
	}
	return -1;
}

void CDataObject::CopyStgMedium(STGMEDIUM* pMedDest, const STGMEDIUM* pMedSrc, const FORMATETC* pFmtSrc) const {
	pMedDest->tymed = pMedSrc->tymed;
	pMedDest->pUnkForRelease = NULL;
	switch (pMedSrc->tymed) {
	case TYMED_GDI:
	case TYMED_FILE:
	case TYMED_ENHMF:
	case TYMED_MFPICT:
	case TYMED_HGLOBAL:
	{
		pMedDest->hGlobal = (HGLOBAL) ::OleDuplicateData(pMedSrc->hGlobal, pFmtSrc->cfFormat, NULL);
	}
	break;
	case TYMED_ISTREAM:
	{
		pMedDest->pstm = NULL;
		LARGE_INTEGER dlibMove = { 0, 0 };
		ULARGE_INTEGER dlibAlot = { 0, 999999999 };
		OleCheck(::CreateStreamOnHGlobal(NULL, TRUE, &pMedDest->pstm));
		OleCheck(pMedSrc->pstm->Seek(dlibMove, STREAM_SEEK_SET, NULL));
		OleCheck(pMedDest->pstm->Seek(dlibMove, STREAM_SEEK_SET, NULL));
		OleCheck(pMedSrc->pstm->CopyTo(pMedDest->pstm, dlibAlot, NULL, NULL));
		OleCheck(pMedSrc->pstm->Seek(dlibMove, STREAM_SEEK_SET, NULL));
		OleCheck(pMedDest->pstm->Seek(dlibMove, STREAM_SEEK_SET, NULL));
	}
	break;
	case TYMED_ISTORAGE:
	{
		pMedDest->pstg = NULL;
		OleCheck(::StgCreateDocfile(NULL, STGM_READWRITE | STGM_SHARE_EXCLUSIVE | STGM_DELETEONRELEASE | STGM_CREATE, 0, &pMedDest->pstg));
		OleCheck(pMedSrc->pstg->CopyTo(0, NULL, NULL, pMedDest->pstg));
	}
	break;
	default:
		ATLASSERT(false);
		Throw(DV_E_TYMED);
	}
}

void CDataObject::PasteSucceeded(DWORD dwEffect) {
	Throw(E_NOTIMPL);
	/*
	if (dwEffect == DROPEFFECT_MOVE || m_clsidTarget == CLSID_RecycleBin)
	{
		m_pFolder->_DeleteFiles(m_hWnd, m_pidls, m_pidls.GetCount(), CFolder::FILEOP_SILENT);
		// If this was a MOVE operation and we are on the clipboard, the clipboard
		// is no longer valid.
		CComPtr<IDataObject> spClipboard;
		::OleGetClipboard(&spClipboard);
		if (spClipboard.IsEqualObject(GetUnknown()))
			::OleSetClipboard(NULL);
	}
	*/
	ShellExt->UpdateDir();
}

void CDataObject::AddRefStgMedium(STGMEDIUM* pMedDest, const STGMEDIUM* pMedSrc, const FORMATETC* pFmtSrc) {
	pMedDest->tymed = pMedSrc->tymed;
	pMedDest->pUnkForRelease = pMedSrc->pUnkForRelease;
	switch (pMedSrc->tymed) {
	case TYMED_GDI:
	case TYMED_FILE:
	case TYMED_ENHMF:
	case TYMED_MFPICT:
	case TYMED_HGLOBAL:
		pMedDest->hGlobal = pMedSrc->hGlobal;
		if (pMedSrc->pUnkForRelease == NULL)
			pMedDest->pUnkForRelease = GetUnknown();
		break;
	case TYMED_ISTREAM:
		(pMedDest->pstm = pMedSrc->pstm)->AddRef();
		break;
	case TYMED_ISTORAGE:
		(pMedDest->pstg = pMedSrc->pstg)->AddRef();
		break;
	default:
		ATLASSERT(false);
		Throw(DV_E_TYMED);
	}
	if (pMedDest->pUnkForRelease != NULL) pMedDest->pUnkForRelease->AddRef();
}

} // U::VolumeShell
