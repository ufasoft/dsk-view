# .DSK View

Filesystem Shell extension / Far Manager / Total Commander plugin for Retro-computer filesystems

[Project Home Page](https://ufasoft.com/dskview)


## How to Build

* Install Visual Studio 2022 (or later) with *C++ MFC for latest v143 build tools (x86 & x64)* package.
* Add path to `msbuild.exe` into `%PATH%` environment variable.
* Run:
```cmd
git clone --recursive https://github.com/ufasoft/dsk-view
msbuild dsk-view/dsk-view.sln
```

The resulting `fs11.dll` DLL is placed into `dsk-view/artifacts/` directory.
