#include "log.h"
#include "util.h"
#include "mft.h"
#include "progress.h"
#include "sqlite_util.h"
#include "usn.h"

#include <cstring>
#include <iomanip>
#include <sstream>

/*
Decodes the LogFile Op code
*/
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
void parseLog(const std::vector<File>& records, SQLiteHelper& sqliteHelper, std::istream& input, std::ostream& output, const VersionInfo& version, bool extra) {
  unsigned int buffer_size = 4096;
  char* buffer = new char[buffer_size];
  bool split_record = false;
  bool done = false;
  bool parseError = true;
  int records_processed = 3;
  int adjust = 0;
  bool prev_has_next = true;

  /*Skip past the junk at the beginning of the file
  The first two pages (0x0000 - 0x2000) are restart pages
  The next two pages (0x2000 - 0x4000) are buffer record pages
  in my testing I've seen very little of value here, and it doesn't follow the same format as the rest of the $LogFile
  */
  input.clear();
  input.seekg(0, std::ios::end);
  uint64_t end = input.tellg();
  ProgressBar status(end);
  uint64_t start = 0x4000;
  input.seekg(start, std::ios::beg);
  input.read(buffer, 4096);
  doFixup(buffer, 4096, 512);

  output << LogRecord::getColumnHeaders();

  LogData transactions(version);
  transactions.clearFields();

  //scan through the $LogFile one  page at a time. Each record is 4096 bytes.
  while(!input.eof() && !done) {

    status.setDone((uint64_t) input.tellg() - start);
    //check log record header
    if(hex_to_long(buffer, 4) != 0x44524352) {
      input.read(buffer, 4096);
      if (input.eof())
        break;
      doFixup(buffer, 4096, 512);
      buffer_size = 4096;
      adjust = 0;
      continue;
    }
    records_processed++;
    unsigned int update_seq_offset, update_seq_count, offset, next_record_offset;
    unsigned int length = 0;
    update_seq_offset = hex_to_long(buffer + 0x4, 2);
    update_seq_count = hex_to_long(buffer + 0x6, 2);
    offset = update_seq_offset + ceilingDivide(update_seq_count, 4) * 8;
    next_record_offset = hex_to_long(buffer + 0x18, 2);
    if(parseError) { //initialize the offset on the "first" record processed
      offset = next_record_offset;
      parseError = false;
      transactions.clearFields();
    }

    //parse log record
    while(offset + 0x30 <= buffer_size) {
      int64_t cur_offset = static_cast<long int>(input.tellg()) - buffer_size + offset - adjust;
      if (transactions.Offset == -1)
        transactions.Offset = cur_offset;
      LogRecord rec(version);
      int rtnVal = rec.init(buffer + offset, cur_offset, prev_has_next);
      prev_has_next = rec.LcnsToFollow;
      if(rtnVal == -1) {
        split_record = true;
        length = rec.ClientDataLength + 0x30;
        break;
      } else if(rtnVal < 0) {
        length = rec.ClientDataLength + 0x30;
        parseError = true;
        break;
      } else {
        length = rtnVal;
      }

      if (extra) {
        output << rec;
        rec.insert(sqliteHelper.LogInsert);
      }

      transactions.processLogRecord(records, rec, sqliteHelper, cur_offset);
      if(transactions.isTransactionOver()) {
        if(transactions.isCreateEvent()) {
          transactions.insertEvent(EventTypes::TYPE_CREATE, sqliteHelper.EventInsert);
        }
        if(transactions.isDeleteEvent()) {
          transactions.insertEvent(EventTypes::TYPE_DELETE, sqliteHelper.EventInsert);
        }
        if(transactions.isRenameEvent()) {
          transactions.insertEvent(EventTypes::TYPE_RENAME, sqliteHelper.EventInsert);
        }
        if(transactions.isMoveEvent()) {
          transactions.insertEvent(EventTypes::TYPE_MOVE, sqliteHelper.EventInsert);
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
      if(input.eof()) {
        done = true;
        delete[] temp;
        break;
      }
      doFixup(temp + buffer_size - offset, 4096, 512);

      update_seq_offset = hex_to_long(temp + buffer_size - offset + 0x4, 2);
      update_seq_count = hex_to_long(temp + buffer_size - offset + 0x6, 2);
      unsigned int header_length = update_seq_offset + ceilingDivide(update_seq_count, 4) * 8;
      memmove(temp, temp + buffer_size - offset, header_length);
      memcpy(temp + header_length, buffer + offset, buffer_size - offset);
      delete[] buffer;
      buffer = temp;
      // Flag the record as not crossing the current page
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
        if(input.eof()) {
          done = true;
          delete[] temp;
          break;
        }
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
      if(input.eof()) {
        done = true;
        break;
      }
      doFixup(buffer, 4096, 512);
      buffer_size = 4096;

      adjust = 0;
    }

  }

  if (transactions.PrevUsnRecord.Usn != 0) {
    transactions.PrevUsnRecord.checkTypeAndInsert(sqliteHelper.EventInsert);
  }
  status.finish();
  delete [] buffer;
}

int LogRecord::init(char* buffer, uint64_t offset, bool prev_has_next) {
  Data = buffer;
  Offset = offset;
  CurrentLsn = hex_to_long(buffer, 8);
  PreviousLsn = hex_to_long(buffer + 0x8, 8);
  UndoLsn = hex_to_long(buffer + 0x10, 8);

  ClientDataLength = hex_to_long(buffer + 0x18, 4);

  ClientId = hex_to_long(buffer + 0x1C, 4);
  RecordType = hex_to_long(buffer + 0x20, 4);

  /*
  Not particularly a concern. Sometimes there is extra slack space at the end of a page.
  In which case, we read that the record type is 0 and print an error message
  (since 0 is not a valid record type).
  If these error messages are spamming error output, however, then something probably went
  horribly wrong. Most likely cause is that the parser offset became misplaced and tried to
  parse the wrong bits of the records.
  */
  if(RecordType == 0 && prev_has_next) {
    std::cerr << std::setw(60) << std::left << std::setfill(' ') << "\r";
    std::cerr << "Invalid record type: " << RecordType << std::endl;
    if(RecordType == 0) {
      return -2;
    }
    exit(0);
  }
  else if (RecordType == 0) {
    return -2;
  }

  /*
  A flag on a record means that at least part of the record is on the next page.
  We do some fancy switcheroo stuff at the bottom of the loop to compensate.
  */
  Flags = hex_to_long(buffer + 0x28, 2);
  if(Flags == 1) {
    return -1;
  }

  RedoOp = hex_to_long(buffer + 0x30, 2);
  UndoOp = hex_to_long(buffer + 0x32, 2);

  // We've run into some junk data
  if(RedoOp > 0x21 || UndoOp > 0x21) {
    std::cerr << std::setw(60) << std::left << std::setfill(' ') << "\r";
    std::cerr << "\rInvalid op code: " << std::hex << RedoOp << " " << UndoOp
              << " at 0x" << offset << std::endl;
    return -2;
  }
  RedoOffset = hex_to_long(buffer + 0x34, 2);
  RedoLength = hex_to_long(buffer + 0x36, 2);
  UndoOffset = hex_to_long(buffer + 0x38, 2);
  UndoLength = hex_to_long(buffer + 0x3a, 2);
  TargetAttribute = hex_to_long(buffer + 0x3c, 2);
  LcnsToFollow = hex_to_long(buffer + 0x3e, 2);

  RecordOffset = hex_to_long(buffer + 0x40, 2);
  AttributeOffset = hex_to_long(buffer + 0x42, 2);
  MftClusterIndex = hex_to_long(buffer + 0x44, 2);
  TargetVcn = hex_to_long(buffer + 0x48, 4);

  TargetLcn = hex_to_long(buffer + 0x50, 4);

  /*
  The length given by ClientDataLength is actually 0x30 less than the length of the record.
  */

  return 0x30 + ClientDataLength;

}

void LogData::processLogRecord(const std::vector<File>& records, LogRecord& rec, SQLiteHelper& sqliteHelper, uint64_t fileOffset) {
  if(Lsn == 0) {
    Lsn = rec.CurrentLsn;
  }
  RedoOps.push_back(rec.RedoOp);
  UndoOps.push_back(rec.UndoOp);

  char *redo_data = rec.Data + 0x30 + rec.RedoOffset;
  char *undo_data = rec.Data + 0x30 + rec.UndoOffset;
  //pull data from necessary opcodes to save for transaction runs
  if(rec.RedoOp == LogOps::SET_BITS_IN_NONRESIDENT_BIT_MAP && rec.UndoOp == LogOps::CLEAR_BITS_IN_NONRESIDENT_BIT_MAP) {
    if(rec.RedoLength >= 4)
      Record = hex_to_long(redo_data, 4);
  }
  else if(rec.RedoOp == LogOps::INITIALIZE_FILE_RECORD_SEGMENT && rec.UndoOp == LogOps::NOOP) {
    //parse MFT record from redo op for create time, file name, parent dir
    //need to check for possible second MFT attribute header
    MFTRecord mftRec(redo_data, rec.RedoLength);
    // Modified timestamp!
    // In case of file system tunneling (i.e., this event is really a write),
    // the Creation time is not the event time - it's the time the file was _originally_ created
    // https://support.microsoft.com/en-us/kb/299648
    Timestamp = filetime_to_iso_8601(mftRec.Sia.Modified);
    Parent = mftRec.Fna.Parent;

    Created = filetime_to_iso_8601(mftRec.Sia.Created);
    Modified = filetime_to_iso_8601(mftRec.Sia.Modified);
    std::stringstream commentSS;
    if (Created != filetime_to_iso_8601(mftRec.Fna.Created))
      commentSS << "Creates don't match, ";
    if (Modified != filetime_to_iso_8601(mftRec.Fna.Modified))
      commentSS << "Modifies don't match";
    Comment = commentSS.str();

    if (compareNames(Name, mftRec.Fna.Name))
      Name = mftRec.Fna.Name;
  }
  else if(rec.RedoOp == LogOps::DELETE_ATTRIBUTE && rec.UndoOp == LogOps::CREATE_ATTRIBUTE) {
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
  else if(rec.RedoOp == LogOps::CREATE_ATTRIBUTE && rec.UndoOp == LogOps::DELETE_ATTRIBUTE) {
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
  else if((rec.RedoOp == LogOps::DELETE_INDEX_ENTRY_ALLOCATION && rec.UndoOp == LogOps::ADD_INDEX_ENTRY_ALLOCATION) || (rec.RedoOp == LogOps::DELETE_INDEX_ENTRY_ROOT && rec.UndoOp == LogOps::ADD_INDEX_ENTRY_ROOT)) {
    if(rec.UndoLength > 0x42) {
      // Delete or rename
      FNAttribute fna(undo_data + 0x10);
      Parent = fna.Parent;

      if (compareNames(Name, fna.Name))
        Name = fna.Name;
    }

  }
  else if ((rec.RedoOp == LogOps::ADD_INDEX_ENTRY_ALLOCATION && rec.UndoOp == LogOps::DELETE_INDEX_ENTRY_ALLOCATION) || (rec.RedoOp == LogOps::ADD_INDEX_ENTRY_ROOT && rec.UndoOp == LogOps::DELETE_INDEX_ENTRY_ROOT)) {
    // Add index entry root/AddIndexEntryAllocation operation
    // See https://flatcap.org/linux-ntfs/ntfs/concepts/index_record.html
    // for additional info about Index Record structure ("The header part")
    // TODO REFACTOR MAKE THIS ITS OWN CLASS
    if (rec.RedoLength > 0x52) {
      Record = hex_to_long(redo_data, 6);
      Parent = hex_to_long(redo_data + 0x10, 6);
      Timestamp = filetime_to_iso_8601(hex_to_long(redo_data + 0x18, 8));

      unsigned int len = hex_to_long(redo_data + 0x50, 1);
      std::string new_name = mbcatos(redo_data + 0x52, 2*len);
      if (compareNames(Name, new_name))
        Name = new_name;
    }
  }
  else if (rec.RedoOp == LogOps::UPDATE_NONRESIDENT_VALUE && rec.UndoOp == LogOps::NOOP) {
    // Embedded $UsnJrnl/$J record
    UsnRecord usnRecord(redo_data, fileOffset + 0x30 + rec.RedoOffset, VersionInfo(Snapshot, Volume), rec.RedoLength, true);
    usnRecord.insert(sqliteHelper.UsnInsert, records);
    if (PrevUsnRecord.Record != usnRecord.Record || PrevUsnRecord.Reason & UsnReasons::USN_CLOSE) {
      PrevUsnRecord.checkTypeAndInsert(sqliteHelper.EventInsert);
      PrevUsnRecord.clearFields();
    }
    if (PrevUsnRecord.Usn == 0)
      PrevUsnRecord = usnRecord;
    PrevUsnRecord.update(usnRecord);

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
  Offset = -1;
  Created = "";
  Modified = "";
  Comment = "";
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
  sqlite3_bind_int64(stmt, ++i, EventSources::SOURCE_LOG);
  sqlite3_bind_int64(stmt, ++i, 0);  // Not embedded
  sqlite3_bind_int64(stmt, ++i, Offset);
  sqlite3_bind_text (stmt, ++i, Created.c_str()     , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, ++i, Modified.c_str()    , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, ++i, Comment.c_str()     , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, ++i, Snapshot.c_str()    , -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, ++i, Volume.c_str()      , -1, SQLITE_TRANSIENT);

  sqlite3_step(stmt);
  sqlite3_reset(stmt);
}

void LogRecord::insert(sqlite3_stmt* stmt) {
  int i = 0;
  sqlite3_bind_int64(stmt, ++i, CurrentLsn);
  sqlite3_bind_int64(stmt, ++i, PreviousLsn);
  sqlite3_bind_int64(stmt, ++i, UndoLsn);
  sqlite3_bind_int  (stmt, ++i, ClientId);
  sqlite3_bind_int  (stmt, ++i, RecordType);
  sqlite3_bind_text (stmt, ++i, decodeLogFileOpCode(RedoOp).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, ++i, decodeLogFileOpCode(UndoOp).c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int  (stmt, ++i, TargetAttribute);
  sqlite3_bind_int  (stmt, ++i, MftClusterIndex);
  sqlite3_bind_int64(stmt, ++i, Offset);
  sqlite3_bind_text (stmt, ++i, Snapshot.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, ++i, Volume.c_str()  , -1, SQLITE_TRANSIENT);

  sqlite3_step(stmt);
  sqlite3_reset(stmt);
}

std::string LogRecord::getColumnHeaders() {
  std::stringstream ss;
  ss << "Current Lsn"       << "\t"
     << "Previous Lsn"      << "\t"
     << "Undo Lsn"          << "\t"
     << "Client ID"         << "\t"
     << "Record Type"       << "\t"
     << "Redo Op"           << "\t"
     << "Undo Op"           << "\t"
     << "Target Attribute"  << "\t"
     << "MFT Cluster Index" << "\t"
     << "Target VCN"        << "\t"
     << "Target LCN"        << "\t"
     << "Offset"            << "\t"
     << "Snapshot"          << "\t"
     << "Volume"            << std::endl;
  return ss.str();
}

std::ostream& operator<<(std::ostream& out, const LogRecord& rec) {
  out << rec.CurrentLsn                  << "\t"
      << rec.PreviousLsn                 << "\t"
      << rec.UndoLsn                     << "\t"
      << rec.ClientId                    << "\t"
      << rec.RecordType                  << "\t"
      << decodeLogFileOpCode(rec.RedoOp) << "\t"
      << decodeLogFileOpCode(rec.UndoOp) << "\t"
      << rec.TargetAttribute             << "\t"
      << rec.MftClusterIndex             << "\t"
      << rec.TargetVcn                   << "\t"
      << rec.TargetLcn                   << "\t"
      << rec.Offset                      << "\t"
      << rec.Snapshot                    << "\t"
      << rec.Volume                      << std::endl;
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
