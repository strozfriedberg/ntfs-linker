#include <iterator>

#include "log.h"
#include "helper_functions.h"
#include "mft.h"
#include "progress.h"

/*
Decodes the LogFile Op code
*/
std::string decodeLogFileOpCode(int op) {
  switch(op) {
    case 0x00: return "Noop";
    case 0x01: return "CompensationLogRecord";
    case 0x02: return "InitializeFileRecordSegment";
    case 0x03: return "DeallocateFileRecordSegment";
    case 0x04: return "WriteEndOfFileRecordSegment";
    case 0x05: return "CreateAttribute";
    case 0x06: return "DeleteAttribute";
    case 0x07: return "UpdateResidentValue";
    case 0x08: return "UpdateNonresidentValue";
    case 0x09: return "UpdateMappingPairs";
    case 0x0A: return "DeleteDirtyClusters";
    case 0x0B: return "SetNewAttributeSizes";
    case 0x0C: return "AddIndexEntryRoot";
    case 0x0D: return "DeleteIndexEntryRoot";
    case 0x0E: return "AddIndexEntryAllocation";
    case 0x0F: return "DeleteIndexEntryAllocation";
    case 0x12: return "SetIndexEntryVCNAllocation";
    case 0x13: return "UpdateFileNameRoot";
    case 0x14: return "UpdateFileNameAllocation";
    case 0x15: return "SetBitsInNonresidentBitMap";
    case 0x16: return "ClearBitsInNonresidentBitMap";
    case 0x19: return "PrepareTransaction";
    case 0x1A: return "CommitTransaction";
    case 0x1B: return "ForgetTransaction";
    case 0x1C: return "OpenNonresidentAttribute";
    case 0x1F: return "DirtyPageTableDump";
    case 0x20: return "TransactionTableDump";
    case 0x21: return "UpdateRecordDataRoot";
    default:
      return "Invalid";
  }
}

bool isUTF16leNonAscii(char* arr, unsigned long long len) {
  for (unsigned int i = 0; i < len; i++) {
    if (arr[2*i + 1] != 0)
      return true;
  }
  return false;
}

/*
Parses the $LogFile
outputs to the various streams
*/
void parseLog(std::vector<File>& records, sqlite3* db, std::istream& input, std::ostream& output) {
  unsigned int buffer_size = 4096;
  char* buffer = new char[buffer_size];
  bool split_record = false;
  bool done = false;
  bool parseError = true;
  int records_processed = 3;
  int adjust = 0;
  int rc = 0;

  /*Skip past the junk at the beginning of the file
  The first two pages (0x0000 - 0x2000) are restart pages
  The next two pages (0x2000 - 0x4000) are buffer record pages
  in my testing I've seen very little of value here, and it doesn't follow the same format as the rest of the $LogFile
  */
  input.seekg(0, std::ios::end);
  unsigned long long end = input.tellg();
  ProgressBar status(end);
  unsigned long long start = 0x4000;
  input.seekg(start);
  input.read(buffer, 4096);

  LogData transactions;
  transactions.clearFields();
  LogData::initTransactionVectors();
  std::string log_sql = "insert into log values(?, ?, ?, ?, ?, ?, ?, ?, ?);";
  std::string events_sql  = "insert into events values (?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt *log_stmt, *events_stmt;
  rc &= sqlite3_prepare_v2(db, log_sql.c_str(), log_sql.length() + 1, &log_stmt, NULL);
  rc &= sqlite3_prepare_v2(db, events_sql.c_str(), events_sql.length() + 1, &events_stmt, NULL);

  //scan through the $LogFile one  page at a time. Each record is 4096 bytes.
  while(!input.eof() && !done) {

    status.setDone((unsigned long long) input.tellg() - start);
    //check log record header
    if(hex_to_long(buffer, 4) != 0x44524352) {
      input.read(buffer, 4096);
      buffer_size = 4096;
      adjust = 0;
      continue;
    }
    records_processed++;
    unsigned int update_seq_offset, update_seq_count, offset, next_record_offset;
    unsigned int length;
    update_seq_offset = hex_to_long(buffer + 0x4, 2);
    update_seq_count = hex_to_long(buffer + 0x6, 2);
    // Equivalent to 8*ceil(update_seq_count/4)
    offset = update_seq_offset + ((((update_seq_count << 1) + 7) >> 3) << 3);
    next_record_offset = hex_to_long(buffer + 0x18, 2);
    if(parseError) { //initialize the offset on the "first" record processed
      offset = next_record_offset;
      parseError = false;
      transactions.clearFields();
    }

    //parse log record
    while(offset + 0x30 <= buffer_size) {
      LogRecord rec;
      int rtnVal = rec.init(buffer + offset);
      if(rtnVal == -1) {
        split_record = true;
        length = rec.client_data_length + 0x30;
        break;
      } else if(rtnVal < 0) {
        length = rec.client_data_length + 0x30;
        std::cerr << " at " << records_processed << " " << offset - adjust << std::endl;
        std::cerr << "Some records will be skipped to recover." << std::endl;
        parseError = true;
        break;
      } else {
        length = rtnVal;
      }

      output << rec;
      rec.insert(log_stmt);

      transactions.processLogRecord(rec, records);
      if(transactions.isTransactionOver()) {
        if(transactions.isCreateEvent()) {
          transactions.insertEvent(EventTypes::CREATE, events_stmt);
        }
        if(transactions.isDeleteEvent()) {
          transactions.insertEvent(EventTypes::DELETE, events_stmt);
        }
        if(transactions.isRenameEvent()) {
          transactions.insertEvent(EventTypes::RENAME, events_stmt);
        }
        if(transactions.isMoveEvent()) {
          transactions.insertEvent(EventTypes::MOVE, events_stmt);
        }
        transactions.clearFields();
      }
      offset += length;

    }

    /*
    If a record is left dangling across a page boundary, we can still parse the record
    The strategy is to rearrange the data like so:
    BEFORE: dangling transaction | RCRD header | rest of transaction | rest of page
    AFTER : RCRD header | dangling transaction | rest of transaction | rest of page
    We perform  a little switcheroo then return to the top of the loop
    */
    if(split_record) {
      unsigned int new_size = ((length - buffer_size + offset + 4031) / 4032) * 4096 + buffer_size - offset;
      char* temp = new char[new_size];
      adjust = buffer_size - offset;
      input.read(temp + buffer_size - offset, 4096);
      if(input.eof()) {
        done = true;
        delete[] temp;
        break;
      }

      update_seq_offset = hex_to_long(temp + buffer_size - offset + 0x4, 2);
      update_seq_count = hex_to_long(temp + buffer_size - offset + 0x6, 2);
      unsigned int header_length = update_seq_offset + ((((update_seq_count << 1) + 7) >> 3) << 3);
      memmove(temp, temp + buffer_size - offset, header_length);
      memcpy(temp + header_length, buffer + offset, buffer_size - offset);
      delete[] buffer;
      buffer = temp;
      buffer[header_length + 0x28] = 0;
      buffer[header_length + 0x29] = 0;
      unsigned int write_offset = buffer_size - offset + 4096;

      /*
      In some cases, it's not that easy. Sometimes a single record spans multiple pages.
      We perform a more involved switcheroo.
      Notice that we are implicitly assuming
      that the page header is always 0x40 bytes, whereas before we perform some calculation involving
      the update sequence offset and update sequence count

      The first page header is left intact, but subsequent page headers are discarded
      BEFORE: RCRD header | record pt1 | RCRD header | record pt2 | RCRD header | record pt3 | ...
      AFTER : RCRD header | record pt1 | record pt2 | record pt3 | ...
      */
      for(unsigned int i = 1; write_offset < new_size; i++) {
        temp = new char[4096];
        input.read(temp, 4096);

        update_seq_offset = hex_to_long(temp + 0x4, 2);
        update_seq_count = hex_to_long(temp + 0x6, 2);
        header_length = update_seq_offset + ((((update_seq_count << 1) + 7) >> 3) << 3);
        memcpy(buffer + write_offset, temp + header_length, 4096 - header_length);
        write_offset += 4096 - header_length;
        records_processed++;
        new_size -= header_length;
        delete[] temp;
      }
      buffer_size = new_size;
      split_record = false;

    }
    else {
      /*
      If the preceding record wasn't flagged for being split across the page,
      we just read the next page. Easy.
      */
      input.read(buffer, 4096);
      buffer_size = 4096;

      if(input.eof()) {
        done = true;
        break;
      }
      adjust = 0;
    }

  }
//  rc = sqlite3_exec(db, "END TRANSACTION", 0, 0, 0);
  status.finish();
  sqlite3_finalize(log_stmt);
  sqlite3_finalize(events_stmt);
  delete [] buffer;
}

std::ostream& operator<<(std::ostream& out, const LogRecord& rec) {
  out << rec.cur_lsn << "\t"
      << rec.prev_lsn << "\t"
      << rec.undo_lsn << "\t"
      << rec.client_id << "\t"
      << rec.record_type << "\t"
      << decodeLogFileOpCode(rec.redo_op) << "\t"
      << decodeLogFileOpCode(rec.undo_op) << "\t"
      << rec.target_attr << "\t"
      << rec.mft_cluster_index << "\t"
      << rec.target_vcn << "\t"
      << rec.target_lcn
      << std::endl;
  return out;
}

bool LogData::isCreateEvent() {
  return transactionRunMatch(redo_ops, undo_ops, LogData::create_redo, LogData::create_undo);
}

bool LogData::isDeleteEvent() {
  return transactionRunMatch(redo_ops, undo_ops, LogData::delete_redo, LogData::delete_undo);
}

bool LogData::isRenameEvent() {
  return transactionRunMatch(redo_ops, undo_ops, LogData::rename_redo, LogData::rename_undo) && name != prev_name;
}

bool LogData::isMoveEvent() {
  return transactionRunMatch(redo_ops, undo_ops, LogData::rename_redo, LogData::rename_undo) && par_mft_record != prev_par_mft_record;
}

bool LogData::isTransactionOver() {
  return redo_ops.back() == 0x1b && undo_ops.back() == 0x1;
}

void LogData::initTransactionVectors() {
  /*
  Used to determine whether a particular transaction run has occurred
  The source for these runs is "NTFS Log Tracker" : forensicinsight.org
  My reading of that presentation is that the transactions should be consecutive,
  however, I found very few (if any) transaction runs that match the below exactly.
  Instead I check that the below sequences are merely a subsequence of the given run.
  */
  int a_create_redo[] = {0x15, 0x0, 0xe, 0x2, 0x1b};
  int a_create_undo[] = {0x16, 0x3, 0xf, 0x0, 0x1};
  int a_delete_redo[] = {0xf, 0x3, 0x16, 0x1b};
  int a_delete_undo[] = {0xe, 0x2, 0x15, 0x1};
  int a_rename_redo[] = {0xf, 0x6, 0x5, 0xe, 0x1b};
  int a_rename_undo[] = {0xe, 0x5, 0x6, 0xf, 0x1};
  int a_write_redo[] = {0x6, 0x5, 0x15, 0xb, 0x9, 0xb, 0x1b};
  int a_write_undo[] = {0x5, 0x6, 0x16, 0xb, 0x9, 0xb, 0x1};
  create_redo = std::vector<int>(a_create_redo, a_create_redo + sizeof(a_create_redo)/sizeof(int));
  create_undo = std::vector<int>(a_create_undo, a_create_undo + sizeof(a_create_undo)/sizeof(int));
  delete_redo = std::vector<int>(a_delete_redo, a_delete_redo + sizeof(a_delete_redo)/sizeof(int));
  delete_undo = std::vector<int>(a_delete_undo, a_delete_undo + sizeof(a_delete_undo)/sizeof(int));
  rename_redo = std::vector<int>(a_rename_redo, a_rename_redo + sizeof(a_rename_redo)/sizeof(int));
  rename_undo = std::vector<int>(a_rename_undo, a_rename_undo + sizeof(a_rename_undo)/sizeof(int));
  write_redo = std::vector<int>(a_write_redo, a_write_redo + sizeof(a_write_redo)/sizeof(int));
  write_undo = std::vector<int>(a_write_undo, a_write_undo + sizeof(a_write_undo)/sizeof(int));
}

int LogRecord::init(char* buffer) {
  data = buffer;
  cur_lsn = hex_to_long(buffer, 8);
  prev_lsn = hex_to_long(buffer + 0x8, 8);
  undo_lsn = hex_to_long(buffer + 0x10, 8);

  client_data_length = hex_to_long(buffer + 0x18, 4);

  client_id = hex_to_long(buffer + 0x1C, 4);
  record_type = hex_to_long(buffer + 0x20, 4);

  /*
  Not particularly a concern. Sometimes there is extra slack space at the end of a page.
  In which case, we read that the record type is 0 and print an error message
  (since 0 is not a valid record type).
  If these error messages are spamming error output, however, then something probably went
  horribly wrong. Most likely cause is that the parser offset became misplaced and tried to
  parse the wrong bits of the records.
  */
  if(record_type == 0) {
    std::cerr << std::setw(60) << std::left << std::setfill(' ') << "\r";
    std::cerr << "Invalid record type: " << record_type;
    if(record_type == 0) {
      return -2;
    }
    exit(0);
  }

  /*
  A flag on a record means that at least part of the record is on the next page.
  We do some fancy switcheroo stuff at the bottom of the loop to compensate.
  */
  flags = hex_to_long(buffer + 0x28, 2);
  if(flags == 1) {
    return -1;
  }

  /*
  If we're reading invalid op codes then chances are something went horribly wrong.
  We end the process before it can implode.
  */
  redo_op = hex_to_long(buffer + 0x30, 2);
  undo_op = hex_to_long(buffer + 0x32, 2);
  if(redo_op > 0x21 || undo_op > 0x21) {
    std::cerr << std::setw(60) << std::left << std::setfill(' ') << "\r";
    std::cerr << "\rInvalid op code: " << std::hex << redo_op << " " << undo_op;
    return -2;
  }
  redo_offset = hex_to_long(buffer + 0x34, 2);
  redo_length = hex_to_long(buffer + 0x36, 2);
  undo_offset = hex_to_long(buffer + 0x38, 2);
  undo_length = hex_to_long(buffer + 0x3a, 2);
  target_attr = hex_to_long(buffer + 0x3c, 2);
  lcns_to_follow = hex_to_long(buffer + 0x3e, 2);

  record_offset = hex_to_long(buffer + 0x40, 2);
  attribute_offset = hex_to_long(buffer + 0x42, 2);
  mft_cluster_index = hex_to_long(buffer + 0x44, 2);
  target_vcn = hex_to_long(buffer + 0x48, 4);

  target_lcn = hex_to_long(buffer + 0x50, 4);

  /*
  The length given by client_data_length is actually 0x30 less than the length of the record.
  */

  return 0x30 + client_data_length;

}

void LogData::processLogRecord(LogRecord& rec, std::vector<File>& records) {
  if(lsn == 0) {
    lsn = rec.cur_lsn;
  }
  redo_ops.push_back(rec.redo_op);
  undo_ops.push_back(rec.undo_op);
  //pull data from necessary opcodes to save for transaction runs
  if(rec.redo_op == 0x15 && rec.undo_op == 0x16) {
    if(rec.redo_length >= 4)
      mft_record_no = hex_to_long(rec.data + 0x30 + rec.redo_offset, 4);
  }
  else if(rec.redo_op == 0x2 && rec.undo_op == 0x0) {
    //parse MFT record from redo op for create time, file name, parent dir
    //need to check for possible second MFT attribute header
    char* start = rec.data + 0x30 + rec.redo_offset;

    //check if record begins with FILE, otherwise, invalid record
    if(hex_to_long(start, 4) == 0x454C4946) {

      unsigned long long mft_offset = hex_to_long(start+0x14, 2);

      //parse through each attribute
      while(mft_offset + 0x18 <= rec.redo_length) {

        unsigned long long type_id = hex_to_long(start + mft_offset, 4);
        //unsigned long long form_code = hex_to_long(start + mft_offset + 8, 1);
        unsigned long long attribute_length = hex_to_long(start + mft_offset+4, 4);
        unsigned long long content_offset = hex_to_long(start + mft_offset + 0x14, 2);
        unsigned int len;

        switch(type_id) {
          case 0x10:
            timestamp = filetime_to_iso_8601(hex_to_long(start + mft_offset + content_offset + 0x0, 8));
            break;
          case 0x30:
            par_mft_record = hex_to_long(start + mft_offset + content_offset, 6);
            len = hex_to_long(start + mft_offset+content_offset+0x40, 1);
            bool newNameDirty = isUTF16leNonAscii(start + mft_offset + content_offset + 0x42, len);
            if (((IsNameDirty || len > name_len) && !newNameDirty) || (IsNameDirty && len > name_len)) {
              name_len = len;
              name = mbcatos(start + mft_offset + content_offset+0x42, name_len);
              IsNameDirty = newNameDirty;
            }
            break;
        }

        //check for valid attribute length value
        if(attribute_length > 0 && attribute_length < 1024)
          mft_offset += attribute_length;
        else
          break;
      }
    }
  }
  else if(rec.redo_op == 0x6 && rec.undo_op == 0x5) {
    //get the name before
    //from file attribute with header, undo op
    unsigned long long type_id = hex_to_long(rec.data + 0x30 + rec.undo_offset, 4);
    //unsigned long long form_code = hex_to_long(rec.data + 0x30 + rec.undo_offset + 8 + 8, 1);
    unsigned long long content_offset = hex_to_long(rec.data + 0x30 + rec.undo_offset + 0x14, 2);
    switch(type_id) {
      case 0x30:
        prev_par_mft_record = hex_to_long(rec.data + 0x30 + rec.undo_offset + content_offset, 6);
          if(records.size() > prev_par_mft_record && records[prev_par_mft_record].valid)
            timestamp = records[prev_par_mft_record].timestamp;
          else
            timestamp = "";
          name_len = hex_to_long(rec.data + 0x30 + rec.undo_offset + content_offset+0x40, 1);
          prev_name = mbcatos(rec.data + 0x30 + rec.undo_offset + content_offset+0x42, name_len);
        break;
      }

  }
  else if(rec.redo_op == 0x5 && rec.undo_op == 0x6) {
    //get the name after
    //from file attribute with header, redo op
    //prev_name =

    unsigned long long type_id = hex_to_long(rec.data + 0x30 + rec.redo_offset, 4);
    //unsigned long long form_code = hex_to_long(rec.data + 0x30 + rec.redo_offset + 8 + 8, 1);
    unsigned long long content_offset = hex_to_long(rec.data + 0x30 + rec.redo_offset + 0x14, 2);
    switch(type_id) {
      case 0x30:
        par_mft_record = hex_to_long(rec.data + 0x30 + rec.redo_offset + content_offset, 6);

        if(records.size() > prev_par_mft_record && records[prev_par_mft_record].valid)
          timestamp = records[par_mft_record].timestamp;
        else
          timestamp = "";

        unsigned int len = hex_to_long(rec.data + 0x30 + rec.redo_offset + content_offset+0x40, 1);
        bool newNameDirty = isUTF16leNonAscii(rec.data + 0x30 + rec.redo_offset + content_offset+0x42, len);
        if (((IsNameDirty || len > name_len) && !newNameDirty) || (IsNameDirty && len > name_len)) {
          name_len = len;
          name = mbcatos(rec.data + 0x30 + rec.redo_offset + content_offset+0x42, name_len);
          IsNameDirty = newNameDirty;
        }
        break;
    }
  }
  else if((rec.redo_op == 0xf && rec.undo_op == 0xe) || (rec.redo_op == 0xd && rec.undo_op == 0xc)) {
    if(rec.undo_length > 0x42) {
      // Delete or rename
      // This is a delete. I think.
      par_mft_record = hex_to_long(rec.data + 0x30 + rec.undo_offset + 0x10, 6);
      if(records.size() > prev_par_mft_record && records[prev_par_mft_record].valid)
        timestamp = records[prev_par_mft_record].timestamp;
      else
        timestamp = "";
      unsigned int len = hex_to_long(rec.data + 0x30 + rec.undo_offset + 0x10 + 0x40, 1);
      bool newNameDirty = isUTF16leNonAscii(rec.data + 0x30 + rec.undo_offset + 0x10 + 0x42, len);
      if(((IsNameDirty || len > name_len) && !newNameDirty) || (IsNameDirty && len > name_len)) {
        name_len = len;
        name = mbcatos(rec.data + 0x30 + rec.undo_offset + 0x10 + 0x42, name_len);
        IsNameDirty = newNameDirty;
      }
    }

  }
  else if ((rec.redo_op == 0x0e && rec.undo_op == 0x0f) || (rec.redo_op == 0x0c && rec.undo_op == 0x0d)) {
    // Add index entry root/AddIndexEntryAllocation operation
    // See https://flatcap.org/linux-ntfs/ntfs/concepts/index_record.html
    // for additional info about Index Record structure ("The header part")
    if (rec.redo_length > 0x52) {
      mft_record_no = hex_to_long(rec.data + 0x30 + rec.redo_offset, 6);
      par_mft_record = hex_to_long(rec.data + 0x30 + rec.redo_offset + 0x10, 6);
      timestamp = filetime_to_iso_8601(hex_to_long(rec.data + 0x30 + rec.redo_offset + 0x18, 8));

      unsigned int len = hex_to_long(rec.data + 0x30 + rec.redo_offset + 0x50, 1);
      bool newNameDirty = isUTF16leNonAscii(rec.data + 0x30 + rec.redo_offset + 0x52, len);
      if (((IsNameDirty || len > name_len) && !newNameDirty) || (IsNameDirty && len > name_len)) {
        name_len = len;
        name = mbcatos(rec.data + 0x30 + rec.redo_offset + 0x52, len);
        IsNameDirty = newNameDirty;
      }
    }
  }
}

void LogData::clearFields() {
  redo_ops.clear();
  undo_ops.clear();
  mft_record_no = 0;
  par_mft_record = 0;
  prev_par_mft_record = 0;
  timestamp = "";
  name_len = 0;
  lsn = 0;
  name = "";
  prev_name = "";
  IsNameDirty = true;
}

std::vector<int> LogData::create_redo, LogData::create_undo;
std::vector<int> LogData::delete_redo, LogData::delete_undo;
std::vector<int> LogData::rename_redo, LogData::rename_undo;
std::vector<int> LogData::write_redo, LogData::write_undo;


/*
Returns whether the given transaction run (redo1, undo1) matches the
given transaction run pattern. Will attempt to find a matching entry in redo1, undo1 for each entry
in redo2, undo2 (in the same order)
if interchange is true then 0xc=0xe and 0xd=0xf
*/
bool transactionRunMatch(const std::vector<int>& const_redo1, const std::vector<int>& const_undo1, const std::vector<int>& redo2, const std::vector<int>& undo2, bool interchange) {
  unsigned int j = 0;
  std::vector<int> redo1(const_redo1);
  std::vector<int> undo1(const_undo1);
  for(unsigned int i = 0; i < redo2.size(); i++) {
    bool top = false;
    for(; j < redo1.size() && !top; j++) {

      if(interchange) {
        if(redo1[j] == 0xc) redo1[j] = 0xe;
        if(redo1[j] == 0xd) redo1[j] = 0xf;
        if(undo1[j] == 0xc) undo1[j] = 0xe;
        if(undo1[j] == 0xd) undo1[j] = 0xf;
      }
      if(redo2[i] == redo1[j] && undo2[i] == undo1[j])
        top = true;
    }
    if(!top)
      return false;
  }
  return true;
}

void LogData::insertEvent(unsigned int type, sqlite3_stmt* stmt) {
  sqlite3_bind_int64(stmt, 1, mft_record_no);
  sqlite3_bind_int64(stmt, 2, par_mft_record);
  sqlite3_bind_int64(stmt, 3, prev_par_mft_record);
  sqlite3_bind_int64(stmt, 4, lsn);
  sqlite3_bind_text(stmt, 5, timestamp.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, prev_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 8, type);
  sqlite3_bind_int64(stmt, 9, EventSources::LOG);
  sqlite3_step(stmt);
  sqlite3_reset(stmt);
}

void LogRecord::insert(sqlite3_stmt* stmt) {
  sqlite3_bind_int64(stmt, 1, cur_lsn);
  sqlite3_bind_int64(stmt, 2, prev_lsn);
  sqlite3_bind_int64(stmt, 3, undo_lsn);
  sqlite3_bind_int(stmt, 4, client_id);
  sqlite3_bind_int(stmt, 5, record_type);
  sqlite3_bind_text(stmt, 6, decodeLogFileOpCode(redo_op).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, decodeLogFileOpCode(undo_op).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 8, target_attr);
  sqlite3_bind_int(stmt, 9, mft_cluster_index);
  sqlite3_step(stmt);
  sqlite3_reset(stmt);
}
