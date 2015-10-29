/*
 * ntfs-linker
 * Copyright 2015 Stroz Friedberg, LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License is available at
 * <http://www.gnu.org/licenses/>.
 *
 * You can contact Stroz Friedberg by electronic and paper mail as follows:
 *
 * Stroz Friedberg, LLC
 * 32 Avenue of the Americas
 * 4th Floor
 * New York, NY, 10013
 * info@strozfriedberg.com
 */

#include "aggregate.h"
#include "sqlite_util.h"

#include <fstream>
#include <iostream>
#include <sstream>

int busyHandler(__attribute__((unused)) void* foo, __attribute__((unused)) int num) {
  char input;
  std::cerr << "Database is busy. Cannot commit transaction." << std::endl;
  std::cerr << "Close any application which may have a lock on the database." << std::endl;
  std::cerr << "Try again? (y/n): ";
  std::cin >> input;
  if(input == 'y' || input == 'Y')
    return 1;
  else
    exit(0);

}

std::string getColList(const std::vector<std::vector<std::string>>& cols, int type) {
  std::stringstream ss;
  bool isFirst = true;
  for (auto& col: cols) {
    if(!isFirst) {
      ss << ", ";
    }
    isFirst = false;
    switch(type) {
      case 0:
        ss << col[0] << " " << col[1];
        break;
      case 1:
        ss << col[0];
        break;
      case 2:
        ss << "?";
        break;
    }
  }
  return ss.str();
}

void SQLiteHelper::init(std::string dbName, bool overwrite) {
  int rc = 0;

  /*
  vacuum cleaning the db file doesn't seem to work
  Instead, I'll open the file for writing, then close it
  so that the file is truncated
  */
  if(overwrite) {
    std::ofstream file(dbName.c_str(), std::ios::trunc);
    file.close();
  }
  rc = sqlite3_open_v2(dbName.c_str(), &Db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
  if(rc) {
    std::cerr << "Error opening database" << std::endl;
    exit(1);
  }

  sqlite3_busy_handler(Db, &busyHandler, 0);
  beginTransaction();

  if(overwrite) {
    rc |= sqlite3_exec(Db, "drop table if exists log;", 0, 0, 0);
    rc |= sqlite3_exec(Db, "drop table if exists usn;", 0, 0, 0);
    rc |= sqlite3_exec(Db, "drop table if exists event;", 0, 0, 0);
  }
  rc |= sqlite3_exec(Db, std::string("create table if not exists log "
                                     "(" + getColList(LogColumns, 0) + ");").c_str(),
                     0, 0, 0);
  rc |= sqlite3_exec(Db, std::string("create table if not exists usn "
                                    "(" + getColList(UsnColumns, 0) + ");").c_str(),
                     0, 0, 0);
  rc |= sqlite3_exec(Db, std::string("create temporary table event_temp "
                                     "(" + getColList(EventTempColumns, 0) + ", "
                                     "UNIQUE(USN_LSN, EventSource, Volume));").c_str(),
                     0, 0, 0);
  rc |= sqlite3_exec(Db, std::string("create table if not exists event "
                                     "(" + getColList(EventColumns, 0) + ");").c_str(),
                     0, 0, 0);
  if(rc) {
    std::cerr << "SQL Error " << rc << " at " << __FILE__ << ":" << __LINE__ << std::endl;
    std::cerr << sqlite3_errmsg(Db) << std::endl;
    sqlite3_close(Db);
    exit(1);
  }
  prepareStatements();
  endTransaction();
}

void SQLiteHelper::beginTransaction() {
  int rc = sqlite3_exec(Db, "BEGIN TRANSACTION", 0, 0, 0);
  if(rc) {
    std::cerr << "SQL Error " << rc << " at " << __FILE__ << ":" << __LINE__ << std::endl;
    std::cerr << sqlite3_errmsg(Db) << std::endl;
    sqlite3_close(Db);
    exit(1);
  }
}

void SQLiteHelper::endTransaction() {
  int rc = sqlite3_exec(Db, "END TRANSACTION", 0, 0, 0);
  if(rc) {
    std::cerr << "SQL Error " << rc << " at " << __FILE__ << ":" << __LINE__ << std::endl;
    std::cerr << sqlite3_errmsg(Db) << std::endl;
    sqlite3_close(Db);
    exit(1);
  }
}

void SQLiteHelper::close() {
  finalizeStatements();
  sqlite3_close(Db);
}

int SQLiteHelper::prepareStatement(sqlite3_stmt **stmt, std::string& sql) {
  return sqlite3_prepare_v2(Db, sql.c_str(), sql.length() + 1, stmt, NULL);
}

void SQLiteHelper::bindForSelect(const VersionInfo& version) {
  sqlite3_bind_int64(EventUsnSelect, 1, EventSources::SOURCE_USN);
  sqlite3_bind_int64(EventLogSelect, 1, EventSources::SOURCE_LOG);

  sqlite3_bind_text(EventUsnSelect, 2, version.Snapshot.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(EventLogSelect, 2, version.Snapshot.c_str(), -1, SQLITE_TRANSIENT);

  sqlite3_bind_text(EventUsnSelect, 3, version.Volume.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(EventLogSelect, 3, version.Volume.c_str(), -1, SQLITE_TRANSIENT);
}

void SQLiteHelper::resetSelect() {
  sqlite3_reset(EventUsnSelect);
  sqlite3_reset(EventLogSelect);
}

void SQLiteHelper::prepareStatements() {
  int rc = 0;
  std::string usnInsert = "insert into usn (" + getColList(UsnColumns, 1) + ") "
                                   "values (" + getColList(UsnColumns, 2) + ");";
  std::string logInsert = "insert into log (" + getColList(LogColumns, 1) + ") "
                                   "values (" + getColList(LogColumns, 2) + ");";
  // Events are processed from the oldest to the newest, so when an event with a conflicting (USN_LSN, EventSource)
  // comes into play, it should be ignored
  std::string eventInsert = "insert or ignore into event_temp "
                            "(" + getColList(EventTempColumns, 1) + ") "
                            + "values (" + getColList(EventTempColumns, 2) + ");";
  std::string eventFinalInsert = "insert into event "
                            "(" + getColList(EventColumns, 1) + ") "
                            + "values (" + getColList(EventColumns, 2) + ");";
  std::string eventSelect = "select " + getColList(EventTempColumns, 1) + " from event_temp where EventSource=? and Snapshot=? and Volume=? order by USN_LSN desc;";

  rc |= prepareStatement(&UsnInsert, usnInsert);
  rc |= prepareStatement(&LogInsert, logInsert);
  rc |= prepareStatement(&EventInsert, eventInsert);
  rc |= prepareStatement(&EventFinalInsert, eventFinalInsert);
  rc |= prepareStatement(&EventUsnSelect, eventSelect);
  rc |= prepareStatement(&EventLogSelect, eventSelect);

  if (rc) {
    std::cerr << "SQL Error " << rc << " at " << __FILE__ << ":" << __LINE__ << std::endl;
    std::cerr << sqlite3_errmsg(Db) << std::endl;
    sqlite3_close(Db);
    exit(1);
  }
}

void SQLiteHelper::finalizeStatements() {
  sqlite3_finalize(UsnInsert);
  sqlite3_finalize(LogInsert);
  sqlite3_finalize(EventInsert);
  sqlite3_finalize(EventFinalInsert);
  sqlite3_finalize(EventUsnSelect);
  sqlite3_finalize(EventLogSelect);
}

const std::vector<std::vector<std::string>> SQLiteHelper::EventColumns = {
  { "Position", "int"},
  { "Timestamp", "text"},
  { "EventSource", "text"},
  { "EventType", "text"},
  { "FileName", "text"},
  { "Folder", "text"},
  { "FullPath", "text"},
  { "MFTRecord", "int"},
  { "ParentMFTRecord", "int"},
  { "USN_LSN", "int"},
  { "OldFileName", "text"},
  { "OldFolder", "text"},
  { "OldParentRecord", "int"},
  { "Offset", "int"},
  { "Created", "text"},
  { "Modified", "text"},
  { "Comment", "text"},
  { "Snapshot", "text"},
  { "Volume", "text"}
};

const std::vector<std::vector<std::string>> SQLiteHelper::EventTempColumns = {
  { "MFTRecord", "int"},
  { "ParentMFTRecord", "int"},
  { "OldParentRecord", "int"},
  { "USN_LSN", "int"},
  { "Timestamp", "text"},
  { "FileName", "text"},
  { "OldFileName", "text"},
  { "EventType", "int"},
  { "EventSource", "int"},
  { "IsEmbedded", "int"},
  { "Offset", "int"},
  { "Created", "text"},
  { "Modified", "text"},
  { "Comment", "text"},
  { "Snapshot", "text"},
  { "Volume", "text"}
};

const std::vector<std::vector<std::string>> SQLiteHelper::LogColumns = {
  { "CurrentLSN", "int"},
  { "PrevLSN", "int"},
  { "UndoLSN", "int"},
  { "ClientID", "int"},
  { "RecordType", "int"},
  { "RedoOP", "text"},
  { "UndoOP", "text"},
  { "TargetAttribute", "int"},
  { "MFTClusterIndex", "int"},
  { "Offset", "int"},
  { "Snapshot", "text"},
  { "Volume", "text"}
};

const std::vector<std::vector<std::string>> SQLiteHelper::UsnColumns = {
  { "MFTRecord", "int"},
  { "ParentMFTRecord", "int"},
  { "USN", "int"},
  { "Timestamp", "text"},
  { "Reason", "text"},
  { "FileName", "text"},
  { "FullPath", "text"},
  { "Folder", "text"},
  { "Offset", "int"},
  { "Snapshot", "text"},
  { "Volume", "text" }
};
