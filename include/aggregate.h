#pragma once

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
  void write(int order, std::ostream& out, std::vector<File>& records);
  void updateRecords(std::vector<File>& records);
  static std::string getColumnHeaders();

  int64_t Record, Parent, PreviousParent, UsnLsn, Type, Source, Offset;
  std::string Timestamp, Name, PreviousName, Created, Modified, Comment, Snapshot;
  bool IsAnchor, IsEmbedded;
};

void outputEvents(std::vector<File>& records, SQLiteHelper& sqliteHelper, std::ofstream& o_events, std::string snapshot);
