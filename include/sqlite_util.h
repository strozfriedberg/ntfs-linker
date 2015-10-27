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

#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>

struct VersionInfo {
  VersionInfo(std::string snapshot, std::string volume) : Snapshot(snapshot), Volume(volume){}
  std::string Snapshot, Volume;
};

class SQLiteHelper {
public:
  SQLiteHelper() : UsnInsert(NULL), LogInsert(NULL),
                   EventInsert(NULL), EventUsnSelect(NULL), EventLogSelect(NULL),
                   Db(NULL) {}
  void init(std::string dbName, bool overwrite);
  void beginTransaction();
  void endTransaction();
  void close();
  void bindForSelect(const VersionInfo& version);
  void resetSelect();

  sqlite3_stmt *UsnInsert, *LogInsert, *EventInsert, *EventUsnSelect, *EventLogSelect, *EventFinalInsert;
private:
  void finalizeStatements();
  int prepareStatement(sqlite3_stmt **stmt, std::string& sql);
  void prepareStatements();
  std::string toColumnList(std::vector<std::vector<std::string>>& cols);

  static const std::vector<std::vector<std::string>> EventColumns, LogColumns, UsnColumns, EventTempColumns;

  sqlite3* Db;
};
