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

  static const std::vector<std::vector<std::string>> EventColumns, LogColumns, UsnColumns;

  sqlite3* Db;
};
