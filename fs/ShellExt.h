// Copyright(c) 2023 Ufasoft  http://ufasoft.com  mailto:support@ufasoft.com,  Sergey Pavlov  mailto:dev@ufasoft.com
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace U::VolumeShell {

extern const DataFormats::Format
	g_formatFileContents
	, g_formatFileGroupDescriptor
	, g_formatFileGroupDescriptorA
	, g_formatPasteSucceded
	, g_formatShellIdList
	, g_formatTargetClsid
	, g_formatLogicalPerformedDropEffect
	, g_formatOleClipboardPersistOnFlush
	, g_formatPerformedDropEffect
	, g_formatPreferredDropEffect;

DWORD GetDWORD(IDataObject* pDataObj, CLIPFORMAT cf);
void SetDWORD(IDataObject* pDataObj, CLIPFORMAT cf, DWORD val);

struct DataObject {
	FORMATETC Formatetc;
	STGMEDIUM StgMedium;
};

class ATL_NO_VTABLE CEnumFORMATETC :
	public CComObjectRootEx<CComSingleThreadModel>,
	public IEnumFORMATETC
{
public:
	vector<FORMATETC> items;
	int pos = 0;

	void Init(const vector<DataObject>& dataItems) {
		for (int i = 0; i < dataItems.size(); i++)
			items.push_back(dataItems[i].Formatetc);
	}

	BEGIN_COM_MAP(CEnumFORMATETC)
		COM_INTERFACE_ENTRY(IEnumFORMATETC)
	END_COM_MAP()

	// IEnumFORMATETC

	STDMETHODIMP Next(ULONG celt, LPFORMATETC pFormatetc, ULONG* pceltFetched) METHOD_BEGIN {
		if (pceltFetched != NULL)
			*pceltFetched = 0L;
		if (pceltFetched == NULL && celt != 1)
			return E_POINTER;
		ULONG nCount = 0;
		while (pos < items.size() && nCount < celt) {
			pFormatetc[nCount++] = items[pos++];
		}
		if (pceltFetched != NULL) *pceltFetched = nCount;
		return celt == nCount ? S_OK : S_FALSE;
	} METHOD_END

	STDMETHODIMP Skip(ULONG celt) METHOD_BEGIN {
		ULONG nCount = 0L;
		while (pos < items.size() && nCount < celt)
			++pos; ++nCount;
		return celt == nCount ? S_OK : S_FALSE;
	} METHOD_END

	STDMETHODIMP Reset() METHOD_BEGIN {
		pos = 0;
	} METHOD_END

	STDMETHODIMP Clone(LPENUMFORMATETC*) METHOD_BEGIN {
		Throw(E_NOTIMPL);
	} METHOD_END
};


class CComStream : public CComObjectRootEx<CComSingleThreadModel>,
	public IStream
{
	unique_ptr<Stream> pStream;
	String filename;
	DateTime timestamp;
public:

	void Init(Stream *pStream, String filename, DateTime timestamp) {
		this->pStream.reset(pStream);
		this->filename = filename;
		this->timestamp = timestamp;
	}

	BEGIN_COM_MAP(CComStream)
		COM_INTERFACE_ENTRY(IStream)
		COM_INTERFACE_ENTRY(ISequentialStream)
	END_COM_MAP()

	// IStream
	STDMETHODIMP Read(LPVOID pv, ULONG cb, ULONG* pcbRead) METHOD_BEGIN {
		if (pcbRead)
			*pcbRead = 0;
		ULONG r = pStream->Read(pv, cb);
		if (pcbRead)
			*pcbRead = r;
		return r == cb ? S_OK : S_FALSE;
	} METHOD_END

	STDMETHODIMP Write(LPCVOID /*pv*/, ULONG /*cb*/, ULONG* pcbWritten) METHOD_BEGIN {
		if (pcbWritten)
			*pcbWritten = 0;
		Throw(E_NOTIMPL);
	} METHOD_END

	STDMETHODIMP Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER* plibNewPosition) METHOD_BEGIN {
		ATLTRACE(_T("CAmigaFileStream::Seek\n"));
		switch (dwOrigin) {
		case STREAM_SEEK_SET:
			pStream->Position = dlibMove.QuadPart;
			break;
		case STREAM_SEEK_CUR:
			pStream->Position = pStream->Position + dlibMove.QuadPart;
			break;
		case STREAM_SEEK_END:
			pStream->Position = pStream->Length - dlibMove.QuadPart;
			break;
		}
		if (plibNewPosition)
			plibNewPosition->QuadPart = pStream->Position;
	} METHOD_END

	STDMETHODIMP SetSize(ULARGE_INTEGER libNewSize) METHOD_BEGIN {
		Throw(E_NOTIMPL);
	} METHOD_END

	// Apparently the Shell calls this because it needs
	// to marshal the IStream between two Explorer processes.
	STDMETHODIMP CopyTo(IStream* pstm, ULARGE_INTEGER cb, ULARGE_INTEGER* pcbRead, ULARGE_INTEGER* pcbWritten) METHOD_BEGIN {
		if (pcbRead) pcbRead->QuadPart = 0;
		if (pcbWritten) pcbWritten->QuadPart = 0;
		CIStream stream(pstm);
		int64_t pos = pStream->Position;
		pStream->CopyTo(stream);
		auto bytes = pStream->Position - pos;
		if (pcbRead)
			pcbRead->QuadPart = bytes;
		if (pcbWritten)
			pcbWritten->QuadPart = bytes;
	} METHOD_END

	STDMETHODIMP Commit(DWORD /*grfCommitFlags*/) METHOD_BEGIN {
		Throw(E_NOTIMPL);
	} METHOD_END

	STDMETHODIMP Revert() METHOD_BEGIN {
		Throw(E_NOTIMPL);
	} METHOD_END

	STDMETHODIMP LockRegion(ULARGE_INTEGER /*libOffset*/, ULARGE_INTEGER /*cb*/, DWORD /*dwLockType*/) METHOD_BEGIN {
		Throw(E_NOTIMPL);
	} METHOD_END

	STDMETHODIMP UnlockRegion(ULARGE_INTEGER /*libOffset*/, ULARGE_INTEGER /*cb*/, DWORD /*dwLockType*/) METHOD_BEGIN {
		Throw(E_NOTIMPL);
	} METHOD_END

	STDMETHODIMP Stat(STATSTG* pstatstg, DWORD grfStatFlag) METHOD_BEGIN {
		ZeroStruct(*pstatstg);
		OleCheck(::SHStrDup(filename, &pstatstg->pwcsName));
		pstatstg->cbSize.QuadPart = pStream->Length;
		pstatstg->ctime = timestamp;
		pstatstg->mtime = timestamp;
		pstatstg->atime = timestamp;
		pstatstg->type = STGTY_STREAM;
		pstatstg->grfMode = STGM_READ;
	} METHOD_END

	STDMETHODIMP Clone(IStream** /*ppstm*/) METHOD_BEGIN {
		Throw(E_NOTIMPL);
	} METHOD_END
};


interface IShellExt {
	virtual void UpdateDir() =0;
};

class ATL_NO_VTABLE
	__declspec(uuid("{55534654-3F35-46d9-0045-54BA20230002}"))
	CDataObject :
	public CComObjectRootEx<CComSingleThreadModel>,
//	public CComCoClass<CDataObject, &CLSID_DataObject>,
	public IDataObjectAsyncCapability,
	public IPersistStream,
	public IDataObject
{
public:
	IShellExt *ShellExt = nullptr;
	CUnkPtr iShellExt;

	Volume *Vol = nullptr;

	vector<DataObject> m_aDataItems;   // Stored clipboard items

	HWND m_hWnd;                             // The source window's HWND
//	CFolder* m_pFolder;                      // CFolder reference
//	CPidlList m_pidls;                       // The list of files
	vector<ShellPath> m_pidls;
	vector<FILEDESCRIPTOR> m_aFiles;   // The files
	CUnkPtr iShellThread;            // Shell Thread reference
	CLSID m_clsidTarget;                     // Target CLSID if set
	bool m_bAsyncStarted = false;                    // Async operation started?
	bool m_bDoOpAsync = false;                       // Async operation requested?

public:
	BEGIN_COM_MAP(CDataObject)
		COM_INTERFACE_ENTRY(IDataObject)
		COM_INTERFACE_ENTRY(IPersistStream)
		COM_INTERFACE_ENTRY(IDataObjectAsyncCapability)
	END_COM_MAP()


	/*
	static HRESULT WINAPI UpdateRegistry(BOOL bRegister) {
		CComBSTR bstrCLSID(CLSID_DataObject);
		CComBSTR bstrDescription;
		CComBSTR bstrProject;
		bstrDescription.LoadString(IDS_DESCRIPTION);
		bstrProject.LoadString(IDS_PROJNAME);
		_ATL_REGMAP_ENTRY rm[] = {
		   { OLESTR("CLSID"), bstrCLSID },
		   { OLESTR("PROJECTNAME"), bstrProject },
		   { OLESTR("DESCRIPTION"), bstrDescription },
		   { NULL,NULL } };
		return _Module.UpdateRegistryFromResource(IDR_DATAOBJECT, bRegister, rm);
	}
	*/

	HRESULT FinalConstruct() {
		m_hWnd = NULL;
//		m_pFolder = NULL;
		m_bDoOpAsync = FALSE;
		m_bAsyncStarted = FALSE;
		m_clsidTarget = CLSID_NULL;
		return S_OK;
	}

	void FinalRelease() {
		for (int i = 0; i < m_aDataItems.size(); i++)
			::ReleaseStgMedium(&m_aDataItems[i].StgMedium);
//		if (m_pFolder != NULL) m_pFolder->Release();
	}

	void AddDataObjectOnHGLOBAL(CLIPFORMAT cfFormat, DWORD dwValue);
	void AddDataObjectPlaceholder(CLIPFORMAT cfFormat, DWORD tymed);
	void CollectFile(RCString name);
	void Init(IShellExt* shellExt, CUnkPtr iShellExt, Volume *vol, HWND hWnd, LPCITEMIDLIST* pPidls, int nCount);

	// IDataObject

	STDMETHODIMP GetData(FORMATETC* pFormatetc, STGMEDIUM* pMedium);

	STDMETHODIMP GetDataHere(FORMATETC* /*pFormatetc*/, STGMEDIUM* /*pMedium*/) METHOD_BEGIN {
		Throw(E_NOTIMPL);
	} METHOD_END

	STDMETHODIMP QueryGetData(FORMATETC* pFormatetc) METHOD_BEGIN {
		TRC(1, "cfFormat: " << pFormatetc->cfFormat);
		// FIX: To make cross-process drag'n'drop work we'll have to deny
		//      that we can return a PIDL list on Windows Vista?! Another Shell bug!
		if (pFormatetc->cfFormat == g_formatShellIdList.Id)
			return DV_E_FORMATETC;
		return FindDataObject(pFormatetc) >= 0 ? S_OK : DV_E_FORMATETC;
	} METHOD_END

	STDMETHODIMP EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppEnumFormatEtc) METHOD_BEGIN {
		if (dwDirection != DATADIR_GET) return E_NOTIMPL;
		CreateComInstance<CEnumFORMATETC>(ppEnumFormatEtc, m_aDataItems);
	} METHOD_END

	STDMETHODIMP SetData(FORMATETC* pFormatetc, STGMEDIUM* pMedium, BOOL fRelease);

	STDMETHODIMP GetCanonicalFormatEtc(FORMATETC* pFormatetcIn, FORMATETC* pFormatetcOut) METHOD_BEGIN {
		ATLTRACE(_T("CDataObject::GetCanonicalFormatEtc\n"));
		*pFormatetcOut = *pFormatetcIn;
		return DATA_S_SAMEFORMATETC;
	} METHOD_END

	STDMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) METHOD_BEGIN {
		Throw(E_NOTIMPL);
	} METHOD_END

	STDMETHODIMP DUnadvise(DWORD) METHOD_BEGIN {
		Throw(E_NOTIMPL);
	} METHOD_END

	STDMETHODIMP EnumDAdvise(IEnumSTATDATA**) METHOD_BEGIN {
		return OLE_E_ADVISENOTSUPPORTED;
	} METHOD_END

	// IPersistStream

	STDMETHODIMP GetClassID(CLSID* pClassID) METHOD_BEGIN {
		ATLTRACE(_T("CDataObject::GetClassID\n"));
		*pClassID = __uuidof(CDataObject);
	} METHOD_END

	STDMETHODIMP IsDirty(void) METHOD_BEGIN {
		Throw(E_NOTIMPL);
	} METHOD_END

	STDMETHODIMP Load(IStream* pStream);
	STDMETHODIMP Save(IStream* pStream, BOOL /*fClearDirty*/);

	STDMETHODIMP GetSizeMax(ULARGE_INTEGER* pcbSize) METHOD_BEGIN {
		Throw(E_NOTIMPL);
		// if (pcbSize != NULL) pcbSize->QuadPart = (m_pidls.GetCount() + 3) * 1000L;
	} METHOD_END

	// IDataObjectAsyncCapability

	STDMETHODIMP SetAsyncMode(BOOL fDoOpAsync) METHOD_BEGIN {
		ATLTRACE(_T("CDataObject::SetAsyncMode (%x)\n"), fDoOpAsync);
		m_bDoOpAsync = fDoOpAsync;
	} METHOD_END

	STDMETHODIMP GetAsyncMode(BOOL* pfIsOpAsync) METHOD_BEGIN {
		ATLTRACE(_T("CDataObject::GetAsyncMode\n"));
		*pfIsOpAsync = m_bDoOpAsync;
	} METHOD_END

	STDMETHODIMP StartOperation(IBindCtx* /*pbcReserved*/) METHOD_BEGIN {
		ATLTRACE(_T("CDataObject::StartOperation\n"));
		m_bAsyncStarted = TRUE;
	} METHOD_END

	STDMETHODIMP InOperation(BOOL* pfInAsyncOp) METHOD_BEGIN {
		ATLTRACE(_T("CDataObject::InOperation\n"));
		*pfInAsyncOp = m_bAsyncStarted;
		return S_OK;
	} METHOD_END

	STDMETHODIMP EndOperation(HRESULT HrRes, IBindCtx* /*pbcReserved*/, DWORD dwEffects) METHOD_BEGIN {
		ATLASSERT(m_bAsyncStarted);
		// If we were moving files, start nuking them now...
		CComPtr<IUnknown> spKeepAlive = GetUnknown();
		if (SUCCEEDED(HrRes))
			PasteSucceeded(dwEffects);
		m_bAsyncStarted = false;
	} METHOD_END


	int FindDataObject(const FORMATETC* pFormatetc) const;
	void CopyStgMedium(STGMEDIUM* pMedDest, const STGMEDIUM* pMedSrc, const FORMATETC* pFmtSrc) const;
	void PasteSucceeded(DWORD dwEffect);
	void AddRefStgMedium(STGMEDIUM* pMedDest, const STGMEDIUM* pMedSrc, const FORMATETC* pFmtSrc);

	/*




	HRESULT _CollectFolder(CAdfVolume& vol, LPCTSTR pstrPrefix, LPCTSTR pstrFolderName) {
		ATLASSERT(pstrPrefix);
		ATLASSERT(pstrFolderName);
		// Change into the folder
		if (vol.ChangeDirectory(pstrFolderName) == FALSE) return E_UNEXPECTED;
		// Get new folder (filename) prefix
		TCHAR szPrefix[MAXPATHLEN + 1] = { 0 };
		::wnsprintf(szPrefix, MAXPATHLEN, _T("%s%s\\"), pstrPrefix, pstrFolderName);
		// Scan folder list...
		CAdfDirList dir;
		if (vol.GetCurrentDirctory(dir) == FALSE) return E_FAIL;
		const List* list = dir;
		while (list != NULL) {
			USES_CONVERSION;
			const struct Entry* ent = (struct Entry*)list->content;
			LPCTSTR pstrName = A2CT(ent->name);
			_CollectFile(vol, szPrefix, pstrName);
			if (ent->type == ST_DIR) _CollectFolder(vol, szPrefix, pstrName);
			list = list->next;
		}
		// Back to parent...
		vol.ChangeDirectoryParent();
		return S_OK;
	}


	*/
};

CComPtr<IShellView> CreateShellView(FsShellExt* iFolder);

} // U::VolumeShell
