#include "aggregate.h"
#include "file.h"
#include "util.h"
#include "sqlite_util.h"

#include <fstream>
#include <sqlite3.h>
#include <sstream>
#include <string>
#include <vector>

void outputEvents(std::vector<File>& records, SQLiteHelper& sqliteHelper, std::ofstream& out, std::string snapshot) {
  int u;
  Event usn_event;

  sqliteHelper.bindForSelect(snapshot);
  u = sqlite3_step(sqliteHelper.EventUsnSelect);

  // Output log events until the log event is a create, so we can compare timestamps properly.
  EventLNIS log(sqliteHelper.EventLogSelect, EventTypes::CREATE);
  int order = 0;
  out << Event::getColumnHeaders();
  order = log.advance(order, records, out, false);

  while (u == SQLITE_ROW && log.hasMore()) {
    usn_event.init(sqliteHelper.EventUsnSelect);

    if (usn_event.Timestamp > log.getTimestamp()) {
      usn_event.IsAnchor = true;
      usn_event.write(++order, out, records);
      usn_event.updateRecords(records);
      u = sqlite3_step(sqliteHelper.EventUsnSelect);
    } else {
      order = log.advance(order, records, out, true);
    }
  }

  while (u == SQLITE_ROW) {
    usn_event.init(sqliteHelper.EventUsnSelect);
    usn_event.IsAnchor = true;
    usn_event.write(++order, out, records);
    usn_event.updateRecords(records);
    u = sqlite3_step(sqliteHelper.EventUsnSelect);
  }

  while(log.hasMore())
    order = log.advance(order, records, out, true);
  sqliteHelper.resetSelect();

  return;
}

std::string textToString(const unsigned char* text) {
  return text == NULL ? "" : std::string(reinterpret_cast<const char*>(text));
}

void Event::init(sqlite3_stmt* stmt) {
  int i = -1;
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

  if (PreviousParent == Parent)
    PreviousParent = -1;
  if (PreviousName == Name)
    PreviousName = "";
  IsAnchor = false;
}

Event::Event() {
  Record = Parent = PreviousParent = UsnLsn = Type = Source = -1;
  Snapshot = Timestamp = Name = PreviousName = "";
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
     << "Snapshot"          << std::endl;
  return ss.str();
}

void Event::write(int order, std::ostream& out, std::vector<File>& records) {
  out << order                                                                         << "\t"
      << (IsAnchor ? Timestamp : "")                                                   << "\t"
      << (IsEmbedded ? EventSources::EMBEDDED_USN : static_cast<EventSources>(Source)) << "\t"
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
      << Snapshot                                                                      << std::endl;
}

void Event::updateRecords(std::vector<File>& records) {
  std::vector<File>::iterator it;
  switch(Type) {
    case EventTypes::CREATE:
      // A file was created, so to move backwards, we should delete it
      // But let's leave it be.
      //records[Record].Valid = false;
      //records[Record] = File();
      break;
    case EventTypes::DELETE:
      // A file was deleted, so to move backwards, create it
      if (Record >= 0)
        records[Record] = File(Name, Record, Parent, Timestamp);
      break;
    case EventTypes::MOVE:
      // Embedded events haven't been aggregated, so before/after name not known
      if (!IsEmbedded)
        records[Record].Parent = PreviousParent;
      break;
    case EventTypes::RENAME:
      // Embedded events haven't been aggregated, so before/after name not known
      if (!IsEmbedded && PreviousName != "")
        records[Record].Name = PreviousName;
      break;
  }
  return;
}

int EventLNIS::advance(int order, std::vector<File>& records, std::ofstream& out, bool update) {
  int start, end;
  if (!LNIS.size())
    return order;

  if (Started) {
    start = *Cursor;
    Events[start].IsAnchor = true;
    ++Cursor;
    if (Cursor == LNIS.end()) {
      end = Events.size();
      Started = false;
    }
    else {
      end = *Cursor;
    }

  }
  else {
    start = 0;
    end = *Cursor;
    Started = true;
  }

  for(int i = start; i < end; ++i) {
    Events[i].write(++order, out, records);
    if (update)
      Events[i].updateRecords(records);
  }
  return order;
}

EventLNIS::EventLNIS(sqlite3_stmt* stmt, EventTypes type) : Started(false) {
  readEvents(stmt, type);

  std::vector<std::string> elements;
  elements.reserve(Events.size());
  for(auto event: Events)
    elements.push_back(event.Timestamp);
  LNIS = computeLNIS<std::string>(elements, Hits);
  Cursor = LNIS.begin();
  std::cout << "Found " << LNIS.size() << " out of " << Hits.size() << " possible anchor points."  << std::endl;
}

void EventLNIS::readEvents(sqlite3_stmt* stmt, EventTypes type) {
  Event x;

  int status = sqlite3_step(stmt);
  while (status == SQLITE_ROW) {
    x.init(stmt);
    Events.push_back(x);
    status = sqlite3_step(stmt);

    if (x.Type == type && x.Timestamp != "") {
      Hits.push_back(Events.size() - 1);
    }
  }
}

bool EventLNIS::hasMore() {
  return LNIS.size() && (Started || Cursor != LNIS.end());
}

std::string EventLNIS::getTimestamp() {
  return Events[*Cursor].Timestamp;
}
