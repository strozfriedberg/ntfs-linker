/*
 * ntfs-linker
 * Copyright 2015 Stroz Friedberg, LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License is available at
 * <http://www.gnu.org/licenses/>.
 *
 * You can contact Stroz Friedberg by electronic and paper mail as follows:
 *
 * Stroz Friedberg, LLC
 * 32 Avenue of the Americas
 * 4th Floor
 * New York, NY, 10013
 * info@strozfriedberg.com
 */

#pragma once

#include "file.h"
#include "sqlite_util.h"

#include <iostream>
#include <string>

const unsigned int USN_BUFFER_SIZE = 65536;

std::string getUSNColumnHeaders();

std::streampos advanceStream(std::istream& stream, char* buffer, bool sparse=false);

void parseUSN(const std::vector<File>& records, SQLiteHelper& sqliteHelper, std::istream& input, std::ostream& output, const VersionInfo& version, bool extra);

int recoverPosition(const char* buffer, unsigned int offset, unsigned int usn_offset);

class UsnRecord {
public:
  UsnRecord(const VersionInfo& version, bool isEmbedded=false);
  UsnRecord(const char* buffer, uint64_t fileOffset, const VersionInfo& version, int len = -1, bool isEmbedded=false);

  std::string getReasonString();
  std::string toCreateString(const  std::vector<File> &records);
  std::string toDeleteString(const  std::vector<File> &records);
  std::string toMoveString(const    std::vector<File> &records);
  std::string toRenameString(const  std::vector<File> &records);
  std::string toString(const        std::vector<File> &records);

  void checkTypeAndInsert(sqlite3_stmt* stmt, bool strict=true);
  void update(UsnRecord rec);
  void clearFields();

  void insert(sqlite3_stmt* stmt, const std::vector<File>& records);
  void insertEvent(unsigned int type, sqlite3_stmt* stmt);

  uint64_t Reference, ParentReference, Usn, FileOffset;
  int64_t Record, Parent, PreviousParent;
  unsigned int Reason;
  std::string Name, PreviousName, Timestamp, Snapshot, Volume;
  bool IsEmbedded;
};

namespace UsnReasons {
  const unsigned int USN_BASIC_INFO_CHANGE         = 0x00008000;
  const unsigned int USN_CLOSE                     = 0x80000000;
  const unsigned int USN_COMPRESSION_CHANGE        = 0x00020000;
  const unsigned int USN_DATA_EXTEND               = 0x00000002;
  const unsigned int USN_DATA_OVERWRITE            = 0x00000001;
  const unsigned int USN_DATA_TRUNCATION           = 0x00000004;
  const unsigned int USN_EXTENDED_ATTRIBUTE_CHANGE = 0x00000400;
  const unsigned int USN_ENCRYPTION_CHANGE         = 0x00040000;
  const unsigned int USN_FILE_CREATE               = 0x00000100;
  const unsigned int USN_FILE_DELETE               = 0x00000200;
  const unsigned int USN_HARD_LINK_CHANGE          = 0x00010000;
  const unsigned int USN_INDEXABLE_CHANGE          = 0x00004000;
  const unsigned int USN_NAMED_DATA_EXTEND         = 0x00000020;
  const unsigned int USN_NAMED_DATA_OVERWRITE      = 0x00000010;
  const unsigned int USN_NAMED_DATA_TRUNCATION     = 0x00000040;
  const unsigned int USN_OBJECT_ID_CHANGE          = 0x00080000;
  const unsigned int USN_RENAME_NEW_NAME           = 0x00002000;
  const unsigned int USN_RENAME_OLD_NAME           = 0x00001000;
  const unsigned int USN_REPARSE_POINT_CHANGE      = 0x00100000;
  const unsigned int USN_SECURITY_CHANGE           = 0x00000800;
  const unsigned int USN_STREAM_CHANGE             = 0x00200000;
}
