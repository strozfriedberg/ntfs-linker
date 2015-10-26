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
  void insert(sqlite3_stmt* stmt);
  static std::string getColumnHeaders();

  int64_t Record, Parent, PreviousParent, UsnLsn, Type, Source, Offset, Id, Order;
  std::string Timestamp, Name, PreviousName, Created, Modified, Comment, Snapshot, Volume;
  bool IsAnchor, IsEmbedded;
};

void outputEvents(std::vector<File>& records, SQLiteHelper& sqliteHelper, VolumeIO& volumeIO, const VersionInfo& version);
