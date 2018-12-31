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

// FUNCTION PROTOTYPES
//
void load_recordings(sqlite3* instance, std::unique_ptr<ADDON::CHelper_libXBMC_addon> const& callbacks, char const* folder, scalar_condition<bool> const& cancel);

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

void delete_recording(sqlite3* instance, std::unique_ptr<ADDON::CHelper_libXBMC_addon> const& callbacks, char const* recordingid)
{
	sqlite3_stmt*				statement;				// SQL statement to execute
	int							result;					// Result from SQLite function
	bool						deletefile = false;		// Flag to delete the file itself

	if((instance == nullptr) || (callbacks == nullptr) || (recordingid == nullptr)) return;

	// Prepare a query to delete the recording from the database
	auto sql = "delete from recording where recordingid like ?1";
	result = sqlite3_prepare_v2(instance, sql, -1, &statement, nullptr);
	if(result != SQLITE_OK) throw sqlite_exception(result, sqlite3_errmsg(instance));

	try {

		// Bind the query parameter(s)
		result = sqlite3_bind_text(statement, 1, recordingid, -1, SQLITE_STATIC);
		if(result != SQLITE_OK) throw sqlite_exception(result);

		// Execute the query - no result set is expected
		result = sqlite3_step(statement);
		if(result == SQLITE_ROW) throw string_exception(__func__, ": unexpected result set returned from non-query");
		if(result != SQLITE_DONE) throw sqlite_exception(result);

		sqlite3_finalize(statement);			// Finalize the SQLite statement

		// If the query deleted any rows from the database, also remove the file
		deletefile = (sqlite3_changes(instance) > 0);
	}

	catch (...) { sqlite3_finalize(statement); throw; }

	// Attempt to delete the actual file if it was removed from the database
	if((deletefile) && (!callbacks->DeleteFile(recordingid)))
		throw string_exception(__func__, ": unable to delete recorded TV file ", recordingid);
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

void discover_recordings(sqlite3* instance, std::unique_ptr<ADDON::CHelper_libXBMC_addon> const& callbacks, char const* folder, scalar_condition<bool> const& cancel, bool& changed)
{
	changed = false;							// Initialize [out] argument

	if(instance == nullptr) throw std::invalid_argument("instance");
	if(callbacks == nullptr) throw std::invalid_argument("callbacks");
	
	// Clone the recording table schema into a temporary table
	execute_non_query(instance, "drop table if exists discover_recording");
	execute_non_query(instance, "create temp table discover_recording as select * from recording limit 0");

	try {

		// Loading the discover_recording temp table is horrible; broken out into a helper function
		load_recordings(instance, callbacks, folder, cancel);
		
		// This requires a multi-step operation against the recording table; start a transaction
		execute_non_query(instance, "begin immediate transaction");

		try {

			// Delete any entries in the main recording table that are no longer present in the data
			if(execute_non_query(instance, "delete from recording where recordingid not in (select recordingid from discover_recording)") > 0) changed = true;

			// Insert entries in the main recording table that are new (not checking for differences here)
			if(execute_non_query(instance, "insert into recording select * from discover_recording where lower(recordingid) not in (select lower(recordingid) from recording)") > 0) changed = true;

			// Commit the database transaction
			execute_non_query(instance, "commit transaction");
		}
		
		// Rollback the transaction on any exception
		catch(...) { try_execute_non_query(instance, "rollback transaction"); throw; }

		// Drop the temporary table
		execute_non_query(instance, "drop table discover_recording");
	}

	// Drop the temporary table on any exception
	catch(...) { execute_non_query(instance, "drop table discover_recording"); throw; }
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
	sqlite3_stmt*				statement;			// SQL statement to execute
	int							result;				// Result from SQLite function
	
	if((instance == nullptr) || (callback == nullptr)) return;

	// recordingid | title | episodename | seriesnumber | episodenumber | year | streamurl | directory | plot | channelname | recordingtime | duration
	auto sql = "select recordingid, title, episodename, seriesnumber, episodenumber, year, streamurl, directory, plot, "
		"channelname, recordingtime, duration from recording";

	result = sqlite3_prepare_v2(instance, sql, -1, &statement, nullptr);
	if(result != SQLITE_OK) throw sqlite_exception(result, sqlite3_errmsg(instance));

	try {

		// Execute the query and iterate over all returned rows
		while(sqlite3_step(statement) == SQLITE_ROW) {

			struct recording item;
			item.recordingid = reinterpret_cast<char const*>(sqlite3_column_text(statement, 0));
			item.title = reinterpret_cast<char const*>(sqlite3_column_text(statement, 1));
			item.episodename = reinterpret_cast<char const*>(sqlite3_column_text(statement, 2));
			item.seriesnumber = sqlite3_column_int(statement, 3);
			item.episodenumber = sqlite3_column_int(statement, 4);
			item.year = sqlite3_column_int(statement, 5);
			item.streamurl = reinterpret_cast<char const*>(sqlite3_column_text(statement, 6));
			item.directory = reinterpret_cast<char const*>(sqlite3_column_text(statement, 7));
			item.plot = reinterpret_cast<char const*>(sqlite3_column_text(statement, 8));
			item.channelname = reinterpret_cast<char const*>(sqlite3_column_text(statement, 9));
			item.recordingtime = sqlite3_column_int(statement, 10);
			item.duration = sqlite3_column_int(statement, 11);

			callback(item);						// Invoke caller-supplied callback
		}
	
		sqlite3_finalize(statement);			// Finalize the SQLite statement
	}

	catch(...) { sqlite3_finalize(statement); throw; }
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
// get_recording_stream_url
//
// Gets the playback URL for a recording
//
// Arguments:
//
//	instance		- Database instance
//	recordingid		- Recording identifier (command url)

std::string get_recording_stream_url(sqlite3* instance, char const* recordingid)
{
	sqlite3_stmt*				statement;				// Database query statement
	std::string					streamurl;				// Generated stream URL
	int							result;					// Result from SQLite function call

	if((instance == nullptr) || (recordingid == nullptr)) return streamurl;

	// Prepare a scalar result query to generate a stream URL for the specified recording
	auto sql = "select streamurl from recording where recordingid like ?1";

	result = sqlite3_prepare_v2(instance, sql, -1, &statement, nullptr);
	if(result != SQLITE_OK) throw sqlite_exception(result, sqlite3_errmsg(instance));

	try {

		// Bind the query parameters
		result = sqlite3_bind_text(statement, 1, recordingid, -1, SQLITE_STATIC);
		if(result != SQLITE_OK) throw sqlite_exception(result);
		
		// Execute the scalar query
		result = sqlite3_step(statement);

		// There should be a single SQLITE_ROW returned from the initial step
		if(result == SQLITE_ROW) streamurl.assign(reinterpret_cast<char const*>(sqlite3_column_text(statement, 0)));
		else if(result != SQLITE_DONE) throw sqlite_exception(result, sqlite3_errmsg(instance));

		sqlite3_finalize(statement);
		return streamurl;
	}

	catch(...) { sqlite3_finalize(statement); throw; }
}

//---------------------------------------------------------------------------
// load_recordings
//
// Loads the available MCE recordings into the discover_recording table
//
// Arguments:
//
//	instance		- Database instance
//	callbacks		- addoncallbacks instance
//	folder			- Location of the recorded TV files
//	cancel			- Condition variable used to cancel the operation

void load_recordings(sqlite3* instance, std::unique_ptr<ADDON::CHelper_libXBMC_addon> const& callbacks, char const* folder, scalar_condition<bool> const& cancel)
{
	sqlite3_stmt*				statement;			// SQL statement to execute
	VFSDirEntry*				files;				// Enumerated files in the directory
	unsigned int				numfiles;			// Number of files enumerated in the directory
	int							result;				// Result from SQLite function
	HRESULT						hresult;			// Result from COM/OLE function call

	if (instance == nullptr) throw std::invalid_argument("instance");
	if (callbacks == nullptr) throw std::invalid_argument("callbacks");

	// If the folder name is null or empty, there is nothing to discover; leave the temp table empty
	if ((folder == nullptr) || (*folder == '\0')) return;

	// recordingid | title | episodename | seriesnumber | episodenumber | year | streamurl | directory | plot | channelname | recordingtime | duration
	auto sql = "insert into discover_recording values(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)";

	result = sqlite3_prepare_v2(instance, sql, -1, &statement, nullptr);
	if (result != SQLITE_OK) throw sqlite_exception(result, sqlite3_errmsg(instance));

	try {

		// Attempt to get a list of all the .wtv files in the target directory
		if (!callbacks->GetDirectory(folder, ".wtv", &files, &numfiles)) throw string_exception(__func__, ": cannot enumerate the contents of folder ", folder);

		try {

			// Iterate over each .wtv file in the target directory to load the metadata
			for (unsigned int index = 0; index < numfiles; index++) {

				IShellItem2*		shellitem = nullptr;			// IShellItem2 instance pointer
				IPropertyStore*		store = nullptr;				// IPropertyStore instance pointer

				// If the current file entry is a folder, skip it -- this isn't recursive
				if (files[index].folder) continue;

				// Check if the operation should be cancelled prior to loading the next file
				if (cancel.test(true)) throw string_exception(__func__, ": operation cancelled");

				try {

					// Convert smb:// paths into unc paths; won't affect local paths (like "D:\")
					std::string filepath = smb_to_unc(files[index].path);
					std::wstring widepath = to_wstring(filepath.data(), static_cast<int>(filepath.size()));

					// Create an IShellItem2 instance from the current path (UTF-16)
					hresult = SHCreateItemFromParsingName(widepath.c_str(), nullptr, IID_IShellItem2, reinterpret_cast<void**>(&shellitem));
					if (FAILED(hresult)) throw string_exception("SHCreateItemFromParsingName() failed");

					// Query for the GPS_DEFAULT IPropertyStore instance; always done with IShellItem2 afterwards
					hresult = shellitem->GetPropertyStore(GETPROPERTYSTOREFLAGS::GPS_DEFAULT, IID_IPropertyStore, reinterpret_cast<void**>(&store));
					shellitem->Release();
					if (FAILED(hresult)) throw string_exception("shellitem->GetPropertyStore() failed");

					try {

						// recordingid
						sqlite3_bind_text(statement, 1, files[index].path, -1, SQLITE_STATIC);

						// title
						std::wstring title = get_lpwstr(store, PKEY_Title);
						sqlite3_bind_text16(statement, 2, title.c_str(), -1, SQLITE_STATIC);

						// episodename
						std::wstring episodename = get_lpwstr(store, PKEY_RecordedTV_EpisodeName);
						sqlite3_bind_text16(statement, 3, episodename.c_str(), -1, SQLITE_STATIC);

						// seriesnumber
						sqlite3_bind_int(statement, 4, static_cast<int>(get_ui4(store, PKEY_Media_SeasonNumber)));

						// episodenumber
						sqlite3_bind_int(statement, 5, static_cast<int>(get_ui4(store, PKEY_Media_EpisodeNumber)));

						// year
						FILETIME originalbroadcastdate = get_filetime(store, PKEY_RecordedTV_OriginalBroadcastDate);
						SYSTEMTIME storiginalbroadcastdate;
						FileTimeToSystemTime(&originalbroadcastdate, &storiginalbroadcastdate);
						sqlite3_bind_int(statement, 6, static_cast<int>(storiginalbroadcastdate.wYear));

						// streamurl
						sqlite3_bind_text(statement, 7, files[index].path, -1, SQLITE_STATIC);

						// directory
						sqlite3_bind_text16(statement, 8, title.c_str(), -1, SQLITE_STATIC);

						// plot
						std::wstring programdescription = get_lpwstr(store, PKEY_RecordedTV_ProgramDescription);
						sqlite3_bind_text16(statement, 9, programdescription.c_str(), -1, SQLITE_STATIC);

						// channelname
						std::wstring stationname = get_lpwstr(store, PKEY_RecordedTV_StationName);
						sqlite3_bind_text16(statement, 10, stationname.c_str(), -1, SQLITE_STATIC);

						// recordingTime
						FILETIME recordingtime = get_filetime(store, PKEY_RecordedTV_RecordingTime);
						ULARGE_INTEGER ulrectime = { recordingtime.dwLowDateTime, recordingtime.dwHighDateTime };
						sqlite3_bind_int(statement, 11, static_cast<int>(ulrectime.QuadPart / 10000000ULL - 11644473600ULL));

						// duration
						uint64_t duration = get_ui8(store, PKEY_Media_Duration);
						sqlite3_bind_int(statement, 12, static_cast<int>(duration / 10000000));

						// This is a non-query, it's not expected to return any rows
						result = sqlite3_step(statement);
						if (result != SQLITE_DONE) throw string_exception("non-query failed or returned an unexpected result set");

						store->Release();							// Release IPropertyStore
					}

					catch (...) { store->Release(); throw; }
				}

				catch (std::exception& ex) {

					// Log an error message if any one file fails to process, but keep going ...
					std::string message = std::string("Unable to process file ") + files[index].path + ": " + ex.what();
					callbacks->Log(ADDON::addon_log_t::LOG_ERROR, message.c_str());
				}

				// Reset the prepared statement so that it can be executed again
				result = sqlite3_reset(statement);
				if (result != SQLITE_OK) throw sqlite_exception(result);
			}

			// Release the file listing returned via callbacks->GetDirectory()
			callbacks->FreeDirectory(files, numfiles);
		}

		catch (...) { callbacks->FreeDirectory(files, numfiles); throw; }
	}

	catch (...) { sqlite3_finalize(statement); throw; }
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
