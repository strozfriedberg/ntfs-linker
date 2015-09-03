#include "sqlite_helper.h"
#include "aggregate.h"

#include <iostream>
#include <fstream>

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
  rc = sqlite3_exec(Db, "BEGIN TRANSACTION", 0, 0, 0);
  if(rc) {
    std::cerr << "Error opening database" << std::endl;
    std::cerr << sqlite3_errmsg(Db) << std::endl;
    exit(1);
  }

  if(overwrite) {
    rc |= sqlite3_exec(Db, "drop table if exists mft;", 0, 0, 0);
    rc |= sqlite3_exec(Db, "drop table if exists logfile;", 0, 0, 0);
    rc |= sqlite3_exec(Db, "drop table if exists usnjrnl;", 0, 0, 0);
    rc |= sqlite3_exec(Db, "drop table if exists events;", 0, 0, 0);
  }
  rc |= sqlite3_exec(Db, "create table if not exists mft (" \
                         "LSN int, " \
                         "MFTRecNo int, " \
                         "ParMFTRecNo int, " \
                         "USN int, " \
                         "FileName text, " \
                         "isDir int, " \
                         "isAllocated int, " \
                         "sia_created text, " \
                         "sia_modified text, " \
                         "sia_mft_modified text, " \
                         "sia_accessed text, " \
                         "fna_created text, " \
                         "fna_modified text, " \
                         "fna_mft_modified text, " \
                         "fna_accessed text, " \
                         "logical_size text, " \
                         "physical_size text);",
                     0, 0, 0);
  rc |= sqlite3_exec(Db, "create table if not exists log (" \
                         "CurrentLSN int, " \
                         "PrevLSN int, " \
                         "UndoLSN int, " \
                         "ClientID int, " \
                         "RecordType int, " \
                         "RedoOP text, " \
                         "UndoOP text, " \
                         "TargetAttribute int, " \
                         "MFTClusterIndex int);",
                     0, 0, 0);
  rc |= sqlite3_exec(Db, "create table if not exists usn (" \
                         "MFTRecNo int, " \
                         "ParRecNo int, " \
                         "USN int, " \
                         "Timestamp text, " \
                         "Reason text, " \
                         "FileName text, " \
                         "PossiblePath text, " \
                         "PossibleParPath text);",
                     0, 0, 0);
  rc |= sqlite3_exec(Db, "create table if not exists events(" \
                         "MFTRecNo int, " \
                         "ParRecNo int, " \
                         "PreviousParRecNo int, " \
                         "USN_LSN int, " \
                         "Timestamp text, " \
                         "FileName text, " \
                         "PreviousFileName text, " \
                         "EventType int, " \
                         "EventSource int, " \
                         "IsEmbedded int, " \
                         "UNIQUE(USN_LSN, EventSource));",
                     0, 0, 0);
  prepareStatements();
}

void SQLiteHelper::commit() {
  int rc = sqlite3_exec(Db, "END TRANSACTION", 0, 0, 0);
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

void SQLiteHelper::prepareStatements() {
  int rc = 0;
  std::string mftInsert = "insert into mft values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  std::string usnInsert = "insert into usn values(?, ?, ?, ?, ?, ?, ?, ?);";
  std::string logInsert = "insert into log values(?, ?, ?, ?, ?, ?, ?, ?, ?);";
  std::string eventInsert = "insert or ignore into events values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  std::string eventSelect = "select * from events where EventSource=? order by USN_LSN desc";

  rc |= prepareStatement(&MftInsert, mftInsert);
  rc |= prepareStatement(&UsnInsert, usnInsert);
  rc |= prepareStatement(&LogInsert, logInsert);
  rc |= prepareStatement(&EventInsert, eventInsert);
  rc |= prepareStatement(&EventUsnSelect, eventSelect);
  rc |= prepareStatement(&EventLogSelect, eventSelect);

  sqlite3_bind_int64(EventUsnSelect, 1, EventSources::USN);
  sqlite3_bind_int64(EventLogSelect, 1, EventSources::LOG);

  if (rc) {
    std::cerr << "SQL Error " << rc << " at " << __FILE__ << ":" << __LINE__ << std::endl;
    std::cerr << sqlite3_errmsg(Db) << std::endl;
    sqlite3_close(Db);
    exit(2);
  }
}

void SQLiteHelper::finalizeStatements() {
  sqlite3_finalize(MftInsert);
  sqlite3_finalize(UsnInsert);
  sqlite3_finalize(LogInsert);
  sqlite3_finalize(EventInsert);
  sqlite3_finalize(EventUsnSelect);
  sqlite3_finalize(EventLogSelect);
}
