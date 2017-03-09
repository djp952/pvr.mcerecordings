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

#include "stdafx.h"

// todo: clean these up
#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <sstream>
#include <type_traits>
#include <vector>

#include <version.h>
#include <xbmc_pvr_dll.h>

#include "addoncallbacks.h"
#include "pvrcallbacks.h"
#include "scheduler.h"
#include "string_exception.h"

#pragma warning(push, 4)				// Enable maximum compiler warnings

//---------------------------------------------------------------------------
// FUNCTION PROTOTYPES
//---------------------------------------------------------------------------

// Exception helpers
//
static void handle_generalexception(char const* function);
template<typename _result> static _result handle_generalexception(char const* function, _result result);
static void handle_stdexception(char const* function, std::exception const& ex);
template<typename _result> static _result handle_stdexception(char const* function, std::exception const& ex, _result result);

// Log helpers
//
template<typename... _args> static void log_debug(_args&&... args);
template<typename... _args> static void log_error(_args&&... args);
template<typename... _args> static void log_info(_args&&... args);
template<typename... _args>	static void log_message(addoncallbacks::addon_log_t level, _args&&... args);
template<typename... _args> static void log_notice(_args&&... args);

// Scheduled Tasks
//
static void discover_recordings_task(void);

//---------------------------------------------------------------------------
// TYPE DECLARATIONS
//---------------------------------------------------------------------------

// addon_settings
//
// Defines all of the configurable addon settings
struct addon_settings {

	// Path to the Media Center RecordedTV folder
	//
	std::string recordedtv_folder;
};

//---------------------------------------------------------------------------
// GLOBAL VARIABLES
//---------------------------------------------------------------------------

// g_addon
//
// Kodi add-on callback implementation
static std::unique_ptr<addoncallbacks> g_addon;

// g_capabilities (const)
//
// PVR implementation capability flags
static const PVR_ADDON_CAPABILITIES g_capabilities = {

	false,		// bSupportsEPG
	false,		// bSupportsTV
	false,		// bSupportsRadio
	true,		// bSupportsRecordings
	false,		// bSupportsRecordingsUndelete
	false,		// bSupportsTimers
	false,		// bSupportsChannelGroups
	false,		// bSupportsChannelScan
	false,		// bSupportsChannelSettings
	false,		// bHandlesInputStream
	false,		// bHandlesDemuxing
	false,		// bSupportsRecordingPlayCount
	false,		// bSupportsLastPlayedPosition
	false,		// bSupportsRecordingEdl
};

// g_pvr
//
// Kodi PVR add-on callback implementation
static std::unique_ptr<pvrcallbacks> g_pvr;

// g_scheduler
//
// Task scheduler
static scheduler g_scheduler([](std::exception const& ex) -> void { handle_stdexception("scheduled task", ex); });

// g_settings
//
// Global addon settings instance
static addon_settings g_settings;

// g_settings_lock
//
// Synchronization object to serialize access to addon settings
std::mutex g_settings_lock;

//---------------------------------------------------------------------------
// HELPER FUNCTIONS
//---------------------------------------------------------------------------

// discover_recordings_task
//
// Scheduled task implementation to discover the storage recordings
static void discover_recordings_task(void)
{
	// todo: implement me
	assert(g_addon && g_pvr);
	log_notice(__func__, ": initiated windows media center recording discovery");
}

// get_filetime
//
// Retrieves a VT_FILETIME property from an IPropertyStore instance
static FILETIME get_filetime(IPropertyStore* store, PROPERTYKEY const& key)
{
	PROPVARIANT				value;				// PROPVARIANT value
	FILETIME				result{0, 0};		// Result from this function

	assert(store);
	PropVariantInit(&value);

	// Attempt to retrieve the value from the property store and set the result if successful
	if(SUCCEEDED(store->GetValue(key, &value)) && (value.vt == VT_FILETIME)) result = value.filetime;

	PropVariantClear(&value);
	return result;
}

// get_lpwstr
//
// Retrieves a VT_LPWSTR property from an IPropertyStore instance
static std::wstring get_lpwstr(IPropertyStore* store, PROPERTYKEY const& key)
{
	PROPVARIANT				value;			// PROPVARIANT value
	std::wstring			result;			// Result from this function

	assert(store);
	PropVariantInit(&value);

	// Attempt to retrieve the value from the property store and set the result if successful
	if(SUCCEEDED(store->GetValue(key, &value)) && (value.vt == VT_LPWSTR)) result = std::wstring(value.pwszVal);

	PropVariantClear(&value);
	return result;
}

// get_ui4
//
// Retrieves a VT_UI4 property from an IPropertyStore instance
static uint32_t get_ui4(IPropertyStore* store, PROPERTYKEY const& key)
{
	PROPVARIANT				value;				// PROPVARIANT value
	uint32_t				result = 0;			// Result from this function

	assert(store);
	PropVariantInit(&value);

	// Attempt to retrieve the value from the property store and set the result if successful
	if(SUCCEEDED(store->GetValue(key, &value)) && (value.vt == VT_UI4)) result = value.ulVal;

	PropVariantClear(&value);
	return result;
}

// get_ui8
//
// Retrieves a VT_UI8 property from an IPropertyStore instance
static uint64_t get_ui8(IPropertyStore* store, PROPERTYKEY const& key)
{
	PROPVARIANT				value;				// PROPVARIANT value
	uint64_t				result = 0;			// Result from this function

	assert(store);
	PropVariantInit(&value);

	// Attempt to retrieve the value from the property store and set the result if successful
	if(SUCCEEDED(store->GetValue(key, &value)) && (value.vt == VT_UI8)) result = value.uhVal.QuadPart;

	PropVariantClear(&value);
	return result;
}

// handle_generalexception
//
// Handler for thrown generic exceptions
static void handle_generalexception(char const* function)
{
	log_error(function, " failed due to an unhandled exception");
}

// handle_generalexception
//
// Handler for thrown generic exceptions
template<typename _result>
static _result handle_generalexception(char const* function, _result result)
{
	handle_generalexception(function);
	return result;
}

// handle_stdexception
//
// Handler for thrown std::exceptions
static void handle_stdexception(char const* function, std::exception const& ex)
{
	log_error(function, " failed due to an unhandled exception: ", ex.what());
}

// handle_stdexception
//
// Handler for thrown std::exceptions
template<typename _result>
static _result handle_stdexception(char const* function, std::exception const& ex, _result result)
{
	handle_stdexception(function, ex);
	return result;
}

// log_debug
//
// Variadic method of writing a LOG_DEBUG entry into the Kodi application log
template<typename... _args>
static void log_debug(_args&&... args)
{
	log_message(addoncallbacks::addon_log_t::LOG_DEBUG, std::forward<_args>(args)...);
}

// log_error
//
// Variadic method of writing a LOG_ERROR entry into the Kodi application log
template<typename... _args>
static void log_error(_args&&... args)
{
	log_message(addoncallbacks::addon_log_t::LOG_ERROR, std::forward<_args>(args)...);
}

// log_info
//
// Variadic method of writing a LOG_INFO entry into the Kodi application log
template<typename... _args>
static void log_info(_args&&... args)
{
	log_message(addoncallbacks::addon_log_t::LOG_INFO, std::forward<_args>(args)...);
}

// log_message
//
// Variadic method of writing an entry into the Kodi application log
template<typename... _args>
static void log_message(addoncallbacks::addon_log_t level, _args&&... args)
{
	std::ostringstream stream;
	int unpack[] = {0, ( static_cast<void>(stream << args), 0 ) ... };
	(void)unpack;

	if(g_addon) g_addon->Log(level, stream.str().c_str());

	// Write LOG_ERROR level messages to an appropriate secondary log mechanism
	if(level == addoncallbacks::addon_log_t::LOG_ERROR) {

		std::string message = "ERROR: " + stream.str() + "\r\n";
		OutputDebugStringA(message.c_str());
	}
}

// log_notice
//
// Variadic method of writing a LOG_NOTICE entry into the Kodi application log
template<typename... _args>
static void log_notice(_args&&... args)
{
	log_message(addoncallbacks::addon_log_t::LOG_NOTICE, std::forward<_args>(args)...);
}

// smb_to_unc
//
// Converts an smb:// scheme path into a UNC path
static std::string smb_to_unc(char const* smb)
{
	if(smb == nullptr) return std::string();

	// Check if this is an smb:// path
	if(_strnicmp(smb, "smb://", 6) == 0) {

		// Repalce smb:// with \\ and replace slashes with backslashes
		std::string unc = std::string("\\\\") + &smb[6];
		std::replace(unc.begin(), unc.end(), '/', '\\'); 
		return unc;
	}

	else return std::string(smb);
}

// to_wstring
//
// Converts a UTF-8 character string into a UTF-16 std::wstring
static std::wstring to_wstring(char const* psz, int cch)
{
	if(psz == nullptr) return std::wstring();

	// Create a buffer big enough to hold the converted string data and convert it
	int buffercch = MultiByteToWideChar(CP_UTF8, 0, psz, cch, nullptr, 0);
	auto buffer = std::make_unique<wchar_t[]>(buffercch);
	MultiByteToWideChar(CP_UTF8, 0, psz, cch, buffer.get(), buffercch);

	// Construct an std::wstring around the converted character data, watch for the API
	// returning the length including the NULL character when -1 was provided as length
	return std::wstring(buffer.get(), (cch == -1) ? buffercch - 1 : buffercch);
}

//---------------------------------------------------------------------------
// KODI ADDON ENTRY POINTS
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// ADDON_Create
//
// Creates and initializes the Kodi addon instance
//
// Arguments:
//
//	handle			- Kodi add-on handle
//	props			- Add-on specific properties structure (PVR_PROPERTIES)

ADDON_STATUS ADDON_Create(void* handle, void* /*props*/)
{
	if(handle == nullptr) return ADDON_STATUS::ADDON_STATUS_PERMANENT_FAILURE;

	try {
		
		// Create the global addoncallbacks instance
		g_addon = std::make_unique<addoncallbacks>(handle);

		// Throw a banner out to the Kodi log indicating that the add-on is being loaded
		log_notice(VERSION_PRODUCTNAME_ANSI, " v", VERSION_VERSION3_ANSI, " loading");

		try { 

			//
			// todo: load settings here
			//

			// Create the global pvrcallbacks instance
			g_pvr = std::make_unique<pvrcallbacks>(handle); 
		
			try {

				// Schedule the initial discovery run to execute as soon as possible
				std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
				g_scheduler.add(now + std::chrono::seconds(1), discover_recordings_task);

				g_scheduler.start();				// <--- Start the task scheduler
			}
			
			// Clean up the pvrcallbacks instance on exception
			catch(...) { g_pvr.reset(nullptr); throw; }
		}

		// Clean up the addoncallbacks on exception; but log the error first -- once the callbacks
		// are destroyed so is the ability to write to the Kodi log file
		catch(std::exception& ex) { handle_stdexception(__func__, ex); g_addon.reset(nullptr); throw; }
		catch(...) { handle_generalexception(__func__); g_addon.reset(nullptr); throw; }
	}

	// Anything that escapes above can't be logged at this point, just return ADDON_STATUS_PERMANENT_FAILURE
	catch(...) { return ADDON_STATUS::ADDON_STATUS_PERMANENT_FAILURE; }

	// Throw a simple banner out to the Kodi log indicating that the add-on has been loaded
	log_notice(VERSION_PRODUCTNAME_ANSI, " v", VERSION_VERSION3_ANSI, " loaded");

	return ADDON_STATUS::ADDON_STATUS_OK;
}

//---------------------------------------------------------------------------
// ADDON_Stop
//
// Instructs the addon to stop all activities
//
// Arguments:
//
//	NONE

void ADDON_Stop(void)
{
	g_scheduler.stop();				// Stop the task scheduler
}

//---------------------------------------------------------------------------
// ADDON_Destroy
//
// Destroys the Kodi addon instance
//
// Arguments:
//
//	NONE

void ADDON_Destroy(void)
{
	// Throw a message out to the Kodi log indicating that the add-on is being unloaded
	log_notice(VERSION_PRODUCTNAME_ANSI, " v", VERSION_VERSION3_ANSI, " unloading");

	// Stop the task scheduler
	g_scheduler.stop();

	// Destroy all the dynamically created objects
	g_pvr.reset(nullptr);
	g_addon.reset(nullptr);
}

//---------------------------------------------------------------------------
// ADDON_GetStatus
//
// Gets the current status of the Kodi addon
//
// Arguments:
//
//	NONE

ADDON_STATUS ADDON_GetStatus(void)
{
	return ADDON_STATUS::ADDON_STATUS_OK;
}

//---------------------------------------------------------------------------
// ADDON_HasSettings
//
// Indicates if the Kodi addon has settings or not
//
// Arguments:
//
//	NONE

bool ADDON_HasSettings(void)
{
	return true;
}

//---------------------------------------------------------------------------
// ADDON_GetSettings
//
// Acquires the information about the Kodi addon settings
//
// Arguments:
//
//	settings		- Structure to receive the addon settings

unsigned int ADDON_GetSettings(ADDON_StructSetting*** /*settings*/)
{
	return 0;
}

//---------------------------------------------------------------------------
// ADDON_SetSetting
//
// Changes the value of a named Kodi addon setting
//
// Arguments:
//
//	name		- Name of the setting to change
//	value		- New value of the setting to apply

ADDON_STATUS ADDON_SetSetting(char const* name, void const* value)
{
	// todo: implement me
	UNREFERENCED_PARAMETER(name);
	UNREFERENCED_PARAMETER(value);
	return ADDON_STATUS_OK;
}

//---------------------------------------------------------------------------
// ADDON_FreeSettings
//
// Releases settings allocated by ADDON_GetSettings
//
// Arguments:
//
//	NONE

void ADDON_FreeSettings(void)
{
}

//---------------------------------------------------------------------------
// KODI PVR ADDON ENTRY POINTS
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// GetPVRAPIVersion
//
// Get the XBMC_PVR_API_VERSION that was used to compile this add-on
//
// Arguments:
//
//	NONE

char const* GetPVRAPIVersion(void)
{
	return XBMC_PVR_API_VERSION;
}

//---------------------------------------------------------------------------
// GetMininumPVRAPIVersion
//
// Get the XBMC_PVR_MIN_API_VERSION that was used to compile this add-on
//
// Arguments:
//
//	NONE

char const* GetMininumPVRAPIVersion(void)
{
	return XBMC_PVR_MIN_API_VERSION;
}

//---------------------------------------------------------------------------
// GetGUIAPIVersion
//
// Get the XBMC_GUI_API_VERSION that was used to compile this add-on
//
// Arguments:
//
//	NONE

char const* GetGUIAPIVersion(void)
{
	return "";
}

//---------------------------------------------------------------------------
// GetMininumGUIAPIVersion
//
// Get the XBMC_GUI_MIN_API_VERSION that was used to compile this add-on
//
// Arguments:
//
//	NONE

char const* GetMininumGUIAPIVersion(void)
{
	return "";
}

//---------------------------------------------------------------------------
// GetAddonCapabilities
//
// Get the list of features that this add-on provides
//
// Arguments:
//
//	capabilities	- Capabilities structure to fill out

PVR_ERROR GetAddonCapabilities(PVR_ADDON_CAPABILITIES *capabilities)
{
	if(capabilities == nullptr) return PVR_ERROR::PVR_ERROR_INVALID_PARAMETERS;
	*capabilities = g_capabilities;
	
	return PVR_ERROR::PVR_ERROR_NO_ERROR;
}

//---------------------------------------------------------------------------
// GetBackendName
//
// Get the name reported by the backend that will be displayed in the UI
//
// Arguments:
//
//	NONE

char const* GetBackendName(void)
{
	return VERSION_PRODUCTNAME_ANSI;
}

//---------------------------------------------------------------------------
// GetBackendVersion
//
// Get the version string reported by the backend that will be displayed in the UI
//
// Arguments:
//
//	NONE

char const* GetBackendVersion(void)
{
	return VERSION_VERSION3_ANSI;
}

//---------------------------------------------------------------------------
// GetConnectionString
//
// Get the connection string reported by the backend that will be displayed in the UI
//
// Arguments:
//
//	NONE

char const* GetConnectionString(void)
{
	return "conected";
}

//---------------------------------------------------------------------------
// GetDriveSpace
//
// Get the disk space reported by the backend (if supported)
//
// Arguments:
//
//	total		- The total disk space in bytes
//	used		- The used disk space in bytes

PVR_ERROR GetDriveSpace(long long* /*total*/, long long* /*used*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// CallMenuHook
//
// Call one of the menu hooks (if supported)
//
// Arguments:
//
//	menuhook	- The hook to call
//	item		- The selected item for which the hook is called

PVR_ERROR CallMenuHook(PVR_MENUHOOK const& /*menuhook*/, PVR_MENUHOOK_DATA const& /*item*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// GetEPGForChannel
//
// Request the EPG for a channel from the backend
//
// Arguments:
//
//	handle		- Handle to pass to the callback method
//	channel		- The channel to get the EPG table for
//	start		- Get events after this time (UTC)
//	end			- Get events before this time (UTC)

PVR_ERROR GetEPGForChannel(ADDON_HANDLE /*handle*/, PVR_CHANNEL const& /*channel*/, time_t /*start*/, time_t /*end*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// GetChannelGroupsAmount
//
// Get the total amount of channel groups on the backend if it supports channel groups
//
// Arguments:
//
//	NONE

int GetChannelGroupsAmount(void)
{
	return -1;
}

//---------------------------------------------------------------------------
// GetChannelGroups
//
// Request the list of all channel groups from the backend if it supports channel groups
//
// Arguments:
//
//	handle		- Handle to pass to the callack method
//	radio		- True to get radio groups, false to get TV channel groups

PVR_ERROR GetChannelGroups(ADDON_HANDLE /*handle*/, bool /*radio*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// GetChannelGroupMembers
//
// Request the list of all channel groups from the backend if it supports channel groups
//
// Arguments:
//
//	handle		- Handle to pass to the callack method
//	group		- The group to get the members for

PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE /*handle*/, PVR_CHANNEL_GROUP const& /*group*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// OpenDialogChannelScan
//
// Show the channel scan dialog if this backend supports it
//
// Arguments:
//
//	NONE

PVR_ERROR OpenDialogChannelScan(void)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// GetChannelsAmount
//
// The total amount of channels on the backend, or -1 on error
//
// Arguments:
//
//	NONE

int GetChannelsAmount(void)
{
	return -1;
}

//---------------------------------------------------------------------------
// GetChannels
//
// Request the list of all channels from the backend
//
// Arguments:
//
//	handle		- Handle to pass to the callback method
//	radio		- True to get radio channels, false to get TV channels

PVR_ERROR GetChannels(ADDON_HANDLE /*handle*/, bool /*radio*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// DeleteChannel
//
// Delete a channel from the backend
//
// Arguments:
//
//	channel		- The channel to delete

PVR_ERROR DeleteChannel(PVR_CHANNEL const& /*channel*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// RenameChannel
//
// Rename a channel on the backend
//
// Arguments:
//
//	channel		- The channel to rename, containing the new channel name

PVR_ERROR RenameChannel(PVR_CHANNEL const& /*channel*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// MoveChannel
//
// Move a channel to another channel number on the backend
//
// Arguments:
//
//	channel		- The channel to move, containing the new channel number

PVR_ERROR MoveChannel(PVR_CHANNEL const& /*channel*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// OpenDialogChannelSettings
//
// Show the channel settings dialog, if supported by the backend
//
// Arguments:
//
//	channel		- The channel to show the dialog for

PVR_ERROR OpenDialogChannelSettings(PVR_CHANNEL const& /*channel*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// OpenDialogChannelAdd
//
// Show the dialog to add a channel on the backend, if supported by the backend
//
// Arguments:
//
//	channel		- The channel to add

PVR_ERROR OpenDialogChannelAdd(PVR_CHANNEL const& /*channel*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// GetRecordingsAmount
//
// The total amount of recordings on the backend or -1 on error
//
// Arguments:
//
//	deleted		- if set return deleted recording

int GetRecordingsAmount(bool deleted)
{
	if(deleted) return 0;			// Deleted recordings aren't supported

	// todo: implement me
	return 0;
}

//---------------------------------------------------------------------------
// GetRecordings
//
// Request the list of all recordings from the backend, if supported
//
// Arguments:
//
//	handle		- Handle to pass to the callback method
//	deleted		- If set return deleted recording

PVR_ERROR GetRecordings(ADDON_HANDLE handle, bool deleted)
{
	assert(g_pvr);				

	if(handle == nullptr) return PVR_ERROR::PVR_ERROR_INVALID_PARAMETERS;

	// The PVR doesn't support tracking deleted recordings
	if(deleted) return PVR_ERROR::PVR_ERROR_NO_ERROR;

	// todo: implement me
	return PVR_ERROR::PVR_ERROR_NO_ERROR;
}

//---------------------------------------------------------------------------
// DeleteRecording
//
// Delete a recording on the backend
//
// Arguments:
//
//	recording	- The recording to delete

PVR_ERROR DeleteRecording(PVR_RECORDING const& recording)
{
	// todo: implement me
	UNREFERENCED_PARAMETER(recording);
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// UndeleteRecording
//
// Undelete a recording on the backend
//
// Arguments:
//
//	recording	- The recording to undelete

PVR_ERROR UndeleteRecording(PVR_RECORDING const& /*recording*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// DeleteAllRecordingsFromTrash
//
// Delete all recordings permanent which in the deleted folder on the backend
//
// Arguments:
//
//	NONE

PVR_ERROR DeleteAllRecordingsFromTrash()
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// RenameRecording
//
// Rename a recording on the backend
//
// Arguments:
//
//	recording	- The recording to rename, containing the new name

PVR_ERROR RenameRecording(PVR_RECORDING const& /*recording*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// SetRecordingPlayCount
//
// Set the play count of a recording on the backend
//
// Arguments:
//
//	recording	- The recording to change the play count
//	playcount	- Play count

PVR_ERROR SetRecordingPlayCount(PVR_RECORDING const& /*recording*/, int /*playcount*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// SetRecordingLastPlayedPosition
//
// Set the last watched position of a recording on the backend
//
// Arguments:
//
//	recording			- The recording
//	lastplayedposition	- The last watched position in seconds

PVR_ERROR SetRecordingLastPlayedPosition(PVR_RECORDING const& /*recording*/, int /*lastplayedposition*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// GetRecordingLastPlayedPosition
//
// Retrieve the last watched position of a recording (in seconds) on the backend
//
// Arguments:
//
//	recording	- The recording

int GetRecordingLastPlayedPosition(PVR_RECORDING const& /*recording*/)
{
	return -1;
}

//---------------------------------------------------------------------------
// GetRecordingEdl
//
// Retrieve the edit decision list (EDL) of a recording on the backend
//
// Arguments:
//
//	recording	- The recording
//	edl			- The function has to write the EDL list into this array
//	count		- in: The maximum size of the EDL, out: the actual size of the EDL

PVR_ERROR GetRecordingEdl(PVR_RECORDING const& /*recording*/, PVR_EDL_ENTRY edl[], int* count)
{
	if(count == nullptr) return PVR_ERROR::PVR_ERROR_INVALID_PARAMETERS;
	if((*count) && (edl == nullptr)) return PVR_ERROR::PVR_ERROR_INVALID_PARAMETERS;

	*count = 0;

	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// GetTimerTypes
//
// Retrieve the timer types supported by the backend
//
// Arguments:
//
//	types		- The function has to write the definition of the supported timer types into this array
//	count		- in: The maximum size of the list; out: the actual size of the list

PVR_ERROR GetTimerTypes(PVR_TIMER_TYPE[] /*types*/, int* /*count*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// GetTimersAmount
//
// Gets the total amount of timers on the backend or -1 on error
//
// Arguments:
//
//	NONE

int GetTimersAmount(void)
{
	return -1;
}

//---------------------------------------------------------------------------
// GetTimers
//
// Request the list of all timers from the backend if supported
//
// Arguments:
//
//	handle		- Handle to pass to the callback method

PVR_ERROR GetTimers(ADDON_HANDLE /*handle*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// AddTimer
//
// Add a timer on the backend
//
// Arguments:
//
//	timer	- The timer to add

PVR_ERROR AddTimer(PVR_TIMER const& /*timer*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// DeleteTimer
//
// Delete a timer on the backend
//
// Arguments:
//
//	timer	- The timer to delete
//	force	- Set to true to delete a timer that is currently recording a program

PVR_ERROR DeleteTimer(PVR_TIMER const& /*timer*/, bool /*force*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// UpdateTimer
//
// Update the timer information on the backend
//
// Arguments:
//
//	timer	- The timer to update

PVR_ERROR UpdateTimer(PVR_TIMER const& /*timer*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// OpenLiveStream
//
// Open a live stream on the backend
//
// Arguments:
//
//	channel		- The channel to stream

bool OpenLiveStream(PVR_CHANNEL const& /*channel*/)
{
	return false;
}

//---------------------------------------------------------------------------
// CloseLiveStream
//
// Closes the live stream
//
// Arguments:
//
//	NONE

void CloseLiveStream(void)
{
}

//---------------------------------------------------------------------------
// ReadLiveStream
//
// Read from an open live stream
//
// Arguments:
//
//	buffer		- The buffer to store the data in
//	size		- The number of bytes to read into the buffer

int ReadLiveStream(unsigned char* /*buffer*/, unsigned int /*size*/)
{
	return -1;
}

//---------------------------------------------------------------------------
// SeekLiveStream
//
// Seek in a live stream on a backend that supports timeshifting
//
// Arguments:
//
//	position	- Delta within the stream to seek, relative to whence
//	whence		- Starting position from which to apply the delta

long long SeekLiveStream(long long /*position*/, int /*whence*/)
{
	return -1;
}

//---------------------------------------------------------------------------
// PositionLiveStream
//
// Gets the position in the stream that's currently being read
//
// Arguments:
//
//	NONE

long long PositionLiveStream(void)
{
	return -1;
}

//---------------------------------------------------------------------------
// LengthLiveStream
//
// The total length of the stream that's currently being read
//
// Arguments:
//
//	NONE

long long LengthLiveStream(void)
{
	return -1;
}

//---------------------------------------------------------------------------
// SwitchChannel
//
// Switch to another channel. Only to be called when a live stream has already been opened
//
// Arguments:
//
//	channel		- The channel to switch to

bool SwitchChannel(PVR_CHANNEL const& /*channel*/)
{
	return false;
}

//---------------------------------------------------------------------------
// SignalStatus
//
// Get the signal status of the stream that's currently open
//
// Arguments:
//
//	status		- The signal status

PVR_ERROR SignalStatus(PVR_SIGNAL_STATUS& /*status*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// GetLiveStreamURL
//
// Get the stream URL for a channel from the backend
//
// Arguments:
//
//	channel		- The channel to get the stream URL for

char const* GetLiveStreamURL(PVR_CHANNEL const& /*channel*/)
{
	return nullptr;
}

//---------------------------------------------------------------------------
// GetStreamProperties
//
// Get the stream properties of the stream that's currently being read
//
// Arguments:
//
//	properties	- The properties of the currently playing stream

PVR_ERROR GetStreamProperties(PVR_STREAM_PROPERTIES* properties)
{
	if(properties == nullptr) return PVR_ERROR::PVR_ERROR_INVALID_PARAMETERS;

	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// OpenRecordedStream
//
// Open a stream to a recording on the backend
//
// Arguments:
//
//	recording	- The recording to open

bool OpenRecordedStream(PVR_RECORDING const& /*recording*/)
{
	return false;
}

//---------------------------------------------------------------------------
// CloseRecordedStream
//
// Close an open stream from a recording
//
// Arguments:
//
//	NONE

void CloseRecordedStream(void)
{
}

//---------------------------------------------------------------------------
// ReadRecordedStream
//
// Read from a recording
//
// Arguments:
//
//	buffer		- The buffer to store the data in
//	size		- The number of bytes to read into the buffer

int ReadRecordedStream(unsigned char* /*buffer*/, unsigned int /*size*/)
{
	return -1;
}

//---------------------------------------------------------------------------
// SeekRecordedStream
//
// Seek in a recorded stream
//
// Arguments:
//
//	position	- Delta within the stream to seek, relative to whence
//	whence		- Starting position from which to apply the delta

long long SeekRecordedStream(long long /*position*/, int /*whence*/)
{
	return -1;
}

//---------------------------------------------------------------------------
// PositionRecordedStream
//
// Gets the position in the stream that's currently being read
//
// Arguments:
//
//	NONE

long long PositionRecordedStream(void)
{
	return -1;
}

//---------------------------------------------------------------------------
// PositionRecordedStream
//
// Gets the total length of the stream that's currently being read
//
// Arguments:
//
//	NONE

long long LengthRecordedStream(void)
{
	return -1;
}

//---------------------------------------------------------------------------
// DemuxReset
//
// Reset the demultiplexer in the add-on
//
// Arguments:
//
//	NONE

void DemuxReset(void)
{
}

//---------------------------------------------------------------------------
// DemuxAbort
//
// Abort the demultiplexer thread in the add-on
//
// Arguments:
//
//	NONE

void DemuxAbort(void)
{
}

//---------------------------------------------------------------------------
// DemuxFlush
//
// Flush all data that's currently in the demultiplexer buffer in the add-on
//
// Arguments:
//
//	NONE

void DemuxFlush(void)
{
}

//---------------------------------------------------------------------------
// DemuxRead
//
// Read the next packet from the demultiplexer, if there is one
//
// Arguments:
//
//	NONE

DemuxPacket* DemuxRead(void)
{
	return nullptr;
}

//---------------------------------------------------------------------------
// GetChannelSwitchDelay
//
// Gets delay to use when using switching channels for add-ons not providing an input stream
//
// Arguments:
//
//	NONE

unsigned int GetChannelSwitchDelay(void)
{
	return 0;
}

//---------------------------------------------------------------------------
// CanPauseStream
//
// Check if the backend support pausing the currently playing stream
//
// Arguments:
//
//	NONE

bool CanPauseStream(void)
{
	return false;
}

//---------------------------------------------------------------------------
// CanSeekStream
//
// Check if the backend supports seeking for the currently playing stream
//
// Arguments:
//
//	NONE

bool CanSeekStream(void)
{
	return false;
}

//---------------------------------------------------------------------------
// PauseStream
//
// Notify the pvr addon that XBMC (un)paused the currently playing stream
//
// Arguments:
//
//	paused		- Paused/unpaused flag

void PauseStream(bool /*paused*/)
{
}

//---------------------------------------------------------------------------
// SeekTime
//
// Notify the pvr addon/demuxer that XBMC wishes to seek the stream by time
//
// Arguments:
//
//	time		- The absolute time since stream start
//	backwards	- True to seek to keyframe BEFORE time, else AFTER
//	startpts	- Can be updated to point to where display should start

bool SeekTime(double /*time*/, bool /*backwards*/, double* startpts)
{
	if(startpts == nullptr) return false;

	return false;
}

//---------------------------------------------------------------------------
// SetSpeed
//
// Notify the pvr addon/demuxer that XBMC wishes to change playback speed
//
// Arguments:
//
//	speed		- The requested playback speed

void SetSpeed(int /*speed*/)
{
}

//---------------------------------------------------------------------------
// GetPlayingTime
//
// Get actual playing time from addon. With timeshift enabled this is different to live
//
// Arguments:
//
//	NONE

time_t GetPlayingTime(void)
{
	return 0;
}

//---------------------------------------------------------------------------
// GetBufferTimeStart
//
// Get time of oldest packet in timeshift buffer (UTC)
//
// Arguments:
//
//	NONE

time_t GetBufferTimeStart(void)
{
	return 0;
}

//---------------------------------------------------------------------------
// GetBufferTimeEnd
//
// Get time of latest packet in timeshift buffer (UTC)
//
// Arguments:
//
//	NONE

time_t GetBufferTimeEnd(void)
{
	return 0;
}

//---------------------------------------------------------------------------
// GetBackendHostname
//
// Get the hostname of the pvr backend server
//
// Arguments:
//
//	NONE

char const* GetBackendHostname(void)
{
	return "";
}

//---------------------------------------------------------------------------
// IsTimeshifting
//
// Check if timeshift is active
//
// Arguments:
//
//	NONE

bool IsTimeshifting(void)
{
	return false;
}

//---------------------------------------------------------------------------
// IsRealTimeStream
//
// Check for real-time streaming
//
// Arguments:
//
//	NONE

bool IsRealTimeStream(void)
{
	return false;
}

//---------------------------------------------------------------------------
// SetEPGTimeFrame
//
// Tell the client the time frame to use when notifying epg events back to Kodi
//
// Arguments:
//
//	days	- number of days from "now". EPG_TIMEFRAME_UNLIMITED means that Kodi is interested in all epg events

PVR_ERROR SetEPGTimeFrame(int /*days*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// OnSystemSleep
//
// Notification of system sleep power event
//
// Arguments:
//
//	NONE

void OnSystemSleep()
{
	try {

		g_scheduler.stop();					// Stop the scheduler
		g_scheduler.clear();				// Clear out any pending tasks
	}

	catch(std::exception& ex) { return handle_stdexception(__func__, ex); }
	catch(...) { return handle_generalexception(__func__); }
}

//---------------------------------------------------------------------------
// OnSystemWake
//
// Notification of system wake power event
//
// Arguments:
//
//	NONE

void OnSystemWake()
{
	try {

		// Reschedule the discovery to update everything
		std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
		g_scheduler.add(now + std::chrono::seconds(1), discover_recordings_task);
	
		// Restart the scheduler
		g_scheduler.start();
	}

	catch(std::exception& ex) { return handle_stdexception(__func__, ex); }
	catch(...) { return handle_generalexception(__func__); }
}

//---------------------------------------------------------------------------
// OnPowerSavingActivated
//
// Notification of system power saving activation event
//
// Arguments:
//
//	NONE

void OnPowerSavingActivated()
{
}

//---------------------------------------------------------------------------
// OnPowerSavingDeactivated
//
// Notification of system power saving deactivation event
//
// Arguments:
//
//	NONE

void OnPowerSavingDeactivated()
{
}

//---------------------------------------------------------------------------

#pragma warning(pop)