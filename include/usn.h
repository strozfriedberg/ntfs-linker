#include "file.h"
#include "sqlite_helper.h"

#include <sqlite3.h>
#include <map>
#include <iostream>

#pragma once

std::string getUSNColumnHeaders();

void parseUSN(const std::vector<File>& records, SQLiteHelper& sqliteHelper, std::istream& input = std::cin, std::ostream& output = std::cout);

class UsnRecord {
public:
  UsnRecord();
  UsnRecord(const char* buffer, uint64_t fileOffset, int len = -1, bool isEmbedded=false);

  std::string getReasonString();
  std::string toCreateString(const  std::vector<File> &records);
  std::string toDeleteString(const  std::vector<File> &records);
  std::string toMoveString(const    std::vector<File> &records);
  std::string toRenameString(const  std::vector<File> &records);
  std::string toString(const        std::vector<File> &records);

  void checkTypeAndInsert(sqlite3_stmt* stmt);
  void update(UsnRecord rec);
  void clearFields();

  void insert(sqlite3_stmt* stmt, const std::vector<File>& records);
  void insertEvent(unsigned int type, sqlite3_stmt* stmt);

  uint64_t Reference, ParentReference, Usn, FileOffset;
  int64_t Record, Parent, PreviousParent;
  unsigned int Reason;
  bool IsEmbedded;
  std::string Name, PreviousName, Timestamp;
};

namespace UsnReasons {
  const unsigned int BASIC_INFO_CHANGE         = 0x00008000;
  const unsigned int CLOSE                     = 0x80000000;
  const unsigned int COMPRESSION_CHANGE        = 0x00020000;
  const unsigned int DATA_EXTEND               = 0x00000002;
  const unsigned int DATA_OVERWRITE            = 0x00000001;
  const unsigned int DATA_TRUNCATION           = 0x00000004;
  const unsigned int EXTENDED_ATTRIBUTE_CHANGE = 0x00000400;
  const unsigned int ENCRYPTION_CHANGE         = 0x00040000;
  const unsigned int FILE_CREATE               = 0x00000100;
  const unsigned int FILE_DELETE               = 0x00000200;
  const unsigned int HARD_LINK_CHANGE          = 0x00010000;
  const unsigned int INDEXABLE_CHANGE          = 0x00004000;
  const unsigned int NAMED_DATA_EXTEND         = 0x00000020;
  const unsigned int NAMED_DATA_OVERWRITE      = 0x00000010;
  const unsigned int NAMED_DATA_TRUNCATION     = 0x00000040;
  const unsigned int OBJECT_ID_CHANGE          = 0x00080000;
  const unsigned int RENAME_NEW_NAME           = 0x00002000;
  const unsigned int RENAME_OLD_NAME           = 0x00001000;
  const unsigned int REPARSE_POINT_CHANGE      = 0x00100000;
  const unsigned int SECURITY_CHANGE           = 0x00000800;
  const unsigned int STREAM_CHANGE             = 0x00200000;
}
