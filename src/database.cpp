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
#include "database.h"

#include <algorithm>
#include <exception>
#include <stdint.h>
#include <string.h>

#include "sqlite_exception.h"
#include "string_exception.h"

#pragma warning(push, 4)

// Check SQLITE_THREADSAFE
//
#if (SQLITE_THREADSAFE != 2)
#error SQLITE_THREADSAFE must be defined and set to 2
#endif

// Check SQLITE_TEMP_STORE
//
#if (SQLITE_TEMP_STORE != 3)
#error SQLITE_TEMP_STORE must be defined and set to 3
#endif

//
// HELPER FUNCTIONS
//

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

//
// CONNECTIONPOOL IMPLEMENTATION
//

//---------------------------------------------------------------------------
// connectionpool Constructor
//
// Arguments:
//
//	connstring		- Database connection string
//	flags			- Database connection flags

connectionpool::connectionpool(char const* connstring, int flags) : m_connstr((connstring) ? connstring : ""), m_flags(flags)
{
	sqlite3*		handle = nullptr;		// Initial database connection

	if(connstring == nullptr) throw std::invalid_argument("connstring");

	// Create and pool the initial connection now to give the caller an opportunity
	// to catch any exceptions during initialization of the database
	handle = open_database(m_connstr.c_str(), m_flags, true);
	m_connections.push_back(handle);
	m_queue.push(handle);
}

//---------------------------------------------------------------------------
// connectionpool Destructor

connectionpool::~connectionpool()
{
	// Close all of the connections that were created in the pool
	for(auto const& iterator : m_connections) close_database(iterator);
}

//---------------------------------------------------------------------------
// connectionpool::acquire
//
// Acquires a database connection, opening a new one if necessary
//
// Arguments:
//
//	NONE

sqlite3* connectionpool::acquire(void)
{
	sqlite3* handle = nullptr;				// Handle to return to the caller

	std::unique_lock<std::mutex> lock(m_lock);

	if(m_queue.empty()) {

		// No connections are available, open a new one using the same flags
		handle = open_database(m_connstr.c_str(), m_flags, false);
		m_connections.push_back(handle);
	}

	// At least one connection is available for reuse
	else { handle = m_queue.front(); m_queue.pop(); }

	return handle;
}

//---------------------------------------------------------------------------
// connectionpool::release
//
// Releases a database handle acquired from the pool
//
// Arguments:
//
//	handle		- Handle to be releases

void connectionpool::release(sqlite3* handle)
{
	std::unique_lock<std::mutex> lock(m_lock);

	if(handle == nullptr) throw std::invalid_argument("handle");

	m_queue.push(handle);
}

//---------------------------------------------------------------------------
// close_database
//
// Closes a SQLite database handle
//
// Arguments:
//
//	instance	- Database instance handle to be closed

void close_database(sqlite3* instance)
{
	if(instance) sqlite3_close(instance);
}

//---------------------------------------------------------------------------
// delete_recording
//
// Deletes a recording from the database instance
//
// Arguments:
//
//	instance		- Database instance
//	callbacks		- addoncallbacks instance
//	recordingid		- Recording ID (CmdURL) of the item to delete

void delete_recording(sqlite3* instance, addoncallbacks const* callbacks, char const* recordingid)
{
	UNREFERENCED_PARAMETER(callbacks);
	UNREFERENCED_PARAMETER(recordingid);
	
	if((instance == nullptr) || (recordingid == nullptr)) return;
}

//---------------------------------------------------------------------------
// discover_recordings
//
// Reloads the information about the available recordings
//
// Arguments:
//
//	instance	- SQLite database instance
//	callbacks	- addoncallbacks instance
//	folder		- Location of the recorded TV files
//	cancel		- Condition variable used to cancel the operation

void discover_recordings(sqlite3* instance, addoncallbacks const* callbacks, char const* folder, scalar_condition<bool> const& cancel)
{
	bool ignored;
	return discover_recordings(instance, callbacks, folder, cancel, ignored);
}

//---------------------------------------------------------------------------
// discover_recordings
//
// Reloads the information about the available recordings
//
// Arguments:
//
//	instance	- SQLite database instance
//	callbacks	- addoncallbacks instance
//	folder		- Location of the recorded TV files
//	cancel		- Condition variable used to cancel the operation
//	changed		- Flag indicating if the data has changed

void discover_recordings(sqlite3* instance, addoncallbacks const* callbacks, char const* folder, scalar_condition<bool> const& cancel, bool& changed)
{
	UNREFERENCED_PARAMETER(callbacks);
	UNREFERENCED_PARAMETER(cancel);
	UNREFERENCED_PARAMETER(folder);
	UNREFERENCED_PARAMETER(changed);

	if(instance == nullptr) throw std::invalid_argument("instance");
}

//---------------------------------------------------------------------------
// enumerate_recordings
//
// Enumerates the available recordings
//
// Arguments:
//
//	instance	- Database instance
//	callback	- Callback function

void enumerate_recordings(sqlite3* instance, enumerate_recordings_callback callback)
{
	UNREFERENCED_PARAMETER(instance);
	UNREFERENCED_PARAMETER(callback);
}

//---------------------------------------------------------------------------
// execute_non_query
//
// Executes a non-query against the database
//
// Arguments:
//
//	instance	- Database instance
//	sql			- SQL non-query to execute

int execute_non_query(sqlite3* instance, char const* sql)
{
	char*		errmsg = nullptr;		// Error message from SQLite
	int			result;					// Result from SQLite function call
	
	try {
	
		// Attempt to execute the statement and throw the error on failure
		result = sqlite3_exec(instance, sql, nullptr, nullptr, &errmsg);
		if(result != SQLITE_OK) throw sqlite_exception(result, errmsg);

		// Return the number of changes made by the preceeding query
		return sqlite3_changes(instance);
	}

	catch(...) { if(errmsg) sqlite3_free(errmsg); throw; }
}

//---------------------------------------------------------------------------
// get_recording_count
//
// Gets the number of available recordings in the database
//
// Arguments:
//
//	instance	- SQLite database instance

int get_recording_count(sqlite3* instance)
{
	if(instance == nullptr) return 0;

	return 0;
}

//---------------------------------------------------------------------------
// open_database
//
// Opens the SQLite database instance
//
// Arguments:
//
//	connstring		- Database connection string
//	flags			- Database open flags (see sqlite3_open_v2)

sqlite3* open_database(char const* connstring, int flags)
{
	return open_database(connstring, flags, false);
}

//---------------------------------------------------------------------------
// open_database
//
// Opens the SQLite database instance
//
// Arguments:
//
//	connstring		- Database connection string
//	flags			- Database open flags (see sqlite3_open_v2)
//	initialize		- Flag indicating database schema should be (re)initialized

sqlite3* open_database(char const* connstring, int flags, bool initialize)
{
	sqlite3*			instance = nullptr;			// SQLite database instance

	// Create the SQLite database using the provided connection string
	int result = sqlite3_open_v2(connstring, &instance, flags, nullptr);
	if(result != SQLITE_OK) throw sqlite_exception(result);

	// set the connection to report extended error codes
	//
	sqlite3_extended_result_codes(instance, -1);

	// set a busy_timeout handler for this connection
	//
	sqlite3_busy_timeout(instance, 30000);
	
	try {

		// switch the database to write-ahead logging
		//
		execute_non_query(instance, "pragma journal_mode=wal");

		// Only execute schema creation steps if the database is being initialized; the caller needs
		// to ensure that this is set for only one connection otherwise locking issues can occur
		//
		if(initialize) {

			// table: recording
			//
			// recordingid(pk) | title | episodename | seriesnumber | episodenumber | year | streamurl | directory | plot | channelname | recordingtime | duration
			execute_non_query(instance, "create table if not exists recording(recordingid text primary key not null, "
				"title text, episodename text, seriesnumber int, episodenumber int, year int, streamurl text, directory text, "
				"plot text, channelname text, recordingtime int, duration int)");
		}
	}

	// Close the database instance on any thrown exceptions
	catch(...) { sqlite3_close(instance); throw; }

	return instance;
}

//---------------------------------------------------------------------------
// try_execute_non_query
//
// Executes a non-query against the database and eats any exceptions
//
// Arguments:
//
//	instance	- Database instance
//	sql			- SQL non-query to execute

bool try_execute_non_query(sqlite3* instance, char const* sql)
{
	try { execute_non_query(instance, sql); }
	catch(...) { return false; }

	return true;
}

//---------------------------------------------------------------------------

#pragma warning(pop)
