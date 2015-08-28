#include "helper_functions.h"
#include "mft.h"
#include "file.h"
#include "progress.h"

std::string getMFTColumnHeaders() {
  return "Logical Sequence Number\tMFT_Record_No\tupdate_sequence_no\tfile_name\tisDir\tisAllocated" \
    "\tsia_created\tsia_modified\tsia_mft_modified\tsia_accessed\tParMFTRecordNo\tfna_created\tfna_modified" \
    "\tfna_mft_modified\tfna_accessed\tlogical_size\tphysical_size";
}

void initMFTMap(std::istream& input, std::vector<File>& records) {
  char buffer[1024];
  input.seekg(0, std::ios::end);
  std::streampos end = input.tellg();
  input.seekg(0, std::ios::beg);
  ProgressBar status(end);
  while(!input.eof()) {
    status.setDone((unsigned long long) input.tellg());
    input.read(buffer, 1024);
    //check if record begins with FILE, otherwise, invalid record
    if(hex_to_long(buffer, 4) != 0x454C4946) {
      continue;
    }
    //parse basic information from file record segment header
    unsigned long long offset = hex_to_long(buffer+0x14, 2);
    unsigned long long mft_record_no = hex_to_long(buffer + 0x2c, 4);
//    cout << mft_record_no << std::endl;
    unsigned long long mft_space_allocated = hex_to_long(buffer + 0x18, 4);
    unsigned long long name_len = 0, par_rec_no = 0, mft_modified = 0;
    //unsigned long long name_type = 0;
    std::string file_name;
    while(offset < 1024 && offset < mft_space_allocated) {

      unsigned long long type_id = hex_to_long(buffer + offset, 4);
      unsigned long long attribute_length = hex_to_long(buffer+offset+4, 4);
      unsigned long long content_offset = hex_to_long(buffer+offset + 0x14, 2);

      switch(type_id) {
        //SIA
        case 0x10:
          mft_modified = hex_to_long(buffer+offset+content_offset+0x10, 8);
          break;
        //FNA
        case 0x30:
          if(hex_to_long(buffer+offset+content_offset+0x40, 1) > name_len) {
            par_rec_no = hex_to_long(buffer+offset+content_offset, 6);
          //  mft_modified = hex_to_long(buffer + offset + content_offset + 0x18, 8);
            name_len = hex_to_long(buffer+offset+content_offset+0x40, 1);
            //name_type = hex_to_long(buffer+offset+content_offset+0x41, 1);
            file_name = mbcatos(buffer+offset+content_offset+0x42, name_len);
          }
          break;
      }

      //check for valid attribute length value
      if(attribute_length > 0 && attribute_length < 1024)
        offset += attribute_length;
      else {
        break;
      }
    }

    for(int i = mft_record_no - records.size() + 1; i >= 0; i--)
      records.push_back(File());
    if(!records[mft_record_no].valid) {
      records[mft_record_no] = File(file_name, mft_record_no, par_rec_no, filetime_to_iso_8601(mft_modified));
    }

  }
  status.finish();

}

MFT_Record::MFT_Record(char* buffer, std::vector<File>& records) {


  //check if record begins with FILE, otherwise, invalid record
  if(hex_to_long(buffer, 4) != 0x454C4946) {
//    std::cerr << "Invalid record" << std::endl;
    return;
  }
  //parse basic information from file record segment header
  lsn = hex_to_long(buffer+8, 8);
  mft_record_no = hex_to_long(buffer + 0x2c, 4);
  unsigned long long allocation_flag = hex_to_long(buffer + 0x16, 2);
  unsigned long long mft_space_allocated = hex_to_long(buffer + 0x18, 4);
  isDir = false;
  isAllocated = false;
  if(allocation_flag & 0x1) {
    isAllocated = true;
  }
  if(allocation_flag & 0x2) {
    isDir = true;
  }


  //buffer + 0x14-0x15  contains the 1st attribute offset.
  unsigned long long offset = hex_to_long(buffer+0x14, 2);
  unsigned long long name_len = 0;
  //unsigned long long name_type = 0;
  //parse through each attribute
  while(offset < 1024 && offset < mft_space_allocated) {

    unsigned long long type_id = hex_to_long(buffer + offset, 4);
    //unsigned long long form_code = hex_to_long(buffer+offset + 8, 1);
    unsigned long long attribute_length = hex_to_long(buffer+offset+4, 4);
    unsigned long long content_offset = hex_to_long(buffer+offset + 0x14, 2);

    switch(type_id) {
      case 0x10:
        sia_created = hex_to_long(buffer+offset+content_offset+0x0, 8);
        sia_modified = hex_to_long(buffer+offset+content_offset+0x8, 8);
        sia_mft_modified = hex_to_long(buffer+offset+content_offset+0x10, 8);
        sia_accessed = hex_to_long(buffer+offset+content_offset+0x18, 8);
        sia_flags = hex_to_long(buffer+offset+content_offset+0x20, 4);
        update_seq_no = hex_to_long(buffer+offset+content_offset+0x40, 8);
        break;
      case 0x30:
        //0x30 File Name attribute is repeatable. Check to see if this current attribute has a longer file name than the previous.
        if(hex_to_long(buffer+offset+content_offset+0x40, 1) > name_len) {
          parent_dir = hex_to_long(buffer+offset+content_offset, 6);
          fna_created = hex_to_long(buffer+offset+content_offset+0x8, 8);
          fna_modified = hex_to_long(buffer+offset+content_offset+0x10, 8);
          fna_mft_modified = hex_to_long(buffer+offset+content_offset+0x18, 8);
          fna_accessed = hex_to_long(buffer+offset+content_offset+0x20, 8);
          logical_size = hex_to_long(buffer+offset+content_offset+0x28, 8);
          physical_size = hex_to_long(buffer+offset+content_offset+0x30, 8);
          name_len = hex_to_long(buffer+offset+content_offset+0x40, 1);
          //name_type = hex_to_long(buffer+offset+content_offset+0x41, 1);
          file_name = mbcatos(buffer+offset+content_offset+0x42, name_len);
  //            file_name = byte_to_str(buffer+offset+content_offset+0x42, name_len<<1);
        }
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

std::string MFT_Record::toString(std::vector<File>& records) {
  std::stringstream ss;
  ss << lsn << "\t" << mft_record_no << "\t" << update_seq_no << "\t";
  ss << getFullPath(records, mft_record_no); //file_name;
  ss << "\t" << isDir << "\t" << isAllocated << "\t"
    << filetime_to_iso_8601(sia_created) << "\t" << filetime_to_iso_8601(sia_modified) << "\t"
    << filetime_to_iso_8601(sia_mft_modified) << "\t" << filetime_to_iso_8601(sia_accessed) << "\t"
    << parent_dir << "\t" << filetime_to_iso_8601(fna_created) << "\t"
    << filetime_to_iso_8601(fna_modified) << "\t"
    << filetime_to_iso_8601(fna_mft_modified) << "\t" << filetime_to_iso_8601(fna_accessed) << "\t"
    << logical_size << "\t" << physical_size;
  ss << std::endl;
  return ss.str();
}

void parseMFT(std::vector<File>& records, sqlite3* db, std::istream& input, std::ostream& output) {
  if(sizeof(long long) < 8) {
    std::cerr << "64-bit arithmetic not available. This won't work." << std::endl;
    exit(1);
  }
  char buffer[1024];

  bool done = false;
  int records_processed = -1;
  //print the column headers
//  output << getMFTColumnHeaders() << std::endl;

  //prepare the SQL statement
  int rc = 0;
//  rc = sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);
//  if (rc) {
//    std::cerr << "SQL Error " << rc << " at " << __FILE__ << ":" << __LINE__ << std::endl;
//    std::cerr << sqlite3_errmsg(db) << std::endl;
//    sqlite3_close(db);
//    exit(2);
//  }
  std::string sql = "insert into mft values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt;
  rc = sqlite3_prepare_v2(db, sql.c_str(), sql.length() + 1, &stmt, NULL);
  if (rc) {
    std::cerr << "SQL Error " << rc << " at " << __FILE__ << ":" << __LINE__ << std::endl;
    std::cerr << sqlite3_errmsg(db) << std::endl;
    sqlite3_close(db);
    exit(2);
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
    MFT_Record record(buffer, records);
    record.insert(db, stmt, records);
    output << record.toString(records);
  }

  status.finish();
  sqlite3_finalize(stmt);
//  rc = sqlite3_exec(db, "END TRANSACTION", 0, 0, 0);
//  if (rc) {
//    std::cerr << "SQL Error " << rc << " at " << __FILE__ << ":" << __LINE__ << std::endl;
//    sqlite3_close(db);
//    exit(2);
//  }


}

void MFT_Record::insert(sqlite3* db, sqlite3_stmt* stmt, std::vector<File>& records) {

  sqlite3_bind_int64(stmt, 1, lsn);
  sqlite3_bind_int64(stmt, 2, mft_record_no);
  sqlite3_bind_int64(stmt, 3, parent_dir);
  sqlite3_bind_int64(stmt, 4, update_seq_no);
  sqlite3_bind_text(stmt, 5, getFullPath(records, mft_record_no).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 6, isDir);
  sqlite3_bind_int(stmt, 7, isAllocated);
  sqlite3_bind_text(stmt, 8, filetime_to_iso_8601(sia_created).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 9, filetime_to_iso_8601(sia_modified).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 10, filetime_to_iso_8601(sia_mft_modified).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 11, filetime_to_iso_8601(sia_accessed).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 12, filetime_to_iso_8601(fna_created).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 13, filetime_to_iso_8601(fna_modified).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 14, filetime_to_iso_8601(fna_mft_modified).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 15, filetime_to_iso_8601(fna_accessed).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 16, logical_size);
  sqlite3_bind_int64(stmt, 17, physical_size);

  sqlite3_step(stmt);

//  sqlite3_clear_bindings(stmt);
  sqlite3_reset(stmt);

}
