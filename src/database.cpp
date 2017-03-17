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

void delete_recording(sqlite3* instance, addoncallbacks const& callbacks, char const* recordingid)
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
//	cancel		- Condition variable used to cancel the operation

void discover_recordings(sqlite3* instance, addoncallbacks const& callbacks, scalar_condition<bool> const& cancel)
{
	bool ignored;
	return discover_recordings(instance, callbacks, cancel, ignored);
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
//	cancel		- Condition variable used to cancel the operation
//	changed		- Flag indicating if the data has changed

void discover_recordings(sqlite3* instance, addoncallbacks const& callbacks, scalar_condition<bool> const& cancel, bool& changed)
{
	UNREFERENCED_PARAMETER(callbacks);
	UNREFERENCED_PARAMETER(cancel);
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
			// deviceid(pk) | data
			///execute_non_query(instance, "create table if not exists recording(deviceid text primary key not null, data text)");
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
