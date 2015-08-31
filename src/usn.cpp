#include "helper_functions.h"
#include <sqlite3.h>
#include "usn.h"
#include "progress.h"

/*
Returns the column names used for the Usn CSV file
*/
std::string getUSNColumnHeaders() {
  return "MFTRecordNumber\tParentMFTRecordNumber\tUsn\tTimestamp\tReason\tFileName\tPossiblePath\tPossibleParentPath";
}

const unsigned int USN_BUFFER_SIZE = 2097152;

/*
Decodes the Usn reason code
Usn uses a bit packing scheme to store reason codes.
Typically, as operations are performed on a file these reason codes are combined (&)
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

std::streampos advanceStream(std::istream& stream, char* buffer, bool sparse=false) {
  /**
   * Handle sparse $J file.
   * Seeks to end, then advances backwards until an all zero block is found
   * Does NOT set the stream position to last all zero block, just somewhere near the end
   * Returns the streampos of the end
   */
  stream.seekg(0, std::ios::end);
  std::streampos end = stream.tellg();
  if (sparse) {
    bool done = false;
    while (!done) {
      stream.seekg(-(1 << 20), std::ios::cur);
      stream.read(buffer, USN_BUFFER_SIZE);
      done = true;
      for (unsigned int i = 0; i < USN_BUFFER_SIZE && done; i++) {
        if (buffer[i] != 0)
          done = false;
      }
    }
  }
  else {
    stream.seekg(0, std::ios::beg);
    stream.read(buffer, USN_BUFFER_SIZE);
  }
  return end;
}

/*
Parses all records found in the USN file represented by input. Uses the records map to recreate file paths
Outputs the results to several streams.
*/
void parseUSN(const std::vector<File>& records, SQLiteHelper& sqliteHelper, std::istream& input, std::ostream& output) {
  if (sizeof(long long) < 8) {
    std::cerr << "64-bit arithmetic not available. This won't work. Exiting." << std::endl;
    exit(1);
  }

  char buffer[USN_BUFFER_SIZE];

  int records_processed = -1;

  std::streampos end = advanceStream(input, buffer);
  std::streampos start = input.tellg();
  ProgressBar status(end - start);

  UsnRecord prevRec;
  output << getUSNColumnHeaders();

  unsigned int offset = 0;

  //scan through the $USNJrnl one record at a time. Each record is variable length.
  bool done = false;
  while (!input.eof() && !done) {
    status.setDone((unsigned long long) input.tellg() - USN_BUFFER_SIZE + offset - start);

    if (offset + 4 > USN_BUFFER_SIZE || hex_to_long(buffer + offset, 4) + offset > USN_BUFFER_SIZE) {
      // We've reached the end of the buffer. Move the record to the front,
      // then read to fill out the rest of the buffer
      memmove(buffer, buffer + offset, USN_BUFFER_SIZE - offset);
      input.read(buffer + USN_BUFFER_SIZE - offset, offset);
      offset = 0;
    }

    unsigned long long record_length = hex_to_long(buffer + offset, 4);

    if (record_length == 0) {
      offset += 8;
      continue;
    }
    if (record_length > USN_BUFFER_SIZE) {
      status.clear();
      std::cerr << std::hex << record_length << " is an awfully large record_length!" << std::endl;
      std::cerr << "Cannot continue. Check that we're not missing much at "
        << std::hex << input.tellg()<< std::endl;
      break;
    }
    records_processed++;

    UsnRecord rec(buffer + offset);
    output << rec.toString(records);
    rec.insert(sqliteHelper.UsnInsert, records);

    if (prevRec.Record != rec.Record || prevRec.Reason & UsnReasons::CLOSE) {
      prevRec.checkTypeAndInsert(sqliteHelper.EventInsert);
      prevRec.clearFields();
    }
    if (prevRec.Usn == 0)
      prevRec = rec;
    else
      prevRec.update(rec);

    offset += record_length;
  }
  status.finish();
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

UsnRecord::UsnRecord(const char* buffer, int len, bool isEmbedded) : IsEmbedded(isEmbedded) {
  if (len < 0 || (unsigned) len >= 0x3C) {
    PreviousName                     = "";
    PreviousParent                   = 0;
    unsigned long long record_length = hex_to_long(buffer, 4);
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
  Record          = 0;
  ParentReference = 0;
  Parent          = 0;
  PreviousName    = "";
  PreviousParent  = 0;
  Reason          = 0;
  Timestamp       = "";
  Usn             = 0;
}

UsnRecord::UsnRecord() {
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
     << getFullPath(records, Parent) << std::endl;
  return ss.str();
}

void UsnRecord::insertEvent(unsigned int type, sqlite3_stmt* stmt) {
  sqlite3_bind_int64(stmt, 1, Record);
  sqlite3_bind_int64(stmt, 2, Parent);
  sqlite3_bind_int64(stmt, 3, PreviousParent);
  sqlite3_bind_int64(stmt, 4, Usn);
  sqlite3_bind_text(stmt , 5, Timestamp.c_str()   , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt , 6, Name.c_str()        , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt , 7, PreviousName.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 8, type);
  sqlite3_bind_int64(stmt, 9, IsEmbedded ? EventSources::USN_EMBEDDED : EventSources::USN);

  sqlite3_step(stmt);
  sqlite3_reset(stmt);
}

void UsnRecord::insert(sqlite3_stmt* stmt, const std::vector<File>& records) {
  sqlite3_bind_int64(stmt, 1, Record);
  sqlite3_bind_int64(stmt, 2, Parent);
  sqlite3_bind_int64(stmt, 3, Usn);
  sqlite3_bind_text(stmt, 4, Timestamp.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, getReasonString().c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, Name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, getFullPath(records, Record).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8, getFullPath(records, Parent).c_str(), -1, SQLITE_TRANSIENT);

  sqlite3_step(stmt);
  sqlite3_reset(stmt);
}

void UsnRecord::checkTypeAndInsert(sqlite3_stmt* stmt) {
  if (Reason & UsnReasons::FILE_CREATE)
    insertEvent(EventTypes::CREATE, stmt);
  if (Reason & UsnReasons::FILE_DELETE)
    insertEvent(EventTypes::DELETE, stmt);
  if (PreviousName != Name && (Reason & (UsnReasons::RENAME_NEW_NAME | UsnReasons::RENAME_OLD_NAME)))
    insertEvent(EventTypes::RENAME, stmt);
  if (Parent != PreviousParent && (Reason & (UsnReasons::RENAME_NEW_NAME | UsnReasons::RENAME_OLD_NAME)))
    insertEvent(EventTypes::MOVE, stmt);
}
