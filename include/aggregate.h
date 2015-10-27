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

#include "controller.h"
#include "file.h"
#include "util.h"
#include "sqlite_util.h"

#include <fstream>
#include <list>
#include <sqlite3.h>
#include <vector>
#include <string>

class Event {
public:
  Event();
  void init(sqlite3_stmt* stmt);
  void write(std::ostream& out, const std::vector<File>& records);
  void updateRecords(std::vector<File>& records);
  void insert(sqlite3_stmt* stmt, std::vector<File>& records);
  static std::string getColumnHeaders();

  int64_t Record, Parent, PreviousParent, UsnLsn, Type, Source, Offset, Id, Order;
  std::string Timestamp, Name, PreviousName, Created, Modified, Comment, Snapshot, Volume;
  bool IsAnchor, IsEmbedded;
};

void outputEvents(std::vector<File>& records, SQLiteHelper& sqliteHelper, VolumeIO& volumeIO, const VersionInfo& version);
