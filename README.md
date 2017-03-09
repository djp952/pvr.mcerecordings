#__pvr.mcerecordings__  
   
Windows Media Center Recorded TV PVR Client
   
Copyright (C)2017 Michael G. Brehm    
[MIT LICENSE](https://opensource.org/licenses/MIT)   
   
**BUILD ENVIRONMENT**  
* Windows 10 x64 15025   
* Visual Studio 2015 (with Git for Windows)   
   
**BUILD**   
Open "Developer Command Prompt for VS2015"   
```
git clone https://github.com/djp952/pvr.mcerecordings
cd pvr.mcerecordings
git submodule update --init
msbuild msbuild.proj

> out\zuki.pvr.mcerecordings-win32-krypton-x.x.x.x.zip (windows-Win32)
```
