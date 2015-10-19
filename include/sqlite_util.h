#pragma once

#include <sqlite3.h>
#include <string>

class SQLiteHelper {
public:
  SQLiteHelper() : UsnInsert(NULL), LogInsert(NULL),
                   EventInsert(NULL), EventUsnSelect(NULL), EventLogSelect(NULL),
                   Db(NULL) {}
  void init(std::string dbName, bool overwrite);
  void commit();
  void close();
  void bindForSelect(std::string snapshot);
  void resetSelect();

  sqlite3_stmt *UsnInsert, *LogInsert, *EventInsert, *EventUsnSelect, *EventLogSelect;
private:
  void finalizeStatements();
  int prepareStatement(sqlite3_stmt **stmt, std::string& sql);
  void prepareStatements();

  sqlite3* Db;
};
