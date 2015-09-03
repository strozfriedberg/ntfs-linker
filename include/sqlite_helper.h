#include <sqlite3.h>
#include <string>
#pragma once

class SQLiteHelper {
public:
  SQLiteHelper() : MftInsert(NULL), UsnInsert(NULL), LogInsert(NULL),
                   EventInsert(NULL), EventUsnSelect(NULL), EventLogSelect(NULL),
                   Db(NULL) {}
  void init(std::string dbName, bool overwrite);
  void commit();
  void close();

  sqlite3_stmt *MftInsert, *UsnInsert, *LogInsert, *EventInsert, *EventUsnSelect, *EventLogSelect;
private:
  void finalizeStatements();
  int prepareStatement(sqlite3_stmt **stmt, std::string& sql);
  void prepareStatements();

  sqlite3* Db;
};
