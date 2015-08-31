#include <sqlite3.h>
#include <string>
#pragma once

class SQLiteHelper {
public:
  SQLiteHelper(std::string dbName, bool overwrite);
  void commit();
  void close();

  sqlite3_stmt *MftInsert, *UsnInsert, *LogInsert, *EventInsert, *EventUsnSelect, *EventLogSelect;
private:
  void finalizeStatements();
  int prepareStatement(sqlite3_stmt **stmt, std::string& sql);
  void prepareStatements();

  sqlite3* Db;
};
