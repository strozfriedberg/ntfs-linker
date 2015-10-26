#include "aggregate.h"
#include "sqlite_util.h"

#include <fstream>
#include <iostream>
#include <sstream>

int busyHandler(__attribute__((unused)) void* foo, __attribute__((unused)) int num) {
  char input;
  std::cerr << "Database is busy. Cannot commit transaction." << std::endl;
  std::cerr << "Close any application which may have a lock on the database." << std::endl;
  std::cerr << "Try again? (y/n): ";
  std::cin >> input;
  if(input == 'y' || input == 'Y')
    return 1;
  else
    exit(0);

}

typedef std::vector<std::vector<std::string>>::const_iterator col_iter;
std::string getColList(col_iter first, col_iter last, int type) {
  std::stringstream ss;
  bool isFirst = true;
  for (col_iter cur = first; cur != last; ++cur) {
    if(!isFirst) {
      ss << ", ";
    }
    isFirst = false;
    switch(type) {
      case 0:
        ss << (*cur)[0] << " " << (*cur)[1];
        break;
      case 1:
        ss << (*cur)[0];
        break;
      case 2:
        ss << "?";
        break;
    }
  }
  return ss.str();
}

void SQLiteHelper::init(std::string dbName, bool overwrite) {
  int rc = 0;

  /*
  vacuum cleaning the db file doesn't seem to work
  Instead, I'll open the file for writing, then close it
  so that the file is truncated
  */
  if(overwrite) {
    std::ofstream file(dbName.c_str(), std::ios::trunc);
    file.close();
  }
  rc = sqlite3_open_v2(dbName.c_str(), &Db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
  if(rc) {
    std::cerr << "Error opening database" << std::endl;
    exit(1);
  }

  sqlite3_busy_handler(Db, &busyHandler, 0);
  beginTransaction();

  if(overwrite) {
    rc |= sqlite3_exec(Db, "drop table if exists log;", 0, 0, 0);
    rc |= sqlite3_exec(Db, "drop table if exists usn;", 0, 0, 0);
    rc |= sqlite3_exec(Db, "drop table if exists event;", 0, 0, 0);
  }
  rc |= sqlite3_exec(Db, std::string("create table if not exists log "
                                     "(" + getColList(LogColumns.begin(), LogColumns.end(), 0) + ");").c_str(),
                     0, 0, 0);
  rc |= sqlite3_exec(Db, std::string("create table if not exists usn "
                                    "(" + getColList(UsnColumns.begin(), UsnColumns.end(), 0) + ");").c_str(),
                     0, 0, 0);
  rc |= sqlite3_exec(Db, std::string("create table if not exists event "
                                     "(" + getColList(EventColumns.begin(), EventColumns.end(), 0) + ", "
                                     "UNIQUE(USN_LSN, EventSource, Volume));").c_str(),
                     0, 0, 0);
  if(rc) {
    std::cerr << "SQL Error " << rc << " at " << __FILE__ << ":" << __LINE__ << std::endl;
    std::cerr << sqlite3_errmsg(Db) << std::endl;
    sqlite3_close(Db);
    exit(2);
  }
  prepareStatements();
  endTransaction();
}

void SQLiteHelper::beginTransaction() {
  int rc = sqlite3_exec(Db, "BEGIN TRANSACTION", 0, 0, 0);
  if(rc) {
    std::cerr << "SQL Error " << rc << " at " << __FILE__ << ":" << __LINE__ << std::endl;
    std::cerr << sqlite3_errmsg(Db) << std::endl;
    sqlite3_close(Db);
    exit(2);
  }
}

void SQLiteHelper::endTransaction() {
  int rc = sqlite3_exec(Db, "END TRANSACTION", 0, 0, 0);
  if(rc) {
    std::cerr << "SQL Error " << rc << " at " << __FILE__ << ":" << __LINE__ << std::endl;
    std::cerr << sqlite3_errmsg(Db) << std::endl;
    sqlite3_close(Db);
    exit(2);
  }
}

void SQLiteHelper::makeTempOrderTable() {
  int rc = sqlite3_exec(Db, "create temporary table event_order(" \
                            "id int, " \
                            "position int)", 0, 0, 0);

  std::string eventUpdate = "insert into event_order (id, position) values (?, ?);";
  rc |= prepareStatement(&EventUpdate, eventUpdate);

  if(rc) {
    std::cerr << "SQL Error " << rc << " at " << __FILE__ << ":" << __LINE__ << std::endl;
    std::cerr << sqlite3_errmsg(Db) << std::endl;
    sqlite3_close(Db);
    exit(2);
  }
}

void SQLiteHelper::updateEventOrders() {
  std::string subquery = "(SELECT position FROM event_order WHERE event_order.id = event.id)";
  int rc = sqlite3_exec(Db, std::string("update event set position = " + subquery + " where exists " + subquery +";").c_str(), 0, 0, 0);
  if(rc) {
    std::cerr << "SQL Error " << rc << " at " << __FILE__ << ":" << __LINE__ << std::endl;
    std::cerr << sqlite3_errmsg(Db) << std::endl;
    sqlite3_close(Db);
    exit(2);
  }
  rc |= sqlite3_exec(Db, "drop table event_order;", 0, 0, 0);
  rc |= sqlite3_finalize(EventUpdate);
  if(rc) {
    std::cerr << "SQL Error " << rc << " at " << __FILE__ << ":" << __LINE__ << std::endl;
    std::cerr << sqlite3_errmsg(Db) << std::endl;
    sqlite3_close(Db);
    exit(2);
  }
}

void SQLiteHelper::close() {
  finalizeStatements();
  sqlite3_close(Db);
}

int SQLiteHelper::prepareStatement(sqlite3_stmt **stmt, std::string& sql) {
  return sqlite3_prepare_v2(Db, sql.c_str(), sql.length() + 1, stmt, NULL);
}

void SQLiteHelper::bindForSelect(const VersionInfo& version) {
  sqlite3_bind_int64(EventUsnSelect, 1, EventSources::SOURCE_USN);
  sqlite3_bind_int64(EventLogSelect, 1, EventSources::SOURCE_LOG);

  sqlite3_bind_text(EventUsnSelect, 2, version.Snapshot.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(EventLogSelect, 2, version.Snapshot.c_str(), -1, SQLITE_TRANSIENT);

  sqlite3_bind_text(EventUsnSelect, 3, version.Volume.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(EventLogSelect, 3, version.Volume.c_str(), -1, SQLITE_TRANSIENT);
}

void SQLiteHelper::resetSelect() {
  sqlite3_reset(EventUsnSelect);
  sqlite3_reset(EventLogSelect);
}

void SQLiteHelper::prepareStatements() {
  int rc = 0;
  std::string usnInsert = "insert into usn (" + getColList(UsnColumns.begin(), UsnColumns.end(), 1) + ") "
                                   "values (" + getColList(UsnColumns.begin(), UsnColumns.end(), 2) + ");";
  std::string logInsert = "insert into log (" + getColList(LogColumns.begin(), LogColumns.end(), 1) + ") "
                                   "values (" + getColList(LogColumns.begin(), LogColumns.end(), 2) + ");";
  // Events are processed from the oldest to the newest, so when an event with a conflicting (USN_LSN, EventSource)
  // comes into play, it should be ignored
  std::string eventInsert = "insert or ignore into event "
                            "(" + getColList(EventColumns.begin() + 2, EventColumns.end(), 1) + ") "
                            + "values (" + getColList(EventColumns.begin() + 2, EventColumns.end(), 2) + ");";
  std::string eventSelect = "select " + getColList(EventColumns.begin(), EventColumns.end(), 1) + " from event where EventSource=? and Snapshot=? and Volume=? order by USN_LSN desc";

  rc |= prepareStatement(&UsnInsert, usnInsert);
  rc |= prepareStatement(&LogInsert, logInsert);
  rc |= prepareStatement(&EventInsert, eventInsert);
  rc |= prepareStatement(&EventUsnSelect, eventSelect);
  rc |= prepareStatement(&EventLogSelect, eventSelect);

  if (rc) {
    std::cerr << "SQL Error " << rc << " at " << __FILE__ << ":" << __LINE__ << std::endl;
    std::cerr << sqlite3_errmsg(Db) << std::endl;
    sqlite3_close(Db);
    exit(2);
  }
}

void SQLiteHelper::finalizeStatements() {
  sqlite3_finalize(UsnInsert);
  sqlite3_finalize(LogInsert);
  sqlite3_finalize(EventInsert);
  sqlite3_finalize(EventUsnSelect);
  sqlite3_finalize(EventLogSelect);
}

const std::vector<std::vector<std::string>> SQLiteHelper::EventColumns = {
  { "id", "integer primary key autoincrement"},
  { "position", "int default -1"},
  { "MFTRecNo", "int"},
  { "ParRecNo", "int"},
  { "PreviousParRecNo", "int"},
  { "USN_LSN", "int"},
  { "Timestamp", "text"},
  { "FileName", "text"},
  { "PreviousFileName", "text"},
  { "EventType", "int"},
  { "EventSource", "int"},
  { "IsEmbedded", "int"},
  { "Offset", "int"},
  { "Created", "text"},
  { "Modified", "text"},
  { "Comment", "text"},
  { "Snapshot", "text"},
  { "Volume", "text"}
};

const std::vector<std::vector<std::string>> SQLiteHelper::LogColumns = {
  { "CurrentLSN", "int"},
  { "PrevLSN", "int"},
  { "UndoLSN", "int"},
  { "ClientID", "int"},
  { "RecordType", "int"},
  { "RedoOP", "text"},
  { "UndoOP", "text"},
  { "TargetAttribute", "int"},
  { "MFTClusterIndex", "int"},
  { "Offset", "int"},
  { "Snapshot", "text"},
  { "Volume", "text"}
};

const std::vector<std::vector<std::string>> SQLiteHelper::UsnColumns = {
  { "MFTRecNo", "int"},
  { "ParRecNo", "int"},
  { "USN", "int"},
  { "Timestamp", "text"},
  { "Reason", "text"},
  { "FileName", "text"},
  { "PossiblePath", "text"},
  { "PossibleParPath", "text"},
  { "Offset", "int"},
  { "Snapshot", "text"},
  { "Volume", "text" }
};
