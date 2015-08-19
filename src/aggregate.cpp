#include "sqlite3.h"
#include "file.h"
#include "helper_functions.h"
#include "aggregate.h"

#include <fstream>
#include <map>
#include <string>
#include <sstream>

void prepare_statement(sqlite3 *db, sqlite3_stmt **stmt) {
  std::string sql = "select * from events where EventSource=? order by USN_LSN desc";
  sqlite3_prepare_v2(db, sql.c_str(), sql.length() + 1, stmt, NULL);
}

void outputEvents(std::map<unsigned int, File*> records, sqlite3* db, std::ofstream& out) {
  sqlite3_stmt *usn_stmt, *log_stmt;
  prepare_statement(db, &usn_stmt);
  sqlite3_bind_int64(usn_stmt, 1, event_sources::USN);
  prepare_statement(db, &log_stmt);
  sqlite3_bind_int64(log_stmt, 1, event_sources::LOG);

  int u, l;
  u = sqlite3_step(usn_stmt);
  l = sqlite3_step(log_stmt);
  Event usn_event, log_event;
  while (u == SQLITE_ROW && l == SQLITE_ROW) {
    // TODO selectively init based on what was updated
    usn_event.init(usn_stmt);
    log_event.init(log_stmt);

    if (log_event.type != event_types::CREATE || usn_event.timestamp > log_event.timestamp) {
      usn_event.write(out, records);
      usn_event.update_records(records);
      u = sqlite3_step(usn_stmt);
    } else {
      log_event.write(out, records);
      log_event.update_records(records);
      l = sqlite3_step(log_stmt);
      while (l == SQLITE_ROW) {
        log_event.init(log_stmt);
        if (log_event.type == event_types::CREATE)
          break;
        log_event.write(out, records);
        log_event.update_records(records);
        l = sqlite3_step(log_stmt);
      }
    }
  }

  while (u == SQLITE_ROW) {
    usn_event.init(usn_stmt);
    usn_event.write(out, records);
    usn_event.update_records(records);
    u = sqlite3_step(usn_stmt);
  }

  while (l == SQLITE_ROW) {
    log_event.init(log_stmt);
    log_event.write(out, records);
    log_event.update_records(records);
    l = sqlite3_step(log_stmt);
  }
  //TODO FINISH IT

  sqlite3_finalize(usn_stmt);
  sqlite3_finalize(log_stmt);
  return;
}

void Event::init(sqlite3_stmt* stmt) {
  int i = 0;
  record              = sqlite3_column_int(stmt, ++i);
  par_record          = sqlite3_column_int(stmt, ++i);
  previous_par_record = sqlite3_column_int(stmt, ++i);
  usn_lsn             = sqlite3_column_int(stmt, ++i);
  timestamp           = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, ++i)));
  file_name           = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, ++i)));
  previous_file_name  = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, ++i)));
  type                = sqlite3_column_int(stmt, ++i);
  source              = sqlite3_column_int(stmt, ++i);
}

Event::Event() {
  record = par_record = previous_par_record = usn_lsn = type = source = -1;
  timestamp = file_name = previous_file_name = "";
}

void Event::write(std::ostream& out, std::map<unsigned int, File*> records) {
  out << record << "\t"
      << par_record << "\t"
      << previous_par_record << "\t"
      << usn_lsn << "\t"
      << timestamp << "\t"
      << file_name << "\t"
      << previous_file_name << "\t"
      // TODO
      << getFullPath(records, record) << "\t"
      << getFullPath(records, par_record) << "\t"
      << getFullPath(records, previous_par_record) << "\t"
      << type << "\t"
      << source;
}

void Event::update_records(std::map<unsigned int, File*> records) {
  File* f;
  switch(type) {
    case event_types::CREATE:
      // A file was created, so to move backwards, delete it
      delete records[record];
      records.erase(record);
      break;
    case event_types::DELETE:
      // A file was deleted, so to move backwards, create it
      f = new File(file_name, record, par_record, timestamp);
      records[record] = f;
      break;
    case event_types::MOVE:
      records[record]->par_record_no = previous_par_record;
      break;
    case event_types::RENAME:
      records[record]->name = previous_file_name;
      break;
  }
  return;
}
