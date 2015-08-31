#include "file.h"
#include "helper_functions.h"
#include "sqlite_helper.h"

#include <fstream>
#include <sqlite3.h>
#include <map>
#include <vector>
#include <list>
#pragma once

class Event {
public:
  Event();
  void init(sqlite3_stmt* stmt);
  void write(int order, std::ostream& out, std::vector<File>& records);
  void updateRecords(std::vector<File>& records);

  unsigned long long Record, Parent, PreviousParent, UsnLsn, Type, Source;
  std::string Timestamp, Name, PreviousName;
  bool isAnchor;
};

class EventLNIS {
  /**
   * Computes the longest non-increasing subsequence of events by timestamp,
   * using a fixed-length search buffer. Once an element falls out of the buffer,
   * it is assumed to be in the LNIS
   */
public:
  EventLNIS(sqlite3_stmt* stmt, EventTypes type);
  void readEvents(sqlite3_stmt* stmt, EventTypes type);
  int advance(int order, std::vector<File>& records, std::ofstream& out, bool update);
  bool hasMore();
  std::string getTimestamp();

  std::vector<Event> Events;
  std::vector<int> Hits;

  std::list<int> LNIS;
  std::list<int>::iterator cursor;
  bool Started;
};

void outputEvents(std::vector<File>& records, SQLiteHelper& sqliteHelper, std::ofstream& o_events);

template<typename T>
std::list<int> computeLNIS(std::vector<T>& elements, std::vector<int>& hits) {
  // s[pos]: the index of the largest integer ending a non-increasing subsequence of length pos
  // p[pos]: the predecessor of element with index pos in the LNIS ending at element with index pos
  std::vector<int> s;
  std::vector<int> p;
  if (hits.size() == 0)
    return std::list<int>();
  p.reserve(hits.size());

  s.push_back(0);
  p.push_back(-1);
  for (unsigned int i = 1; i < hits.size(); i++) {
    T x(elements[hits[i]]);
    if (x <= elements[hits[s.back()]]) {
      p.push_back(s.back());
      s.push_back(i);
    }
    else {
      // The indices in s point to sorted events
      // We seek the largest number in s, which is less than x, and replace it with x

      int lo = 0;
      int hi = s.size() - 1;
      int mid;
      while (lo <= hi) {
        mid = (lo + hi) / 2;
        T y = elements[hits[s[mid]]];
        if (y < x)
          hi = mid - 1;
        else
          lo = mid + 1;
      }
      p.push_back(lo == 0 ? -1 : s[lo - 1]);
      s[lo] = i;
    }
  }

  // Now read predecessors
  int pos = p.size() - 1;
  std::list<int> lnis;
  while(pos >= 0) {
    lnis.push_front(hits[pos]);
    pos = p[pos];
  }
  return lnis;
}

