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

#include <chrono>
#include <memory>
#include <string>
#include <sstream>
#include <vector>

#include <xbmc_addon_dll.h>
#include <xbmc_pvr_dll.h>
#include <version.h>

#include <libXBMC_addon.h>
#include <libXBMC_pvr.h>

#include "database.h"
#include "scheduler.h"
#include "scalar_condition.h"
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
template<typename... _args>	static void log_message(ADDON::addon_log_t level, _args&&... args);
template<typename... _args> static void log_notice(_args&&... args);

// Scheduled Tasks
//
static void discover_recordings_task(const scalar_condition<bool>& cancel);

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
// Kodi add-on callbacks
static std::unique_ptr<ADDON::CHelper_libXBMC_addon> g_addon;

// g_capabilities (const)
//
// PVR implementation capability flags
static const PVR_ADDON_CAPABILITIES g_capabilities = {

	false,			// bSupportsEPG
	false,			// bSupportsEPGEdl
	false,			// bSupportsTV
	false,			// bSupportsRadio
	true,			// bSupportsRecordings
	false,			// bSupportsRecordingsUndelete
	false,			// bSupportsTimers
	false,			// bSupportsChannelGroups
	false,			// bSupportsChannelScan
	false,			// bSupportsChannelSettings
	false,			// bHandlesInputStream
	false,			// bHandlesDemuxing
	false,			// bSupportsRecordingPlayCount
	false,			// bSupportsLastPlayedPosition
	false,			// bSupportsRecordingEdl
	false,			// bSupportsRecordingsRename
	false,			// bSupportsRecordingsLifetimeChange
	false,			// bSupportsDescrambleInfo
	0,				// iRecordingsLifetimesSize
	{ { 0, "" } },	// recordingsLifetimeValues
	false,			// bSupportsAsyncEPGTransfer
};

// g_connpool
//
// Global SQLite database connection pool instance
static std::shared_ptr<connectionpool> g_connpool;

// g_pvr
//
// Kodi PVR add-on callbacks
static std::unique_ptr<CHelper_libXBMC_pvr> g_pvr;

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
// Scheduled task implementation to discover the recordings
static void discover_recordings_task(const scalar_condition<bool>& cancel)
{
	bool		changed = false;			// Flag if the discovery data changed

	assert(g_addon && g_pvr);
	log_notice(__func__, ": initiated windows media center recording discovery");

	// Grab copies of the required setting(s) up front
	std::unique_lock<std::mutex> settings_lock(g_settings_lock);
	std::string recordedtv_folder = g_settings.recordedtv_folder;
	settings_lock.unlock();

	try {

		// Pull a database connection out from the connection pool
		connectionpool::handle dbhandle(g_connpool);

		// Discover the recordings available in the recordedtv_folder
		discover_recordings(dbhandle, g_addon, recordedtv_folder.c_str(), cancel, changed);
		
		if(changed) {

			// Changes in the recording data affects just the PVR recordings
			log_notice(__func__, ": recording discovery data changed -- trigger recording update");
			g_pvr->TriggerRecordingUpdate();
		}

		log_notice(__func__, ": windows media center recording discovery task completed");
	}

	catch(std::exception& ex) { handle_stdexception(__func__, ex); }
	catch(...) { handle_generalexception(__func__); }
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
	log_message(ADDON::addon_log_t::LOG_DEBUG, std::forward<_args>(args)...);
}

// log_error
//
// Variadic method of writing a LOG_ERROR entry into the Kodi application log
template<typename... _args>
static void log_error(_args&&... args)
{
	log_message(ADDON::addon_log_t::LOG_ERROR, std::forward<_args>(args)...);
}

// log_info
//
// Variadic method of writing a LOG_INFO entry into the Kodi application log
template<typename... _args>
static void log_info(_args&&... args)
{
	log_message(ADDON::addon_log_t::LOG_INFO, std::forward<_args>(args)...);
}

// log_message
//
// Variadic method of writing an entry into the Kodi application log
template<typename... _args>
static void log_message(ADDON::addon_log_t level, _args&&... args)
{
	std::ostringstream stream;
	int unpack[] = {0, ( static_cast<void>(stream << args), 0 ) ... };
	(void)unpack;

	if(g_addon) g_addon->Log(level, stream.str().c_str());

	// Write LOG_ERROR level messages to an appropriate secondary log mechanism
	if(level == ADDON::addon_log_t::LOG_ERROR) {

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
	log_message(ADDON::addon_log_t::LOG_NOTICE, std::forward<_args>(args)...);
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

ADDON_STATUS ADDON_Create(void* handle, void* props)
{
	char			strvalue[1024] = { '\0' };				// Setting value 

	if((handle == nullptr) || (props == nullptr)) return ADDON_STATUS::ADDON_STATUS_PERMANENT_FAILURE;

	// Pull out a pointer to the PVR_PROPERTIES
	PVR_PROPERTIES* pvrprops = reinterpret_cast<PVR_PROPERTIES*>(props);

	try {
		
		// Create the global addon callbacks instance
		g_addon.reset(new ADDON::CHelper_libXBMC_addon());
		if(!g_addon->RegisterMe(handle)) throw string_exception("Failed to register addon handle (CHelper_libXBMC_addon::RegisterMe)");

		// Throw a banner out to the Kodi log indicating that the add-on is being loaded
		log_notice(VERSION_PRODUCTNAME_ANSI, " v", VERSION_VERSION3_ANSI, " loading");

		try { 

			// The user data path doesn't always exist when an addon has been installed
			if(!g_addon->DirectoryExists(pvrprops->strUserPath)) {

				log_notice(__func__, ": user data directory ", pvrprops->strUserPath, " does not exist");
				if(!g_addon->CreateDirectory(pvrprops->strUserPath)) throw string_exception("unable to create addon user data directory");
				log_notice(__func__, ": user data directory ", pvrprops->strUserPath, " created");
			}

			// Load the general settings
			if(g_addon->GetSetting("recordedtv_folder", strvalue)) g_settings.recordedtv_folder = strvalue;

			// Create the global pvrcallbacks instance
			g_pvr.reset(new CHelper_libXBMC_pvr());
			if(!g_pvr->RegisterMe(handle)) throw string_exception("Failed to register pvr addon handle (CHelper_libXBMC_pvr::RegisterMe)");
		
			try {

				// Create the global database connection pool instance, the file name is based on the versionb
				std::string databasefile = "file:///" + std::string(pvrprops->strUserPath) + "/mcerecordings-v" + VERSION_VERSION2_ANSI + ".db";
				g_connpool = std::make_shared<connectionpool>(databasefile.c_str(), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI);

				try {

					// Schedule the initial discovery run to execute as soon as possible
					std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
					g_scheduler.add(now + std::chrono::seconds(1), discover_recordings_task);

					g_scheduler.start();				// <--- Start the task scheduler
				}

				// Clean up the database connection pool on exception
				catch(...) { g_connpool.reset(); throw; }
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
	g_connpool.reset();
	g_pvr.reset(nullptr);
	
	// Send a notice out to the Kodi log as late as possible and destroy the addon callbacks
	log_notice(__func__, ": ", VERSION_PRODUCTNAME_ANSI, " v", VERSION_VERSION3_ANSI, " unloaded");
	g_addon.reset();
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
	std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
	std::unique_lock<std::mutex> settings_lock(g_settings_lock);

	// recordedtv_folder
	//
	if(strcmp(name, "recordedtv_folder") == 0) {

		if(strcmp(g_settings.recordedtv_folder.c_str(), reinterpret_cast<char const*>(value)) != 0) {

			g_settings.recordedtv_folder = reinterpret_cast<char const*>(value);
			log_notice(__func__, ": setting recordedtv_folder changed to ", g_settings.recordedtv_folder.c_str());

			// Reschedule the discovery task to run as soon as possible
			g_scheduler.remove(discover_recordings_task);
			g_scheduler.add(now + std::chrono::seconds(1), discover_recordings_task);
			log_notice(__func__, ": recorded tv folder changed -- scheduling discovery task to initiate in 1 second");
		}
	}

	return ADDON_STATUS_OK;
}

//---------------------------------------------------------------------------
// KODI PVR ADDON ENTRY POINTS
//---------------------------------------------------------------------------

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
// IsEPGTagRecordable
//
// Check if the given EPG tag can be recorded
//
// Arguments:
//
//	tag			- EPG tag to be checked
//	recordable	- Flag indicating if the EPG tag can be recorded

PVR_ERROR IsEPGTagRecordable(EPG_TAG const* /*tag*/, bool* /*recordable*/)
{
	return PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// IsEPGTagPlayable
//
// Check if the given EPG tag can be played
//
// Arguments:
//
//	tag			- EPG tag to be checked
//	playable	- Flag indicating if the EPG tag can be played

PVR_ERROR IsEPGTagPlayable(EPG_TAG const* /*tag*/, bool* /*playable*/)
{
	return PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// GetEPGTagEdl
//
// Retrieve the edit decision list (EDL) of an EPG tag on the backend
//
// Arguments:
//
//	tag			- EPG tag
//	edl			- The function has to write the EDL list into this array
//	count		- The maximum size of the EDL, out: the actual size of the EDL

PVR_ERROR GetEPGTagEdl(EPG_TAG const* /*tag*/, PVR_EDL_ENTRY /*edl*/[], int* /*count*/)
{
	return PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// GetEPGTagStreamProperties
//
// Get the stream properties for an epg tag from the backend
//
// Arguments:
//
//	tag			- EPG tag for which to retrieve the properties
//	props		- Array in which to set the stream properties
//	numprops	- Number of properties returned by this function

PVR_ERROR GetEPGTagStreamProperties(EPG_TAG const* /*tag*/, PVR_NAMED_VALUE* /*props*/, unsigned int* /*numprops*/)
{
	return PVR_ERROR_NOT_IMPLEMENTED;
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

	try { return get_recording_count(connectionpool::handle(g_connpool)); }
	catch(std::exception& ex) { return handle_stdexception(__func__, ex, -1); }
	catch(...) { return handle_generalexception(__func__, -1); }
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

	try {

		// Pull a database connection out from the connection pool
		connectionpool::handle dbhandle(g_connpool);

		// Enumerate all of the recordings in the database
		enumerate_recordings(dbhandle, [&](struct recording const& item) -> void {

			PVR_RECORDING recording;							// PVR_RECORDING to be transferred to Kodi
			memset(&recording, 0, sizeof(PVR_RECORDING));		// Initialize the structure

			// strRecordingId (required)
			if(item.recordingid == nullptr) return;
			snprintf(recording.strRecordingId, std::extent<decltype(recording.strRecordingId)>::value, "%s", item.recordingid);
			
			// strTitle (required)
			if(item.title == nullptr) return;
			snprintf(recording.strTitle, std::extent<decltype(recording.strTitle)>::value, "%s", item.title);

			// strEpisodeName
			if(item.episodename != nullptr) snprintf(recording.strEpisodeName, std::extent<decltype(recording.strEpisodeName)>::value, "%s", item.episodename);

			// iSeriesNumber
			recording.iSeriesNumber = item.seriesnumber;
			 
			// iEpisodeNumber
			recording.iEpisodeNumber = item.episodenumber;

			// iYear
			recording.iYear = item.year;

			// strDirectory
			if(item.title != nullptr) snprintf(recording.strDirectory, std::extent<decltype(recording.strDirectory)>::value, "%s", item.directory);

			// strPlot
			if(item.plot != nullptr) snprintf(recording.strPlot, std::extent<decltype(recording.strPlot)>::value, "%s", item.plot);

			// strChannelName
			if(item.channelname != nullptr) snprintf(recording.strChannelName, std::extent<decltype(recording.strChannelName)>::value, "%s", item.channelname);

			// recordingTime
			recording.recordingTime = item.recordingtime;

			// iDuration
			recording.iDuration = item.duration;

			// iChannelUid
			recording.iChannelUid = PVR_CHANNEL_INVALID_UID;

			// channelType
			recording.channelType = PVR_RECORDING_CHANNEL_TYPE_TV;

			g_pvr->TransferRecordingEntry(handle, &recording);
		});
	}
	
	catch(std::exception& ex) { return handle_stdexception(__func__, ex, PVR_ERROR::PVR_ERROR_FAILED); }
	catch(...) { return handle_generalexception(__func__, PVR_ERROR::PVR_ERROR_FAILED); }

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
	assert(g_addon);

	try { delete_recording(connectionpool::handle(g_connpool), g_addon, recording.strRecordingId); }
	catch(std::exception& ex) { return handle_stdexception(__func__, ex, PVR_ERROR::PVR_ERROR_FAILED); }
	catch(...) { return handle_generalexception(__func__, PVR_ERROR::PVR_ERROR_FAILED); }

	return PVR_ERROR::PVR_ERROR_NO_ERROR;
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
// SetRecordingLifetime
//
// Set the lifetime of a recording on the backend
//
// Arguments:
//
//	recording	- The recording to change the lifetime for

PVR_ERROR SetRecordingLifetime(PVR_RECORDING const* /*recording*/)
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
// GetDescrambleInfo
//
// Get the descramble information of the stream that's currently open
//
// Arguments:
//
//	descrambleinfo		- Descramble information

PVR_ERROR GetDescrambleInfo(PVR_DESCRAMBLE_INFO* /*descrambleinfo*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// GetChannelStreamProperties
//
// Gets the properties for channel streams when the input stream is not handled
//
// Arguments:
//
//	channel		- Channel to get the stream properties for
//	props		- Array of properties to be set for the stream
//	numprops	- Number of properties returned by this function

PVR_ERROR GetChannelStreamProperties(PVR_CHANNEL const* /*channel*/, PVR_NAMED_VALUE* /*props*/, unsigned int* /*numprops*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------
// GetRecordingStreamProperties
//
// Gets the properties for recording streams when the input stream is not handled
//
// Arguments:
//
//	recording	- Recording to get the stream properties for
//	props		- Array of properties to be set for the stream
//	numprops	- Number of properties returned by this function

PVR_ERROR GetRecordingStreamProperties(PVR_RECORDING const* recording, PVR_NAMED_VALUE* props, unsigned int* numprops)
{
	try {

		// PVR_STREAM_PROPERTY_STREAMURL
		snprintf(props[0].strName, std::extent<decltype(props[0].strName)>::value, PVR_STREAM_PROPERTY_STREAMURL);
		snprintf(props[0].strValue, std::extent<decltype(props[0].strName)>::value, get_recording_stream_url(connectionpool::handle(g_connpool), recording->strRecordingId).c_str());

		// PVR_STREAM_PROPERTY_MIMETYPE
		snprintf(props[1].strName, std::extent<decltype(props[1].strName)>::value, PVR_STREAM_PROPERTY_MIMETYPE);
		snprintf(props[1].strValue, std::extent<decltype(props[1].strName)>::value, "video/x-ms-wtv");

		// PVR_STREAM_PROPERTY_ISREALTIMESTREAM
		snprintf(props[2].strName, std::extent<decltype(props[2].strName)>::value, PVR_STREAM_PROPERTY_ISREALTIMESTREAM);
		snprintf(props[2].strValue, std::extent<decltype(props[2].strName)>::value, "false");

		*numprops = 3;
	}

	catch(std::exception& ex) { return handle_stdexception(__func__, ex, PVR_ERROR::PVR_ERROR_FAILED); }
	catch(...) { return handle_generalexception(__func__, PVR_ERROR::PVR_ERROR_FAILED); }

	return PVR_ERROR::PVR_ERROR_NO_ERROR;
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
// GetStreamReadChunkSize
//
// Obtain the chunk size to use when reading streams
//
// Arguments:
//
//	properties	- The properties of the currently playing stream

PVR_ERROR GetStreamReadChunkSize(int* /*chunksize*/)
{
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
// LengthRecordedStream
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
// GetStreamTimes
//
// Temporary function to be removed in later PVR API version

PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES* /*times*/)
{
	return PVR_ERROR::PVR_ERROR_NOT_IMPLEMENTED;
}

//---------------------------------------------------------------------------

#pragma warning(pop)
