#include "util.h"
#include "progress.h"
#include "usn.h"

#include <cstring>
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
     << "VSS Snapshot"         << std::endl;
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
  if (Reason & UsnReasons::BASIC_INFO_CHANGE)         ss << "|BASIC_INFO_CHANGE";
  if (Reason & UsnReasons::CLOSE)                     ss << "|CLOSE";
  if (Reason & UsnReasons::COMPRESSION_CHANGE)        ss << "|COMPRESSION_CHANGE";
  if (Reason & UsnReasons::DATA_EXTEND)               ss << "|DATA_EXTEND";
  if (Reason & UsnReasons::DATA_OVERWRITE)            ss << "|DATA_OVERWRITE";
  if (Reason & UsnReasons::DATA_TRUNCATION)           ss << "|DATA_TRUNCATION";
  if (Reason & UsnReasons::EXTENDED_ATTRIBUTE_CHANGE) ss << "|EXTENDED_ATTRIBUTE_CHANGE";
  if (Reason & UsnReasons::ENCRYPTION_CHANGE)         ss << "|ENCRYPTION_CHANGE";
  if (Reason & UsnReasons::FILE_CREATE)               ss << "|FILE_CREATE";
  if (Reason & UsnReasons::FILE_DELETE)               ss << "|FILE_DELETE";
  if (Reason & UsnReasons::HARD_LINK_CHANGE)          ss << "|HARD_LINK_CHANGE";
  if (Reason & UsnReasons::INDEXABLE_CHANGE)          ss << "|INDEXABLE_CHANGE";
  if (Reason & UsnReasons::NAMED_DATA_EXTEND)         ss << "|NAMED_DATA_EXTEND";
  if (Reason & UsnReasons::NAMED_DATA_OVERWRITE)      ss << "|NAMED_DATA_OVERWRITE";
  if (Reason & UsnReasons::NAMED_DATA_TRUNCATION)     ss << "|NAMED_DATA_TRUNCATION";
  if (Reason & UsnReasons::OBJECT_ID_CHANGE)          ss << "|OBJECT_ID_CHANGE";
  if (Reason & UsnReasons::RENAME_NEW_NAME)           ss << "|RENAME_NEW_NAME";
  if (Reason & UsnReasons::RENAME_OLD_NAME)           ss << "|RENAME_OLD_NAME";
  if (Reason & UsnReasons::REPARSE_POINT_CHANGE)      ss << "|REPARSE_POINT_CHANGE";
  if (Reason & UsnReasons::SECURITY_CHANGE)           ss << "|SECURITY_CHANGE";
  if (Reason & UsnReasons::STREAM_CHANGE)             ss << "|STREAM_CHANGE";
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
void parseUSN(const std::vector<File>& records, SQLiteHelper& sqliteHelper, std::istream& input, std::ostream& output, std::string snapshot) {
  static char buffer[USN_BUFFER_SIZE];

  int records_processed = -1;

  std::streampos end = advanceStream(input, buffer, true);
  std::streampos start = input.tellg();
  ProgressBar status(end - start);

  UsnRecord prevRec(snapshot);
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
      std::cerr << std::hex << record_length << " is an awfully large record_length! Attempting to recover" << std::endl;
      int new_offset = recoverPosition(buffer, offset, usn_offset + (static_cast<int>(input.tellg()) - USN_BUFFER_SIZE + offset));
      if (new_offset > 0) {
        std::cerr << "Recovery successful with 0x" << std::hex << new_offset - offset << " bytes skipped" << std::endl;
        offset = new_offset;
        continue;
      }
      else {
        std::cerr << "Recovery failed. Cannot continue. Check that we're not missing much at "
          << std::hex << static_cast<int>(input.tellg()) - USN_BUFFER_SIZE + offset << std::endl;
        break;
      }
    }
    records_processed++;

    UsnRecord rec(buffer + offset, offset + totalOffset, snapshot);

    if (usn_offset == UINT64_MAX) {
      usn_offset = rec.Usn - (static_cast<int>(input.tellg()) - USN_BUFFER_SIZE + offset);
    }
    else if (usn_offset != rec.Usn - (static_cast<int>(input.tellg()) - USN_BUFFER_SIZE + offset) && !input.eof()) {
      std::cerr << "Inconsistent Usn value found at " << static_cast<int>(input.tellg()) - offset << std::endl;
      usn_offset = rec.Usn - (static_cast<int>(input.tellg()) - USN_BUFFER_SIZE + offset);
    }
    output << rec.toString(records);
    rec.insert(sqliteHelper.UsnInsert, records);

    if (prevRec.Record != rec.Record || prevRec.Reason & UsnReasons::CLOSE) {
      prevRec.checkTypeAndInsert(sqliteHelper.EventInsert);
      prevRec.clearFields();
    }
    if (prevRec.Usn == 0)
      prevRec = rec;
    prevRec.update(rec);

    offset += record_length;
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

    if (rec.Reason & UsnReasons::RENAME_OLD_NAME) {
      PreviousName = rec.Name;
      PreviousParent = rec.Parent;
    }

    if (rec.Reason & UsnReasons::RENAME_NEW_NAME) {
      Name = rec.Name;
      Parent = rec.Parent;
    }
}

UsnRecord::UsnRecord(const char* buffer, uint64_t fileOffset, std::string snapshot, int len, bool isEmbedded) :
  FileOffset(fileOffset),
  Snapshot(snapshot),
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
      Name = mbcatos(buffer + name_offset, name_len / 2);
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

UsnRecord::UsnRecord(std::string snapshot) : Snapshot(snapshot) {
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
     << Snapshot                     << std::endl;
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
  sqlite3_bind_int64(stmt, ++i, EventSources::USN);
  sqlite3_bind_int64(stmt, ++i, IsEmbedded);
  sqlite3_bind_int64(stmt, ++i, FileOffset);
  sqlite3_bind_text (stmt, ++i, "", -1, SQLITE_TRANSIENT);  // Created
  sqlite3_bind_text (stmt, ++i, "", -1, SQLITE_TRANSIENT);  // Modified
  sqlite3_bind_text (stmt, ++i, "", -1, SQLITE_TRANSIENT);  // Comment
  sqlite3_bind_text (stmt, ++i, Snapshot.c_str(), -1, SQLITE_TRANSIENT);

  sqlite3_step(stmt);
  sqlite3_reset(stmt);
}

void UsnRecord::insert(sqlite3_stmt* stmt, const std::vector<File>& records) {
  unsigned int i = 0;
  sqlite3_bind_int64(stmt, ++i, Record);
  sqlite3_bind_int64(stmt, ++i, Parent);
  sqlite3_bind_int64(stmt, ++i, Usn);
  sqlite3_bind_text(stmt , ++i, Timestamp.c_str()                   , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt , ++i, getReasonString().c_str()           , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt , ++i, Name.c_str()                        , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt , ++i, getFullPath(records, Record).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt , ++i, getFullPath(records, Parent).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, ++i, FileOffset);
  sqlite3_bind_text(stmt,  ++i, ""                                  , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt,  ++i, ""                                  , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt,  ++i, ""                                  , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, ++i, Snapshot.c_str(), -1, SQLITE_TRANSIENT);

  sqlite3_step(stmt);
  sqlite3_reset(stmt);
}

void UsnRecord::checkTypeAndInsert(sqlite3_stmt* stmt) {
  if (Reason & UsnReasons::FILE_CREATE)
    insertEvent(EventTypes::CREATE, stmt);
  if (Reason & UsnReasons::FILE_DELETE)
    insertEvent(EventTypes::DELETE, stmt);
  if (PreviousName != Name
      && (Reason & (UsnReasons::RENAME_NEW_NAME | UsnReasons::RENAME_OLD_NAME))
      && PreviousName != "")
    insertEvent(EventTypes::RENAME, stmt);
  if (Parent != PreviousParent
      && (Reason & (UsnReasons::RENAME_NEW_NAME | UsnReasons::RENAME_OLD_NAME))
      && PreviousParent != -1)
    insertEvent(EventTypes::MOVE, stmt);
}
