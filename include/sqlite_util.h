#pragma once

#include <sqlite3.h>
#include <string>

struct VersionInfo {
  VersionInfo(std::string volume, std::string snapshot) : Volume(volume), Snapshot(snapshot) {}
  std::string Volume, Snapshot;
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

  sqlite3_stmt *UsnInsert, *LogInsert, *EventInsert, *EventUsnSelect, *EventLogSelect;
private:
  void finalizeStatements();
  int prepareStatement(sqlite3_stmt **stmt, std::string& sql);
  void prepareStatements();

  sqlite3* Db;
};
