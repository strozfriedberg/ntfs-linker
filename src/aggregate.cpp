#include "aggregate.h"
#include "controller.h"
#include "file.h"
#include "util.h"
#include "sqlite_util.h"

#include <fstream>
#include <sqlite3.h>
#include <sstream>
#include <string>
#include <vector>

int writeAndStep(Event& event, sqlite3_stmt* step, sqlite3_stmt* update, std::vector<File>& records, int order, std::ofstream& out) {
  event.Order = order;
  event.write(out, records);
  event.updateRecords(records);

  sqlite3_bind_int64(update, event.Id, 0);
  sqlite3_bind_int64(update, event.Order, 1);
  return sqlite3_step(step);
}

void outputEvents(std::vector<File>& records, SQLiteHelper& sqliteHelper, VolumeIO& volumeIO, const VersionInfo& version) {
  int u, l;
  Event usnEvent, logEvent;
  int order = volumeIO.Count;
  std::ofstream& out(volumeIO.Events);

  sqliteHelper.bindForSelect(version);
  u = sqlite3_step(sqliteHelper.EventUsnSelect);
  l = sqlite3_step(sqliteHelper.EventLogSelect);

  // Output log events until the log event is a create, so we can compare timestamps properly.
  while (l == SQLITE_ROW) {
    logEvent.init(sqliteHelper.EventLogSelect);
    if (logEvent.Type == EventTypes::TYPE_CREATE) {
      break;
    }
    logEvent.IsAnchor = false;
    l = writeAndStep(logEvent, sqliteHelper.EventLogSelect, sqliteHelper.EventUpdate, records, ++order, out);
  }

  while (u == SQLITE_ROW && l == SQLITE_ROW) {
    usnEvent.init(sqliteHelper.EventUsnSelect);
    logEvent.init(sqliteHelper.EventLogSelect);

    if (usnEvent.Timestamp > logEvent.Timestamp) {
      usnEvent.IsAnchor = true;
      u = writeAndStep(usnEvent, sqliteHelper.EventUsnSelect, sqliteHelper.EventUpdate, records, ++order, out);
    }
    else {
      logEvent.IsAnchor = true;
      l = writeAndStep(logEvent, sqliteHelper.EventLogSelect, sqliteHelper.EventUpdate, records, ++order, out);

      while (l == SQLITE_ROW) {
        logEvent.init(sqliteHelper.EventLogSelect);
        if (logEvent.Type == EventTypes::TYPE_CREATE) {
          break;
        }
        l = writeAndStep(logEvent, sqliteHelper.EventLogSelect, sqliteHelper.EventUpdate, records, ++order, out);
      }
    }
  }

  while (u == SQLITE_ROW) {
    usnEvent.init(sqliteHelper.EventUsnSelect);
    usnEvent.IsAnchor = true;
    u = writeAndStep(usnEvent, sqliteHelper.EventUsnSelect, sqliteHelper.EventUpdate, records, ++order, out);
  }

  while (l == SQLITE_ROW) {
    logEvent.init(sqliteHelper.EventLogSelect);
    logEvent.IsAnchor = false;
    l = writeAndStep(logEvent, sqliteHelper.EventLogSelect, sqliteHelper.EventUpdate, records, ++order, out);
  }

  sqliteHelper.resetSelect();
  volumeIO.Count = order;
  return;
}

std::string textToString(const unsigned char* text) {
  return text == NULL ? "" : std::string(reinterpret_cast<const char*>(text));
}

void Event::init(sqlite3_stmt* stmt) {
  int i = -1;
  Id             = sqlite3_column_int64(stmt, ++i);
  Order          = sqlite3_column_int64(stmt, ++i);
  Record         = sqlite3_column_int64(stmt, ++i);
  Parent         = sqlite3_column_int64(stmt, ++i);
  PreviousParent = sqlite3_column_int64(stmt, ++i);
  UsnLsn         = sqlite3_column_int64(stmt, ++i);
  Timestamp      = textToString(sqlite3_column_text(stmt, ++i));
  Name           = textToString(sqlite3_column_text(stmt, ++i));
  PreviousName   = textToString(sqlite3_column_text(stmt, ++i));
  Type           = sqlite3_column_int(stmt, ++i);
  Source         = sqlite3_column_int(stmt, ++i);
  IsEmbedded     = sqlite3_column_int(stmt, ++i);
  Offset         = sqlite3_column_int64(stmt, ++i);
  Created        = textToString(sqlite3_column_text(stmt, ++i));
  Modified       = textToString(sqlite3_column_text(stmt, ++i));
  Comment        = textToString(sqlite3_column_text(stmt, ++i));
  Snapshot       = textToString(sqlite3_column_text(stmt, ++i));
  Volume         = textToString(sqlite3_column_text(stmt, ++i));

  if (PreviousParent == Parent)
    PreviousParent = -1;
  if (PreviousName == Name)
    PreviousName = "";
  IsAnchor = false;
}

Event::Event() {
  Record = Parent = PreviousParent = UsnLsn = Type = Source = -1;
  Volume = Snapshot = Timestamp = Name = PreviousName = "";
}

std::string Event::getColumnHeaders() {
  std::stringstream ss;
  ss << "Index"             << "\t"
     << "Timestamp"         << "\t"
     << "Source"            << "\t"
     << "Type"              << "\t"
     << "File Name"         << "\t"
     << "Folder"            << "\t"
     << "Full Path"         << "\t"
     << "MFT Record"        << "\t"
     << "Parent MFT Record" << "\t"
     << "USN/LSN"           << "\t"
     << "Old File Name"     << "\t"
     << "Old Folder"        << "\t"
     << "Old Parent Record" << "\t"
     << "Anchored"          << "\t"
     << "Offset"            << "\t"
     << "Created"           << "\t"
     << "Modified"          << "\t"
     << "Comment"           << "\t"
     << "Snapshot"          << "\t"
     << "Volume"            << std::endl;
  return ss.str();
}

void Event::write(std::ostream& out, const std::vector<File>& records) {
  out << Order                                                                         << "\t"
      << (IsAnchor ? Timestamp : "")                                                   << "\t"
      << (IsEmbedded ? EventSources::SOURCE_EMBEDDED_USN : static_cast<EventSources>(Source)) << "\t"
      << static_cast<EventTypes>(Type)                                                 << "\t"
      << Name                                                                          << "\t"
      << (Parent == -1 ? "" : getFullPath(records, Parent))                            << "\t"
      << (Record == -1 ? "" : getFullPath(records, Record))                            << "\t"
      << (Record == -1 ? "" : std::to_string(Record))                                  << "\t"
      << (Parent == -1 ? "" : std::to_string(Parent))                                  << "\t"
      << UsnLsn                                                                        << "\t"
      << PreviousName                                                                  << "\t"
      << (PreviousParent == -1 ? "" : getFullPath(records, PreviousParent))            << "\t"
      << (PreviousParent == -1 ? "" : std::to_string(PreviousParent))                  << "\t"
      << IsAnchor                                                                      << "\t"
      << Offset                                                                        << "\t"
      << Created                                                                       << "\t"
      << Modified                                                                      << "\t"
      << Comment                                                                       << "\t"
      << Snapshot                                                                      << "\t"
      << Volume                                                                        << std::endl;
}

void Event::updateRecords(std::vector<File>& records) {
  std::vector<File>::iterator it;
  switch(Type) {
    case EventTypes::TYPE_CREATE:
      // A file was created, so to move backwards, we should delete it
      // But let's leave it be.
      //records[Record].Valid = false;
      //records[Record] = File();
      break;
    case EventTypes::TYPE_DELETE:
      // A file was deleted, so to move backwards, create it
      if (Record >= 0)
        records[Record] = File(Name, Record, Parent, Timestamp);
      break;
    case EventTypes::TYPE_MOVE:
      // Embedded events haven't been aggregated, so before/after name not known
      if (!IsEmbedded)
        records[Record].Parent = PreviousParent;
      break;
    case EventTypes::TYPE_RENAME:
      // Embedded events haven't been aggregated, so before/after name not known
      if (!IsEmbedded && PreviousName != "")
        records[Record].Name = PreviousName;
      break;
  }
  return;
}
