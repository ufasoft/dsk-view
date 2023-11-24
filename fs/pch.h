#pragma once

#define _CRT_SECURE_NO_WARNINGS
#define INITGUID

#include <el/inc/inc_configs.h>
#include <el/libext.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cguid.h>

#include <float.h>
#include <stdio.h>
#include <wchar.h>
#include <cstdlib>
#include <cstdint>
/*
#include <el/libext/win32/Shell/Shell.h>
#include <el/libext/win32/clipboard.h>
#include <el/gui/menu.h>
using namespace Ext::Win::Shell;

*/
using namespace Ext;
using namespace std;

/*
#include <chrono>
#include <fstream>
//#include <filesystem>
#include <vector>
*/

#pragma warning(disable: 26495)		// Initialize member variable
#pragma warning(disable: 26813)		// Use bitwise and


#include "file_config.h"
