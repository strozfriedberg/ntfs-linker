#include "file.h"
#include "helper_functions.h"
#include "aggregate.h"
#include "sqlite_helper.h"

#include <fstream>
#include <vector>
#include <list>
#include <string>
#include <sstream>

#include <sqlite3.h>

void outputEvents(std::vector<File>& records, SQLiteHelper& sqliteHelper, std::ofstream& out) {
  int u;
  Event usn_event;

  u = sqlite3_step(sqliteHelper.EventUsnSelect);

  // Output log events until the log event is a create, so we can compare timestamps properly.
  EventLNIS log(sqliteHelper.EventLogSelect, EventTypes::CREATE);
  int order = 0;
  order = log.advance(order, records, out, false);

  while (u == SQLITE_ROW && log.hasMore()) {
    usn_event.init(sqliteHelper.EventUsnSelect);

    if (usn_event.Timestamp > log.getTimestamp()) {
      usn_event.isAnchor = true;
      usn_event.write(++order, out, records);
      usn_event.updateRecords(records);
      u = sqlite3_step(sqliteHelper.EventUsnSelect);
    } else {
      order = log.advance(order, records, out, true);
    }
  }

  while (u == SQLITE_ROW) {
    usn_event.init(sqliteHelper.EventUsnSelect);
    usn_event.isAnchor = true;
    usn_event.write(++order, out, records);
    usn_event.updateRecords(records);
    u = sqlite3_step(sqliteHelper.EventUsnSelect);
  }

  while(log.hasMore())
    order = log.advance(order, records, out, true);

  return;
}

void Event::init(sqlite3_stmt* stmt) {
  int i = -1;
  Record         = sqlite3_column_int64(stmt, ++i);
  Parent         = sqlite3_column_int64(stmt, ++i);
  PreviousParent = sqlite3_column_int64(stmt, ++i);
  UsnLsn         = sqlite3_column_int64(stmt, ++i);
  Timestamp      = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, ++i)));
  Name           = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, ++i)));
  PreviousName   = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, ++i)));
  Type           = sqlite3_column_int(stmt, ++i);
  Source         = sqlite3_column_int(stmt, ++i);

  if (PreviousParent == Parent)
    PreviousParent = 0;
  if (PreviousName == Name)
    PreviousName = "";
  isAnchor = false;
}

Event::Event() {
  Record = Parent = PreviousParent = UsnLsn = Type = Source = -1;
  Timestamp = Name = PreviousName = "";
}

void Event::write(int order, std::ostream& out, std::vector<File>& records) {
  out << order                                                             << "\t"
      << Record                                                            << "\t"
      << Parent                                                            << "\t"
      << PreviousParent                                                    << "\t"
      << UsnLsn                                                            << "\t"
      << Timestamp                                                         << "\t"
      << Name                                                              << "\t"
      << PreviousName                                                      << "\t"
      << getFullPath(records, Record)                                      << "\t"
      << getFullPath(records, Parent)                                      << "\t"
      << (PreviousParent == 0 ? "" : getFullPath(records, PreviousParent)) << "\t"
      << static_cast<EventTypes>(Type)                                     << "\t"
      << static_cast<EventSources>(Source)                                 << "\t"
      << isAnchor                                                          << std::endl;
}

void Event::updateRecords(std::vector<File>& records) {
  std::vector<File>::iterator it;
  switch(Type) {
    case EventTypes::CREATE:
      // A file was created, so to move backwards, delete it
      records[Record].Valid = false;
      records[Record] = File();
      break;
    case EventTypes::DELETE:
      // A file was deleted, so to move backwards, create it
      records[Record] = File(PreviousName, Record, Parent, Timestamp);
      break;
    case EventTypes::MOVE:
      records[Record].Parent = PreviousParent;
      break;
    case EventTypes::RENAME:
      records[Record].Name = PreviousName;
      break;
  }
  return;
}

int EventLNIS::advance(int order, std::vector<File>& records, std::ofstream& out, bool update) {
  int start, end;
  if (Started) {
    start = *cursor;
    Events[start].isAnchor = true;
    ++cursor;
    if (cursor == LNIS.end()) {
      end = Events.size();
      Started = false;
    }
    else {
      end = *cursor;
    }

  }
  else {
    start = 0;
    end = *cursor;
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
  cursor = LNIS.begin();
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
  return Started || cursor != LNIS.end();
}

std::string EventLNIS::getTimestamp() {
  return Events[*cursor].Timestamp;
}
