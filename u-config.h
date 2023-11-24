#define UCFG_AFXDLL 0
#define UCFG_STDSTL 1

#define _WIN32_WINNT 0xA00


#define _STL_CALL_ABORT_INSTEAD_OF_INVALID_PARAMETER

#define _ACRTIMP_ALT

#define __STDC_WANT_SECURE_LIB__ 1


#if !defined(_CPPRT) && !defined(_ELRT)
#	define UCFG_ATL 'S'
#endif

#define UCFG_ASSERT 0
#define UCFG_ATL_ASSERT 0
#define UCFG_TRACE 0

#define _ATL_APARTMENT_THREADED
//#define _ATL_NO_WIN_SUPPORT
#define _ATL_USE_WINAPI_FAMILY_DESKTOP_APP
#define _ATL_NO_AUTOMATIC_NAMESPACE
#define _ATL_NO_SERVICE
#define _ATL_DEBUG_QI
//#define _ATL_STATIC_LIB_IMPL

#define UCFG_USE_MODULE_STATE 0

#define UCFG_ALLOCATOR 'S'
#define UCFG_LIB_DECLS 0
#define UCFG_CRT 'S'
#define UCFG_LOCALE 1
#define UCFG_USE_LOCALE 0
#define UCFG_DEFINE_NEW 0
#define UCFG_GUI 0
#define UCFG_EXTENDED 0
#define UCFG_EH_SUPPORT_IGNORE 0
#define UCFG_THREAD_MANAGEMENT 0
#define UCFG_OS_IMPTLS 1
#define UCFG_RTTI 0
//#define UCFG_STACK_TRACE 0
#define UCFG_USE_REGEX 1
#define UCFG_WND 0


#define UCFG_WINAPI_THUNKS 0
#define UCFG_ERROR_MESSAGE 0
#define UCFG_OLE 0
#define UCFG_COM 1
#define UCFG_COM_IMPLOBJ 1
#define UCFG_USE_REGISTRY 1
#define UCFG_COMPLEX_WINAPP 0

#ifdef _DEBUG
#	define UCFG_TRC 1
#else
#	define UCFG_TRC 0
#endif

#pragma warning(disable: 4541) // 'dynamic_cast'
