#pragma once

#include "file.h"
#include "sqlite_util.h"
#include "usn.h"

#include <iostream>
#include <string>
#include <vector>

/*
returns the meaning of the operation code
*/
std::string decodeLogFileOpCode(int op);

/*
Parses the $LogFile stream input
Writes output to designated streams
*/
void parseLog(const std::vector<File>& records, SQLiteHelper& sqliteHelper, std::istream& input, std::ostream& output, const VersionInfo& version, bool extra);

class LogRecord {
public:
  LogRecord(const VersionInfo& version) : Snapshot(version.Snapshot), Volume(version.Volume) {}

  int init(char* buffer, uint64_t offset, bool prev_has_next);
  void clearFields();
  void insert(sqlite3_stmt* stmt);
  static std::string getColumnHeaders();

  uint64_t CurrentLsn, PreviousLsn, UndoLsn, Offset;
  unsigned int ClientId, RecordType, Flags, RedoOp, UndoOp, RedoOffset, RedoLength, UndoOffset, UndoLength;
  unsigned int TargetAttribute, LcnsToFollow, RecordOffset, AttributeOffset, MftClusterIndex, TargetVcn, TargetLcn;
  unsigned int ClientDataLength;
  char* Data;
  std::string Snapshot, Volume;
};
std::ostream& operator<<(std::ostream& out, const LogRecord& rec);

class LogData {
public:
  LogData(const VersionInfo& version) : Snapshot(version.Snapshot), Volume(version.Volume), PrevUsnRecord(version) {}

  void clearFields();
  void processLogRecord(const std::vector<File>& records, LogRecord& rec, SQLiteHelper& sqliteHelper, uint64_t fileOffset);
  std::string pickName(std::string a, std::string b);
  std::string toCreateString(std::vector<File>& records);
  std::string toDeleteString(std::vector<File>& records);
  std::string toRenameString(std::vector<File>& records);
  std::string toMoveString(std::vector<File>& records);
  void insertEvent(unsigned int type, sqlite3_stmt* stmt);
  bool isCreateEvent();
  bool isDeleteEvent();
  bool isRenameEvent();
  bool isMoveEvent();
  bool isTransactionOver();

  int64_t Record, Parent, PreviousParent, Offset;
  uint64_t Lsn;
  std::string Name, PreviousName, Timestamp, Created, Modified, Comment, Snapshot, Volume;
  std::vector<int> RedoOps, UndoOps;

  static const std::vector<int> createRedo, createUndo, deleteRedo, deleteUndo;
  static const std::vector<int> renameRedo, renameUndo, writeRedo, writeUndo;
  UsnRecord PrevUsnRecord;
private:
  /*
  Used for $LogFile event analysis
  returns whether the transaction run represented by redo1, undo1 "matches" redo2, undo2
  Here matching means that redo2 is a subsequence of redo1, and undo2 is a subsequence of undo1
  if interchange is set, then it is considered that 0xc == 0xe and 0xd == 0xf
  */
  bool transactionRunMatch(const std::vector<int>& redo2, const std::vector<int>& undo2, bool interchange = true);
};

namespace LogOps {
    const int NOOP                              = 0x00;
    const int COMPENSATION_LOG_RECORD           = 0x01;
    const int INITIALIZE_FILE_RECORD_SEGMENT    = 0x02;
    const int DEALLOCATE_FILE_RECORD_SEGMENT    = 0x03;
    const int WRITE_END_OF_FILE_RECORD_SEGMENT  = 0x04;
    const int CREATE_ATTRIBUTE                  = 0x05;
    const int DELETE_ATTRIBUTE                  = 0x06;
    const int UPDATE_RESIDENT_VALUE             = 0x07;
    const int UPDATE_NONRESIDENT_VALUE          = 0x08;
    const int UPDATE_MAPPING_PAIRS              = 0x09;
    const int DELETE_DIRTY_CLUSTERS             = 0x0A;
    const int SET_NEW_ATTRIBUTE_SIZES           = 0x0B;
    const int ADD_INDEX_ENTRY_ROOT              = 0x0C;
    const int DELETE_INDEX_ENTRY_ROOT           = 0x0D;
    const int ADD_INDEX_ENTRY_ALLOCATION        = 0x0E;
    const int DELETE_INDEX_ENTRY_ALLOCATION     = 0x0F;
    const int SET_INDEX_ENTRY_VCN_ALLOCATION    = 0x12;
    const int UPDATE_FILE_NAME_ROOT             = 0x13;
    const int UPDATE_FILE_NAME_ALLOCATION       = 0x14;
    const int SET_BITS_IN_NONRESIDENT_BIT_MAP   = 0x15;
    const int CLEAR_BITS_IN_NONRESIDENT_BIT_MAP = 0x16;
    const int PREPARE_TRANSACTION               = 0x19;
    const int COMMIT_TRANSACTION                = 0x1A;
    const int FORGET_TRANSACTION                = 0x1B;
    const int OPEN_NONRESIDENT_ATTRIBUTE        = 0x1C;
    const int DIRTY_PAGE_TABLE_DUMP             = 0x1F;
    const int TRANSACTION_TABLE_DUMP            = 0x20;
    const int UPDATE_RECORD_DATA_ROOT           = 0x21;
}
