#include "helper_functions.h"
#include "mft.h"
#include "file.h"
#include "progress.h"

std::string getMFTColumnHeaders() {
  return "Logical Sequence Number\tMFT_Record_No\tupdate_sequence_no\tfile_name\tisDir\tisAllocated" \
    "\tsia_created\tsia_modified\tsia_mft_modified\tsia_accessed\tParMFTRecordNo\tfna_created\tfna_modified" \
    "\tfna_mft_modified\tfna_accessed\tlogical_size\tphysical_size";
}

MFTRecord::MFTRecord(char* buffer) {
  // MFT entries must begin with FILE
  if(hex_to_long(buffer, 4) != 0x454C4946) {
    return;
  }

  // Parse basic information from file record segment header
  Lsn                                    = hex_to_long(buffer + 8,    8);
  Record                                 = hex_to_long(buffer + 0x2c, 4);
  unsigned long long allocation_flag     = hex_to_long(buffer + 0x16, 2);
  unsigned long long mft_space_allocated = hex_to_long(buffer + 0x18, 4);
  unsigned long long offset              = hex_to_long(buffer + 0x14, 2);

  isAllocated                            = allocation_flag & 0x1;
  isDir                                  = allocation_flag & 0x2;

  // Parse the attributes
  while(offset < 1024 && offset < mft_space_allocated) {

    unsigned long long type_id          = hex_to_long(buffer + offset,        4);
    unsigned long long attribute_length = hex_to_long(buffer + offset + 4,    4);
    unsigned long long content_offset   = hex_to_long(buffer + offset + 0x14, 2);
    char* attribute_data = buffer + offset + content_offset;

    switch(type_id) {
      case 0x10:
        Sia = SIAttribute(attribute_data);
        break;
      case 0x30:
        // Use the fna which is "largest" (based on ASCII-ness and size)
        FNAttribute fna2(attribute_data);
        if (Fna < fna2)
          Fna = fna2;
        break;
    }

    //check for valid attribute length value
    if(attribute_length > 0 && attribute_length < 1024)
      offset += attribute_length;
    else {
      break;
    }
  }
}

SIAttribute::SIAttribute(char* buffer) {
  Created     = hex_to_long(buffer + 0x0,  8);
  Modified    = hex_to_long(buffer + 0x8,  8);
  MFTModified = hex_to_long(buffer + 0x10, 8);
  Accessed    = hex_to_long(buffer + 0x18, 8);
  Usn         = hex_to_long(buffer + 0x40, 8);
  Valid       = true;
}

FNAttribute::FNAttribute(char* buffer) {
  Parent                = hex_to_long(buffer,        6);
  Created               = hex_to_long(buffer + 0x08, 8);
  Modified              = hex_to_long(buffer + 0x10, 8);
  MFTModified           = hex_to_long(buffer + 0x18, 8);
  Accessed              = hex_to_long(buffer + 0x20, 8);
  LogicalSize           = hex_to_long(buffer + 0x28, 8);
  PhysicalSize          = hex_to_long(buffer + 0x30, 8);
  unsigned int name_len = hex_to_long(buffer + 0x40, 1);
  Name                  = mbcatos    (buffer + 0x42, name_len);
  Valid                 = true;
}

unsigned int FNAttribute::countAscii() {
  unsigned int count = 0;
  for (auto c: Name) {
    if (c == (c & 0x7F))
      count++;
  }
  return count;
}

File MFTRecord::asFile() {
  return File(Fna.Name, Record, Fna.Parent, filetime_to_iso_8601(Sia.MFTModified));
}

bool operator<(FNAttribute a, FNAttribute b) {
  // Compares attributes based on the names
  // We prefer names which are ASCII, and after that names which are long.
  // TODO this uses the byte count of the UTF8 string, which could add preference to names with more non ascii characters...
  bool x = a.countAscii() == a.Name.length();
  bool y = b.countAscii() == b.Name.length();
  if (a.Valid != b.Valid)
    return b.Valid;
  if (x != y)
    return y;
  return a.Name.length() < b.Name.length();
}

std::string MFTRecord::toString(std::vector<File>& records) {
  std::stringstream ss;
  ss << Lsn                                    << "\t"
     << Record                                 << "\t"
     << Sia.Usn                                << "\t"
     << getFullPath(records, Record)           << "\t"
     << isDir                                  << "\t"
     << isAllocated                            << "\t"
     << filetime_to_iso_8601(Sia.Created)      << "\t"
     << filetime_to_iso_8601(Sia.Modified)     << "\t"
     << filetime_to_iso_8601(Sia.MFTModified)  << "\t"
     << filetime_to_iso_8601(Sia.Accessed)     << "\t"
     << Fna.Parent                             << "\t"
     << filetime_to_iso_8601(Fna.Created)      << "\t"
     << filetime_to_iso_8601(Fna.Modified)     << "\t"
     << filetime_to_iso_8601(Fna.MFTModified)  << "\t"
     << filetime_to_iso_8601(Fna.Accessed)     << "\t"
     << Fna.LogicalSize                        << "\t"
     << Fna.PhysicalSize
     << std::endl;
  return ss.str();
}

void parseMFT(std::vector<File>& records, sqlite3* db, std::istream& input, std::ostream& output, const bool initRecords) {
  if(sizeof(long long) < 8) {
    std::cerr << "64-bit arithmetic not available. This won't work." << std::endl;
    exit(1);
  }
  char buffer[1024];

  bool done = false;
  int records_processed = 0;

  sqlite3_stmt* stmt;
  if (!initRecords) {
    //prepare the SQL statement
    int rc = 0;
    std::string sql = "insert into mft values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    rc = sqlite3_prepare_v2(db, sql.c_str(), sql.length() + 1, &stmt, NULL);
    if (rc) {
      std::cerr << "SQL Error " << rc << " at " << __FILE__ << ":" << __LINE__ << std::endl;
      std::cerr << sqlite3_errmsg(db) << std::endl;
      sqlite3_close(db);
      exit(2);
    }
  }
  input.seekg(0, std::ios::end);
  unsigned long long end = input.tellg();
  input.seekg(0, std::ios::beg);
  ProgressBar status(end);

  //scan through the $MFT one record at a time. Each record is 1024 bytes.
  while(!input.eof() && !done) {
    status.setDone((unsigned long long) input.tellg());
    records_processed++;
    input.read(buffer, 1024);
    MFTRecord record(buffer);
    if (initRecords) {
      for(int i = record.Record - records.size() + 1; i >= 0; i--)
        records.push_back(File());
      records[record.Record] = record.asFile();
    }
    else {
      record.insert(stmt, records);
      output << record.toString(records);
    }
  }

  status.finish();
  if (!initRecords)
    sqlite3_finalize(stmt);
}

void MFTRecord::insert(sqlite3_stmt* stmt, std::vector<File>& records) {
  int i = 0;
  sqlite3_bind_int64(stmt, ++i, Lsn);
  sqlite3_bind_int64(stmt, ++i, Record);
  sqlite3_bind_int64(stmt, ++i, Fna.Parent);
  sqlite3_bind_int64(stmt, ++i, Sia.Usn);
  sqlite3_bind_text(stmt , ++i, getFullPath(records, Record).c_str()          , -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt  , ++i, isDir);
  sqlite3_bind_int(stmt  , ++i, isAllocated);
  sqlite3_bind_text(stmt , ++i, filetime_to_iso_8601(Sia.Created).c_str()     , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt , ++i, filetime_to_iso_8601(Sia.Modified).c_str()    , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt , ++i, filetime_to_iso_8601(Sia.MFTModified).c_str() , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt , ++i, filetime_to_iso_8601(Sia.Accessed).c_str()    , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt , ++i, filetime_to_iso_8601(Fna.Created).c_str()     , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt , ++i, filetime_to_iso_8601(Fna.Modified).c_str()    , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt , ++i, filetime_to_iso_8601(Fna.MFTModified).c_str() , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt , ++i, filetime_to_iso_8601(Fna.Accessed).c_str()    , -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, ++i, Fna.LogicalSize);
  sqlite3_bind_int64(stmt, ++i, Fna.PhysicalSize);

  sqlite3_step(stmt);
  sqlite3_reset(stmt);
}
