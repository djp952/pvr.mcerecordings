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

#ifndef __DATABASE_H_
#define __DATABASE_H_
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "addoncallbacks.h"
#include "scalar_condition.h"

#pragma warning(push, 4)				// Enable maximum compiler warnings

//---------------------------------------------------------------------------
// DATA TYPES
//---------------------------------------------------------------------------

// recording
//
// Information about a single recording enumerated from the database
struct recording {

	char const*			recordingid;
	char const*			title;
	char const*			episodename;
	int					seriesnumber;
	int					episodenumber;
	int					year;
	char const*			streamurl;
	char const*			directory;
	char const*			plot;
	char const*			channelname;
	time_t				recordingtime;
	int					duration;
};

// enumerate_recordings_callback
//
// Callback function passed to enumerate_recordings
using enumerate_recordings_callback = std::function<void(struct recording const& recording)>;

//---------------------------------------------------------------------------
// connectionpool
//
// Implements a connection pool for the SQLite database connections

class connectionpool
{
public:

	// Instance Constructor
	//
	connectionpool(char const* connstr, int flags);

	// Destructor
	//
	~connectionpool();

	//-----------------------------------------------------------------------
	// Member Functions

	// acquire
	//
	// Acquires a connection from the pool, creating a new one as necessary
	sqlite3* acquire(void);

	// release
	//
	// Releases a previously acquired connection back into the pool
	void release(sqlite3* handle);

	//-----------------------------------------------------------------------
	// Type Declarations

	// handle
	//
	// RAII class to acquire and release connections from the pool
	class handle
	{
	public:

		// Constructor / Destructor
		//
		handle(std::shared_ptr<connectionpool> const& pool) : m_pool(pool), m_handle(pool->acquire()) { }
		~handle() { m_pool->release(m_handle); }

		// sqlite3* type conversion operator
		//
		operator sqlite3*(void) const { return m_handle; }

	private:

		handle(handle const&)=delete;
		handle& operator=(handle const&)=delete;

		// m_pool
		//
		// Shared pointer to the parent connection pool
		std::shared_ptr<connectionpool> const m_pool;

		// m_handle
		//
		// SQLite handle acquired from the pool
		sqlite3* m_handle;
	};

private:

	connectionpool(connectionpool const&)=delete;
	connectionpool& operator=(connectionpool const&)=delete;

	//-----------------------------------------------------------------------
	// Member Variables
	
	std::string	const			m_connstr;			// Connection string
	int	const					m_flags;			// Connection flags
	std::vector<sqlite3*>		m_connections;		// All active connections
	std::queue<sqlite3*>		m_queue;			// Queue of unused connection
	mutable std::mutex			m_lock;				// Synchronization object
};

//---------------------------------------------------------------------------
// FUNCTION PROTOTYPES
//---------------------------------------------------------------------------

// close_database
//
// Creates a SQLite database instance handle
void close_database(sqlite3* instance);

// delete_recording
//
// Deletes a recording from the database instance
void delete_recording(sqlite3* instance, addoncallbacks const* callbacks, char const* recordingid);

// discover_recordings
//
// Reloads the information about the available recordings
void discover_recordings(sqlite3* instance, addoncallbacks const* callbacks, char const* folder, scalar_condition<bool> const& cancel, bool& changed);

// enumerate_recordings
//
// Enumerates the available recordings
void enumerate_recordings(sqlite3* instance, enumerate_recordings_callback callback);

// execute_non_query
//
// executes a non-query against the database
int execute_non_query(sqlite3* instance, char const* sql);

// get_recording_count
//
// Gets the number of available recordings in the database
int get_recording_count(sqlite3* instance);

// open_database
//
// Opens a handle to the backend SQLite database
sqlite3* open_database(char const* connstring, int flags);
sqlite3* open_database(char const* connstring, int flags, bool initialize);

// try_execute_non_query
//
// executes a non-query against the database but eats any exceptions
bool try_execute_non_query(sqlite3* instance, char const* sql);

//---------------------------------------------------------------------------

#pragma warning(pop)

#endif	// __DATABASE_H_
