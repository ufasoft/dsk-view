#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "libext.lib")
#pragma comment(lib, "gui.lib")

#ifdef _DEBUG
#	pragma comment(lib, "libcmtd")
#	pragma comment(lib, "libucrtd")
#else
#	pragma comment(lib, "libcmt")
#	pragma comment(lib, "libucrt")
#endif

