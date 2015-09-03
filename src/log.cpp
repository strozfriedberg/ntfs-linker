#include <iterator>

#include "log.h"
#include "helper_functions.h"
#include "mft.h"
#include "progress.h"
#include "sqlite_helper.h"
#include "usn.h"

/*
Decodes the LogFile Op code
*/

int ceilingDivide(int n, int m) {
  // Returns ceil(n/m), without using clunky FP arithmetic
  return (n + m - 1) / m;

}

std::string decodeLogFileOpCode(int op) {
  switch(op) {
    case LogOps::NOOP                               : return "Noop";
    case LogOps::COMPENSATION_LOG_RECORD            : return "CompensationLogRecord";
    case LogOps::INITIALIZE_FILE_RECORD_SEGMENT     : return "InitializeFileRecordSegment";
    case LogOps::DEALLOCATE_FILE_RECORD_SEGMENT     : return "DeallocateFileRecordSegment";
    case LogOps::WRITE_END_OF_FILE_RECORD_SEGMENT   : return "WriteEndOfFileRecordSegment";
    case LogOps::CREATE_ATTRIBUTE                   : return "CreateAttribute";
    case LogOps::DELETE_ATTRIBUTE                   : return "DeleteAttribute";
    case LogOps::UPDATE_RESIDENT_VALUE              : return "UpdateResidentValue";
    case LogOps::UPDATE_NONRESIDENT_VALUE           : return "UpdateNonresidentValue";
    case LogOps::UPDATE_MAPPING_PAIRS               : return "UpdateMappingPairs";
    case LogOps::DELETE_DIRTY_CLUSTERS              : return "DeleteDirtyClusters";
    case LogOps::SET_NEW_ATTRIBUTE_SIZES            : return "SetNewAttributeSizes";
    case LogOps::ADD_INDEX_ENTRY_ROOT               : return "AddIndexEntryRoot";
    case LogOps::DELETE_INDEX_ENTRY_ROOT            : return "DeleteIndexEntryRoot";
    case LogOps::ADD_INDEX_ENTRY_ALLOCATION         : return "AddIndexEntryAllocation";
    case LogOps::DELETE_INDEX_ENTRY_ALLOCATION      : return "DeleteIndexEntryAllocation";
    case LogOps::SET_INDEX_ENTRY_VCN_ALLOCATION     : return "SetIndexEntryVCNAllocation";
    case LogOps::UPDATE_FILE_NAME_ROOT              : return "UpdateFileNameRoot";
    case LogOps::UPDATE_FILE_NAME_ALLOCATION        : return "UpdateFileNameAllocation";
    case LogOps::SET_BITS_IN_NONRESIDENT_BIT_MAP    : return "SetBitsInNonresidentBitMap";
    case LogOps::CLEAR_BITS_IN_NONRESIDENT_BIT_MAP  : return "ClearBitsInNonresidentBitMap";
    case LogOps::PREPARE_TRANSACTION                : return "PrepareTransaction";
    case LogOps::COMMIT_TRANSACTION                 : return "CommitTransaction";
    case LogOps::FORGET_TRANSACTION                 : return "ForgetTransaction";
    case LogOps::OPEN_NONRESIDENT_ATTRIBUTE         : return "OpenNonresidentAttribute";
    case LogOps::DIRTY_PAGE_TABLE_DUMP              : return "DirtyPageTableDump";
    case LogOps::TRANSACTION_TABLE_DUMP             : return "TransactionTableDump";
    case LogOps::UPDATE_RECORD_DATA_ROOT            : return "UpdateRecordDataRoot";
    default:
      return "Invalid";
  }
}

/*
Parses the $LogFile
outputs to the various streams
*/
void parseLog(std::vector<File>& records, SQLiteHelper& sqliteHelper, std::istream& input, std::ostream& output) {
  unsigned int buffer_size = 4096;
  char* buffer = new char[buffer_size];
  bool split_record = false;
  bool done = false;
  bool parseError = true;
  int records_processed = 3;
  int adjust = 0;

  /*Skip past the junk at the beginning of the file
  The first two pages (0x0000 - 0x2000) are restart pages
  The next two pages (0x2000 - 0x4000) are buffer record pages
  in my testing I've seen very little of value here, and it doesn't follow the same format as the rest of the $LogFile
  */
  input.seekg(0, std::ios::end);
  uint64_t end = input.tellg();
  ProgressBar status(end);
  uint64_t start = 0x4000;
  input.seekg(start);
  input.read(buffer, 4096);
  doFixup(buffer, 4096, 512);

  output << LogRecord::getColumnHeaders();

  LogData transactions;
  transactions.clearFields();

  //scan through the $LogFile one  page at a time. Each record is 4096 bytes.
  while(!input.eof() && !done) {

    status.setDone((uint64_t) input.tellg() - start);
    //check log record header
    if(hex_to_long(buffer, 4) != 0x44524352) {
      input.read(buffer, 4096);
      doFixup(buffer, 4096, 512);
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
    offset = update_seq_offset + ceilingDivide(update_seq_count, 4) * 8;
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
      rec.insert(sqliteHelper.LogInsert);

      transactions.processLogRecord(rec, records, sqliteHelper);
      if(transactions.isTransactionOver()) {
        if(transactions.isCreateEvent()) {
          transactions.insertEvent(EventTypes::CREATE, sqliteHelper.EventInsert);
        }
        if(transactions.isDeleteEvent()) {
          transactions.insertEvent(EventTypes::DELETE, sqliteHelper.EventInsert);
        }
        if(transactions.isRenameEvent()) {
          transactions.insertEvent(EventTypes::RENAME, sqliteHelper.EventInsert);
        }
        if(transactions.isMoveEvent()) {
          transactions.insertEvent(EventTypes::MOVE, sqliteHelper.EventInsert);
        }
        transactions.clearFields();
      }
      offset += length;

    }

    /*
    If a record is left dangling across a page boundary, we can still parse the record
    The strategy is to rearrange the data like so:
    BEFORE: dangling record | RCRD header | rest of record | rest of page
    AFTER : RCRD header | dangling record | rest of record | rest of page
    We perform  a little switcheroo then return to the top of the loop
    */
    if(split_record) {
      unsigned int new_size = ceilingDivide(length - buffer_size + offset, 4032) * 4096 + buffer_size - offset;
      char* temp = new char[new_size];
      adjust = buffer_size - offset;
      input.read(temp + buffer_size - offset, 4096);
      doFixup(temp + buffer_size - offset, 4096, 512);
      if(input.eof()) {
        done = true;
        delete[] temp;
        break;
      }

      update_seq_offset = hex_to_long(temp + buffer_size - offset + 0x4, 2);
      update_seq_count = hex_to_long(temp + buffer_size - offset + 0x6, 2);
      unsigned int header_length = update_seq_offset + ceilingDivide(update_seq_count, 4) * 8;
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
        doFixup(temp, 4096, 512);

        update_seq_offset = hex_to_long(temp + 0x4, 2);
        update_seq_count = hex_to_long(temp + 0x6, 2);
        header_length = update_seq_offset + ceilingDivide(update_seq_count, 4) * 8;
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
      doFixup(buffer, 4096, 512);
      buffer_size = 4096;

      if(input.eof()) {
        done = true;
        break;
      }
      adjust = 0;
    }

  }
  status.finish();
  delete [] buffer;
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

void LogData::processLogRecord(LogRecord& rec, std::vector<File>& records, SQLiteHelper& sqliteHelper) {
  if(Lsn == 0) {
    Lsn = rec.cur_lsn;
  }
  RedoOps.push_back(rec.redo_op);
  UndoOps.push_back(rec.undo_op);

  char *redo_data = rec.data + 0x30 + rec.redo_offset;
  char *undo_data = rec.data + 0x30 + rec.undo_offset;
  //pull data from necessary opcodes to save for transaction runs
  if(rec.redo_op == LogOps::SET_BITS_IN_NONRESIDENT_BIT_MAP && rec.undo_op == LogOps::CLEAR_BITS_IN_NONRESIDENT_BIT_MAP) {
    if(rec.redo_length >= 4)
      Record = hex_to_long(redo_data, 4);
  }
  else if(rec.redo_op == LogOps::INITIALIZE_FILE_RECORD_SEGMENT && rec.undo_op == LogOps::NOOP) {
    //parse MFT record from redo op for create time, file name, parent dir
    //need to check for possible second MFT attribute header
    MFTRecord mftRec(redo_data, rec.redo_length);
    Timestamp = filetime_to_iso_8601(mftRec.Sia.Created);
    Parent = mftRec.Fna.Parent;
    if (compareNames(Name, mftRec.Fna.Name))
      Name = mftRec.Fna.Name;
  }
  else if(rec.redo_op == LogOps::DELETE_ATTRIBUTE && rec.undo_op == LogOps::CREATE_ATTRIBUTE) {
    //get the name before
    //from file attribute with header, undo op
    uint64_t type_id = hex_to_long(undo_data, 4);
    uint64_t content_offset = hex_to_long(undo_data + 0x14, 2);
    if (type_id == 0x30) {
      FNAttribute fna(undo_data + content_offset);
      PreviousParent = fna.Parent;

      if (compareNames(PreviousName, fna.Name))
        PreviousName = fna.Name;
    }
  }
  else if(rec.redo_op == LogOps::CREATE_ATTRIBUTE && rec.undo_op == LogOps::DELETE_ATTRIBUTE) {
    //get the name after
    //from file attribute with header, redo op
    //prev_name =

    uint64_t type_id = hex_to_long(redo_data, 4);
    uint64_t content_offset = hex_to_long(redo_data + 0x14, 2);
    if (type_id == 0x30) {
      FNAttribute fna(redo_data + content_offset);
      Parent = fna.Parent;

      if (compareNames(Name, fna.Name))
        Name = fna.Name;
    }
  }
  else if((rec.redo_op == LogOps::DELETE_INDEX_ENTRY_ALLOCATION && rec.undo_op == LogOps::ADD_INDEX_ENTRY_ALLOCATION) || (rec.redo_op == LogOps::DELETE_INDEX_ENTRY_ROOT && rec.undo_op == LogOps::ADD_INDEX_ENTRY_ROOT)) {
    if(rec.undo_length > 0x42) {
      // Delete or rename
      FNAttribute fna(undo_data + 0x10);
      Parent = fna.Parent;

      if (compareNames(Name, fna.Name))
        Name = fna.Name;
    }

  }
  else if ((rec.redo_op == LogOps::ADD_INDEX_ENTRY_ALLOCATION && rec.undo_op == LogOps::DELETE_INDEX_ENTRY_ALLOCATION) || (rec.redo_op == LogOps::ADD_INDEX_ENTRY_ROOT && rec.undo_op == LogOps::DELETE_INDEX_ENTRY_ROOT)) {
    // Add index entry root/AddIndexEntryAllocation operation
    // See https://flatcap.org/linux-ntfs/ntfs/concepts/index_record.html
    // for additional info about Index Record structure ("The header part")
    if (rec.redo_length > 0x52) {
      Record = hex_to_long(redo_data, 6);
      Parent = hex_to_long(redo_data + 0x10, 6);
      Timestamp = filetime_to_iso_8601(hex_to_long(redo_data + 0x18, 8));

      unsigned int len = hex_to_long(redo_data + 0x50, 1);
      std::string new_name = mbcatos(redo_data + 0x52, len);
      if (compareNames(Name, new_name))
        Name = new_name;
    }
  }
  else if (rec.redo_op == LogOps::UPDATE_NONRESIDENT_VALUE && rec.undo_op == LogOps::NOOP) {
    // Embedded $UsnJrnl/$J record
    UsnRecord usnRecord(redo_data, rec.redo_length, true);
    usnRecord.insert(sqliteHelper.UsnInsert, records);
    usnRecord.checkTypeAndInsert(sqliteHelper.EventInsert);

  }
}

void LogData::clearFields() {
  RedoOps.clear();
  UndoOps.clear();
  Record = -1;
  Parent = -1;
  PreviousParent = -1;
  Timestamp = "";
  Lsn = 0;
  Name = "";
  PreviousName = "";
}

/*
Returns whether the given transaction run (redo1, undo1) matches the
given transaction run pattern. Will attempt to find a matching entry in redo1, undo1 for each entry
in redo2, undo2 (in the same order)
if interchange is true then ADD_INDEX_ENTRY_ROOT=ADD_INDEX_ENTRY_ALLOCATION and DELETE_INDEX_ENTRY_ROOT=DELETE_INDEX_ENTRY_ALLOCATION
*/
bool LogData::transactionRunMatch(const std::vector<int>& redo2, const std::vector<int>& undo2, bool interchange) {
  unsigned int j = 0;
  std::vector<int> redo1(RedoOps);
  std::vector<int> undo1(UndoOps);
  for(unsigned int i = 0; i < redo2.size(); i++) {
    bool top = false;
    for(; j < redo1.size() && !top; j++) {

      if(interchange) {
        if(redo1[j] == LogOps::ADD_INDEX_ENTRY_ROOT)    redo1[j] = LogOps::ADD_INDEX_ENTRY_ALLOCATION;
        if(redo1[j] == LogOps::DELETE_INDEX_ENTRY_ROOT) redo1[j] = LogOps::DELETE_INDEX_ENTRY_ALLOCATION;
        if(undo1[j] == LogOps::ADD_INDEX_ENTRY_ROOT)    undo1[j] = LogOps::ADD_INDEX_ENTRY_ALLOCATION;
        if(undo1[j] == LogOps::DELETE_INDEX_ENTRY_ROOT) undo1[j] = LogOps::DELETE_INDEX_ENTRY_ALLOCATION;
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
  int i = 0;
  sqlite3_bind_int64(stmt, ++i, Record);
  sqlite3_bind_int64(stmt, ++i, Parent);
  sqlite3_bind_int64(stmt, ++i, PreviousParent);
  sqlite3_bind_int64(stmt, ++i, Lsn);
  sqlite3_bind_text (stmt, ++i, Timestamp.c_str()   , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, ++i, Name.c_str()        , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, ++i, PreviousName.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, ++i, type);
  sqlite3_bind_int64(stmt, ++i, EventSources::LOG);
  sqlite3_bind_int64(stmt, ++i, 0);  // Not embedded

  sqlite3_step(stmt);
  sqlite3_reset(stmt);
}

void LogRecord::insert(sqlite3_stmt* stmt) {
  int i = 0;
  sqlite3_bind_int64(stmt, ++i, cur_lsn);
  sqlite3_bind_int64(stmt, ++i, prev_lsn);
  sqlite3_bind_int64(stmt, ++i, undo_lsn);
  sqlite3_bind_int  (stmt, ++i, client_id);
  sqlite3_bind_int  (stmt, ++i, record_type);
  sqlite3_bind_text (stmt, ++i, decodeLogFileOpCode(redo_op).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, ++i, decodeLogFileOpCode(undo_op).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int  (stmt, ++i, target_attr);
  sqlite3_bind_int  (stmt, ++i, mft_cluster_index);

  sqlite3_step(stmt);
  sqlite3_reset(stmt);
}

std::string LogRecord::getColumnHeaders() {
  std::stringstream ss;
  ss << "Current Lsn\t"
     << "Previous Lsn\t"
     << "Undo Lsn\t"
     << "Client ID\t"
     << "Record Type\t"
     << "Redo Op\t"
     << "Undo Op\t"
     << "Target Attribute\t"
     << "MFT Cluster Index\t"
     << "Target VCN\t"
     << "Target LCN" << std::endl;
  return ss.str();
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
  return transactionRunMatch(LogData::createRedo, LogData::createUndo);
}

bool LogData::isDeleteEvent() {
  return transactionRunMatch(LogData::deleteRedo, LogData::deleteUndo);
}

bool LogData::isRenameEvent() {
  return Name != PreviousName && transactionRunMatch(LogData::renameRedo, LogData::renameUndo);
}

bool LogData::isMoveEvent() {
  return Parent != PreviousParent && transactionRunMatch(LogData::renameRedo, LogData::renameUndo);
}

bool LogData::isTransactionOver() {
  return RedoOps.back() == LogOps::FORGET_TRANSACTION && UndoOps.back() == LogOps::COMPENSATION_LOG_RECORD;
}

const std::vector<int> LogData::createRedo ({LogOps::SET_BITS_IN_NONRESIDENT_BIT_MAP,
                                              LogOps::NOOP,
                                              LogOps::ADD_INDEX_ENTRY_ALLOCATION,
                                              LogOps::INITIALIZE_FILE_RECORD_SEGMENT,
                                              LogOps::FORGET_TRANSACTION});
const std::vector<int> LogData::createUndo ({LogOps::CLEAR_BITS_IN_NONRESIDENT_BIT_MAP,
                                              LogOps::DEALLOCATE_FILE_RECORD_SEGMENT,
                                              LogOps::DELETE_INDEX_ENTRY_ALLOCATION,
                                              LogOps::NOOP,
                                              LogOps::COMPENSATION_LOG_RECORD});
const std::vector<int> LogData::deleteRedo ({LogOps::DELETE_INDEX_ENTRY_ALLOCATION,
                                              LogOps::DEALLOCATE_FILE_RECORD_SEGMENT,
                                              LogOps::CLEAR_BITS_IN_NONRESIDENT_BIT_MAP,
                                              LogOps::FORGET_TRANSACTION});
const std::vector<int> LogData::deleteUndo ({LogOps::ADD_INDEX_ENTRY_ALLOCATION,
                                              LogOps::INITIALIZE_FILE_RECORD_SEGMENT,
                                              LogOps::SET_BITS_IN_NONRESIDENT_BIT_MAP,
                                              LogOps::COMPENSATION_LOG_RECORD});
const std::vector<int> LogData::renameRedo ({LogOps::DELETE_INDEX_ENTRY_ALLOCATION,
                                              LogOps::DELETE_ATTRIBUTE,
                                              LogOps::CREATE_ATTRIBUTE,
                                              LogOps::ADD_INDEX_ENTRY_ALLOCATION,
                                              LogOps::FORGET_TRANSACTION});
const std::vector<int> LogData::renameUndo ({LogOps::ADD_INDEX_ENTRY_ALLOCATION,
                                              LogOps::CREATE_ATTRIBUTE,
                                              LogOps::DELETE_ATTRIBUTE,
                                              LogOps::DELETE_INDEX_ENTRY_ALLOCATION,
                                              LogOps::COMPENSATION_LOG_RECORD});
const std::vector<int> LogData::writeRedo  ({LogOps::DELETE_ATTRIBUTE,
                                              LogOps::CREATE_ATTRIBUTE,
                                              LogOps::SET_BITS_IN_NONRESIDENT_BIT_MAP,
                                              LogOps::SET_NEW_ATTRIBUTE_SIZES,
                                              LogOps::UPDATE_MAPPING_PAIRS,
                                              LogOps::SET_NEW_ATTRIBUTE_SIZES,
                                              LogOps::FORGET_TRANSACTION});
const std::vector<int> LogData::writeUndo  ({LogOps::CREATE_ATTRIBUTE,
                                              LogOps::DELETE_ATTRIBUTE,
                                              LogOps::CLEAR_BITS_IN_NONRESIDENT_BIT_MAP,
                                              LogOps::SET_NEW_ATTRIBUTE_SIZES,
                                              LogOps::UPDATE_MAPPING_PAIRS,
                                              LogOps::SET_NEW_ATTRIBUTE_SIZES,
                                              LogOps::COMPENSATION_LOG_RECORD});
