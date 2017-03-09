//---------------------------------------------------------------------------
// Copyright (c) 2017 Michael G. Brehm
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//---------------------------------------------------------------------------

#ifndef __STDAFX_H_
#define __STDAFX_H_
#pragma once

//---------------------------------------------------------------------------
// Win32 Declarations

#define WINVER			_WIN32_WINNT_WIN8
#define	_WIN32_WINNT	_WIN32_WINNT_WIN8
#define	_WIN32_IE		_WIN32_IE_IE80

#define NOMINMAX					// Disable min()/max() macros
#define _USE_32BIT_TIME_T			// time_t has to be 32 bit for Kodi

#include <windows.h>				// Include main Windows declarations
#include <Shlwapi.h>				// Include shell utility functions
#include <ShObjIdl.h>				// Include shell object COM declarations
#include <propkey.h>				// Include property key declarations
#include <PathCch.h>				// Include PathCch helper declarations

#include <assert.h>					// Include standard assertion declarations
#include <stdint.h>					// Include standard integer declarations

// KiB / MiB / GiB
//
#define KiB		*(1 << 10)		// KiB multiplier
#define MiB		*(1 << 20)		// MiB multiplier
#define GiB		*(1 << 30)		// GiB multiplier

//---------------------------------------------------------------------------
// Kodi Addon Declarations

#ifdef _WINDOWS
#define TARGET_WINDOWS
#endif // _WINDOWS

#include <xbmc_addon_dll.h>			// Include general addon declarations

//---------------------------------------------------------------------------

#endif	// __STDAFX_H_
