# __pvr.mcerecordings__  
   
Windows Media Center Recorded TV PVR Client
   
Copyright (C)2018 Michael G. Brehm    
[MIT LICENSE](https://opensource.org/licenses/MIT)   
   
**BUILD ENVIRONMENT**  
* Visual Studio 2017 (with Git for Windows)   
   
**BUILD**   
Open "Developer Command Prompt for VS2017"   
```
git clone https://github.com/djp952/pvr.mcerecordings
cd pvr.mcerecordings
git submodule update --init
msbuild msbuild.proj

> out\zuki.pvr.mcerecordings-windows-win32-leia-x.x.x.x.zip (windows-Win32)
> out\zuki.pvr.mcerecordings-windows-x64-leia-x.x.x.x.zip (windows-x64)
```
