#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>

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

  void makeTempOrderTable();
  void updateEventOrders();


  sqlite3_stmt *UsnInsert, *LogInsert, *EventInsert, *EventUsnSelect, *EventLogSelect, *EventUpdate;
private:
  void finalizeStatements();
  int prepareStatement(sqlite3_stmt **stmt, std::string& sql);
  void prepareStatements();
  std::string toColumnList(std::vector<std::vector<std::string>>& cols);

  static const std::vector<std::vector<std::string>> EventColumns, LogColumns, UsnColumns;

  sqlite3* Db;
};
