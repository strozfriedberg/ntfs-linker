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

#include "util.h"
#include "progress.h"
#include "usn.h"

#include <cstring>
#include <memory>
#include <sqlite3.h>
#include <sstream>

/*
Returns the column names used for the Usn CSV file
*/
std::string getUSNColumnHeaders() {
  std::stringstream ss;
  ss << "MFT Record Number"    << "\t"
     << "Parent Record Number" << "\t"
     << "Usn"                  << "\t"
     << "Timestamp"            << "\t"
     << "Reason"               << "\t"
     << "Filename"             << "\t"
     << "Path"                 << "\t"
     << "Parent Path"          << "\t"
     << "File Offset"          << "\t"
     << "VSS Snapshot"         << "\t"
     << "File System Offset"            << std::endl;
  return ss.str();
}

/*
Decodes the Usn reason code
Usn uses a bit packing scheme to store reason codes.
Typically, as operations are performed on a file these reason codes are combined (|)
*/
std::string UsnRecord::getReasonString() {
  std::stringstream ss;
  ss << "USN";
  if (Reason & UsnReasons::USN_BASIC_INFO_CHANGE)         ss << "|BASIC_INFO_CHANGE";
  if (Reason & UsnReasons::USN_CLOSE)                     ss << "|CLOSE";
  if (Reason & UsnReasons::USN_COMPRESSION_CHANGE)        ss << "|COMPRESSION_CHANGE";
  if (Reason & UsnReasons::USN_DATA_EXTEND)               ss << "|DATA_EXTEND";
  if (Reason & UsnReasons::USN_DATA_OVERWRITE)            ss << "|DATA_OVERWRITE";
  if (Reason & UsnReasons::USN_DATA_TRUNCATION)           ss << "|DATA_TRUNCATION";
  if (Reason & UsnReasons::USN_EXTENDED_ATTRIBUTE_CHANGE) ss << "|EXTENDED_ATTRIBUTE_CHANGE";
  if (Reason & UsnReasons::USN_ENCRYPTION_CHANGE)         ss << "|ENCRYPTION_CHANGE";
  if (Reason & UsnReasons::USN_FILE_CREATE)               ss << "|FILE_CREATE";
  if (Reason & UsnReasons::USN_FILE_DELETE)               ss << "|FILE_DELETE";
  if (Reason & UsnReasons::USN_HARD_LINK_CHANGE)          ss << "|HARD_LINK_CHANGE";
  if (Reason & UsnReasons::USN_INDEXABLE_CHANGE)          ss << "|INDEXABLE_CHANGE";
  if (Reason & UsnReasons::USN_NAMED_DATA_EXTEND)         ss << "|NAMED_DATA_EXTEND";
  if (Reason & UsnReasons::USN_NAMED_DATA_OVERWRITE)      ss << "|NAMED_DATA_OVERWRITE";
  if (Reason & UsnReasons::USN_NAMED_DATA_TRUNCATION)     ss << "|NAMED_DATA_TRUNCATION";
  if (Reason & UsnReasons::USN_OBJECT_ID_CHANGE)          ss << "|OBJECT_ID_CHANGE";
  if (Reason & UsnReasons::USN_RENAME_NEW_NAME)           ss << "|RENAME_NEW_NAME";
  if (Reason & UsnReasons::USN_RENAME_OLD_NAME)           ss << "|RENAME_OLD_NAME";
  if (Reason & UsnReasons::USN_REPARSE_POINT_CHANGE)      ss << "|REPARSE_POINT_CHANGE";
  if (Reason & UsnReasons::USN_SECURITY_CHANGE)           ss << "|SECURITY_CHANGE";
  if (Reason & UsnReasons::USN_STREAM_CHANGE)             ss << "|STREAM_CHANGE";
  return ss.str();
}

std::streampos advanceStream(std::istream& stream, char* buffer, bool sparse) {
  /**
   * Handle sparse $J file.
   * Seeks to end, then advances backwards until an all zero block is found
   * Does NOT set the stream position to last all zero block, just somewhere near the end
   * Returns the streampos of the end
   */
  stream.clear();
  stream.seekg(0, std::ios::end);
  std::streampos end = stream.tellg();
  if (sparse) {
    bool done = false;
    while (!done) {
      stream.seekg(-(1 << 20), std::ios::cur);
      if (stream.fail()) {
        done = true;
        stream.clear();
        stream.seekg(0, std::ios::beg);
        stream.read(buffer, std::min(USN_BUFFER_SIZE, static_cast<unsigned int>(end - stream.tellg())));
      }
      else {
        stream.read(buffer, std::min(USN_BUFFER_SIZE, static_cast<unsigned int>(end - stream.tellg())));
        done = true;
        for (unsigned int i = 0; i < USN_BUFFER_SIZE && done; i++) {
          if (buffer[i] != 0)
            done = false;
        }
      }
    }
  }
  else {
    stream.seekg(0, std::ios::beg);
    stream.read(buffer, std::min(USN_BUFFER_SIZE, static_cast<unsigned int>(end - stream.tellg())));
  }
  return end;
}

/*
Parses all records found in the USN file represented by input. Uses the records map to recreate file paths
Outputs the results to several streams.
*/
void parseUSN(const std::vector<File>& records, SQLiteHelper& sqliteHelper, std::istream& input, std::ostream& output, const VersionInfo& version, bool extra) {
  std::unique_ptr<char[]> bufPtr(new char[USN_BUFFER_SIZE]);
  char* buffer = bufPtr.get();

  int records_processed = -1;

  std::streampos end = advanceStream(input, buffer, true);
  std::streampos start = input.tellg();
  ProgressBar status(end - start);

  UsnRecord prevRec(version);
  output << getUSNColumnHeaders();

  unsigned int offset = 0;
  unsigned int totalOffset = 0;
  uint64_t usn_offset = UINT64_MAX;

  //scan through the $USNJrnl one record at a time. Each record is variable length.
  bool done = false;
  while (!input.eof() && !done) {
    status.setDone((uint64_t) input.tellg() - USN_BUFFER_SIZE + offset - start);

    if (offset + 4 > USN_BUFFER_SIZE || hex_to_long(buffer + offset, 4) + offset > USN_BUFFER_SIZE) {
      // We've reached the end of the buffer. Move the record to the front,
      // then read to fill out the rest of the buffer
      memmove(buffer, buffer + offset, USN_BUFFER_SIZE - offset);
      input.read(buffer + USN_BUFFER_SIZE - offset, offset);
      totalOffset += offset;
      offset = 0;
    }

    uint64_t record_length = hex_to_long(buffer + offset, 4);

    if (record_length == 0) {
      offset += 8;
      continue;
    }
    if (record_length > USN_BUFFER_SIZE) {
      status.clear();
      std::cerr << "Encountered bad record at 0x"
                << std::hex << static_cast<int>(input.tellg()) - USN_BUFFER_SIZE + offset
                << " in snapshot: " << version.Snapshot << ".";
      int new_offset = recoverPosition(buffer, offset, usn_offset + (static_cast<int>(input.tellg()) - USN_BUFFER_SIZE + offset));
      if (new_offset >= 0) {
        std::cerr << " Recovery successful with 0x" << std::hex << new_offset - offset << " bytes skipped" << std::endl;
        offset = new_offset;
        continue;
      }
      else {
        // Try once to read another page, but no more
        input.read(buffer, USN_BUFFER_SIZE);
        totalOffset += USN_BUFFER_SIZE;
        offset = 0;
        int new_offset = recoverPosition(buffer, offset, usn_offset);
        if (new_offset >= 0) {
          std::cerr << " Recovery successful with 0x" << std::hex << new_offset - offset + USN_BUFFER_SIZE
                    << " bytes skipped" << std::endl;
          offset = new_offset;
          continue;
        }
        else {
          std::cerr << " Recovery failed. Cannot continue parsing this $UsnJrnl file." << std::endl;
          break;
        }
      }
    }
    records_processed++;

    UsnRecord rec(buffer + offset, offset + totalOffset, version);

    if (usn_offset == UINT64_MAX) {
      usn_offset = rec.Usn - (static_cast<int>(input.tellg()) - USN_BUFFER_SIZE + offset);
    }
    else if (usn_offset != rec.Usn - (static_cast<int>(input.tellg()) - USN_BUFFER_SIZE + offset) && !input.eof()) {
      std::cerr << "Inconsistent Usn value found at 0x" << std::hex << static_cast<int>(input.tellg()) - offset
                << " in snapshot << " << version.Snapshot
                << ". Update sequence number does not match the offset of the record in the file" << std::endl;
      usn_offset = rec.Usn - (static_cast<int>(input.tellg()) - USN_BUFFER_SIZE + offset);
    }

    if (extra) {
      output << rec.toString(records);
      rec.insert(sqliteHelper.UsnInsert, records);
    }

    if (prevRec.Record != rec.Record || prevRec.Reason & UsnReasons::USN_CLOSE) {
      prevRec.checkTypeAndInsert(sqliteHelper.EventInsert);
      prevRec.clearFields();
    }
    if (prevRec.Usn == 0)
      prevRec = rec;
    prevRec.update(rec);

    offset += record_length;
  }
  if (prevRec.Usn != 0) {
    prevRec.checkTypeAndInsert(sqliteHelper.EventInsert);
  }
  status.finish();
}

int recoverPosition(const char* buffer, unsigned int offset, unsigned int usn_offset) {
  // Look for the offset of the next valid USN record
  // In some instances, entire sectors are replaced with junk data
  // buffer is the current buffer, and offset is the offset within it
  // usn_offset is the offset of the _start_ of buffer in the $J stream, including the leading sparse section
  // Care should be taken that no reads are performed past buffer + USN_BUFFER_SIZE

  // The strategy is to read 8-byte longs until one is found whose value matches its own offset
  // That record may be invalid (with some of the first 0x18 bytes chopped off, so we return
  // the offset to the next record

  // Ensure offset is 8-byte aligned
  offset = 8 * ceilingDivide(offset, 8);
  bool found = false;
  while (offset < USN_BUFFER_SIZE) {
    uint64_t value = hex_to_long(buffer + offset, 8);
    if (value == offset + usn_offset - 0x18) {
     if (found)
        return offset - 0x18;
     found = true;
    }
    offset += 8;
  }
  return -1;
}

void UsnRecord::update(UsnRecord rec) {
    Reason |= rec.Reason;

    if (rec.Reason & UsnReasons::USN_RENAME_OLD_NAME) {
      PreviousName = rec.Name;
      PreviousParent = rec.Parent;
    }

    if (rec.Reason & UsnReasons::USN_RENAME_NEW_NAME) {
      Name = rec.Name;
      Parent = rec.Parent;
    }
}

UsnRecord::UsnRecord(const char* buffer, uint64_t fileOffset, const VersionInfo& version, int len, bool isEmbedded) :
  FileOffset(fileOffset),
  Snapshot(version.Snapshot),
  Volume(version.Volume),
  IsEmbedded(isEmbedded) {
  if (len < 0 || (unsigned) len >= 0x3C) {
    PreviousName                     = "";
    PreviousParent                   = -1;
    uint64_t record_length           = hex_to_long(buffer, 4);
    Record                           = hex_to_long(buffer + 0x8, 6);
    Reference                        = hex_to_long(buffer + 0x8, 8);
    Parent                           = hex_to_long(buffer + 0x10, 6);
    ParentReference                  = hex_to_long(buffer + 0x10, 8);
    Usn                              = hex_to_long(buffer + 0x18, 8);
    Timestamp                        = filetime_to_iso_8601(hex_to_long(buffer + 0x20, 8));
    Reason                           = hex_to_long(buffer + 0x28, 4);
    unsigned int name_len            = hex_to_long(buffer + 0x38, 2);
    unsigned int name_offset         = hex_to_long(buffer + 0x3A, 2);

    if (len < 0 || (unsigned) len >= record_length) {
      Name = mbcatos(buffer + name_offset, name_len);
      return;
    }
  }
  // Not a valid UsnRecord
  clearFields();
}

void UsnRecord::clearFields() {

  Name            = "";
  Reference       = 0;
  Record          = -1;
  ParentReference = 0;
  Parent          = -1;
  PreviousName    = "";
  PreviousParent  = -1;
  Reason          = 0;
  Timestamp       = "";
  Usn             = 0;
  FileOffset      = 0;
}

UsnRecord::UsnRecord(const VersionInfo& version, bool isEmbedded) : Snapshot(version.Snapshot), Volume(version.Volume), IsEmbedded(isEmbedded) {
  IsEmbedded = false;
  clearFields();
}

std::string UsnRecord::toString(const std::vector<File>& records) {
  std::stringstream ss;
  ss << Record                       << "\t"
     << Parent                       << "\t"
     << Usn                          << "\t"
     << Timestamp                    << "\t"
     << getReasonString()            << "\t"
     << Name                         << "\t"
     << getFullPath(records, Record) << "\t"
     << getFullPath(records, Parent) << "\t"
     << FileOffset                   << "\t"
     << Snapshot                     << "\t"
     << Volume                       << std::endl;
  return ss.str();
}

void UsnRecord::insertEvent(unsigned int type, sqlite3_stmt* stmt) {
  unsigned int i = 0;
  sqlite3_bind_int64(stmt, ++i, Record);
  sqlite3_bind_int64(stmt, ++i, Parent);
  sqlite3_bind_int64(stmt, ++i, PreviousParent);
  sqlite3_bind_int64(stmt, ++i, Usn);
  sqlite3_bind_text(stmt , ++i, Timestamp.c_str()   , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt , ++i, Name.c_str()        , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt , ++i, PreviousName.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, ++i, type);
  sqlite3_bind_int64(stmt, ++i, EventSources::SOURCE_USN);
  sqlite3_bind_int  (stmt, ++i, IsEmbedded);
  sqlite3_bind_int64(stmt, ++i, FileOffset);
  sqlite3_bind_text (stmt, ++i, "", -1, SQLITE_TRANSIENT);  // Created
  sqlite3_bind_text (stmt, ++i, "", -1, SQLITE_TRANSIENT);  // Modified
  sqlite3_bind_text (stmt, ++i, "", -1, SQLITE_TRANSIENT);  // Comment
  sqlite3_bind_text (stmt, ++i, Snapshot.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, ++i, Volume.c_str(), -1, SQLITE_TRANSIENT);

  sqlite3_step(stmt);
  sqlite3_reset(stmt);
}

void UsnRecord::insert(sqlite3_stmt* stmt, const std::vector<File>& records) {
  unsigned int i = 0;
  sqlite3_bind_int64(stmt, ++i, Record);
  sqlite3_bind_int64(stmt, ++i, Parent);
  sqlite3_bind_int64(stmt, ++i, Usn);
  sqlite3_bind_text (stmt, ++i, Timestamp.c_str()                   , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, ++i, getReasonString().c_str()           , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, ++i, Name.c_str()                        , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, ++i, getFullPath(records, Record).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, ++i, getFullPath(records, Parent).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, ++i, FileOffset);
  sqlite3_bind_text (stmt, ++i, Snapshot.c_str()                    , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, ++i, Volume.c_str()                      , -1, SQLITE_TRANSIENT);

  sqlite3_step(stmt);
  sqlite3_reset(stmt);
}

void UsnRecord::checkTypeAndInsert(sqlite3_stmt* stmt, bool strict) {
  if (Reason & UsnReasons::USN_FILE_CREATE)
    insertEvent(EventTypes::TYPE_CREATE, stmt);
  if (Reason & UsnReasons::USN_FILE_DELETE)
    insertEvent(EventTypes::TYPE_DELETE, stmt);
  if (PreviousName != Name
      && (Reason & (UsnReasons::USN_RENAME_NEW_NAME | UsnReasons::USN_RENAME_OLD_NAME))
      && (PreviousName != "" || !strict))
    insertEvent(EventTypes::TYPE_RENAME, stmt);
  if (Parent != PreviousParent
      && (Reason & (UsnReasons::USN_RENAME_NEW_NAME | UsnReasons::USN_RENAME_OLD_NAME))
      && (PreviousParent != -1 || !strict))
    insertEvent(EventTypes::TYPE_MOVE, stmt);
}
