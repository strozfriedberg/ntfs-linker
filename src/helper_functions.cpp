#include "file.h"
#include "utf8.h"
#include "helper_functions.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

/*
Returns the first SIZE bytes of the character array as a int64_t
If the result is too large to fit into a int64_t then overflow will occur
Reads the bytes as Little Endian
*/
uint64_t hex_to_long(const char* arr, int size) {
  uint64_t result = 0;
  for(int i = size - 1; i >= 0; i--) {
    result <<= 8;
    result +=  (unsigned char) arr[i];
  }
  return result;
}

/*
Converts filetime to unixtime
The filetime format is the number of 100 nanoseconds since 1601-01-01 (Assumed to be after the Gregorian Calendar cross-over date)
Unixtime is the number of seconds since 1970-01-01
*/
int64_t filetime_to_unixtime(int64_t t) {
  t -= 11644473600000ULL * 10000; //the number of 100 nano-seconds between 1601-01-01 and 1970-01-01
  t /= 10000000; //convert 100 nanoseconds to seconds
  return t;
}

std::wstring string_to_wstring(const std::string &str) {
  std::wstring temp(str.length(), L' ');
  copy(str.begin(), str.end(), temp.begin());
  return temp;
}

/*
Converts the filetime format used by microsoft windows files into ISO 8601 human-readable strings
The filetime format is the number of 100 nanoseconds since 1601-01-01 (Assumed to be after the Gregorian Calendar cross-over date)
Returned string format is YYYY-MM-DD HH:MM:SS 0000000 (nanoseconds)
*/
std::string filetime_to_iso_8601(uint64_t t) {
  int64_t unixtime = filetime_to_unixtime(t);
  time_t* time = (time_t*) &unixtime;
  struct tm* date = gmtime(time);

  char str[21];

  if (!strftime(str, 20, "%Y-%m-%d %H:%M:%S", date))
    return "";
  std::stringstream ss;
  ss << str << ".";
  ss << std::setw(7) << std::setfill('0') << (t % 10000000);
  return ss.str();
}

/*
Multi-byte character array to UTF8 string
Reads each 2 bytes of the charater array as a wide character and converts the UTF16 (??) result wstring to UTF8 string
Length of output string is len, reads len*2 bytes
*/
std::string mbcatos(const char* arr, uint64_t len) {
  std::vector<unsigned short> utf16;
  for(unsigned int i = 0; i < len; i++) {
    utf16.push_back(arr[2*i] + (arr[2*i+1]<<8));
  }
  std::string utf8;
  try {
    utf8::utf16to8(utf16.begin(), utf16.end(), std::back_inserter(utf8));
  } catch(utf8::invalid_utf16& e) {
    return "ERROR";
  }
  //delete any \t \r \n from utf8 string
  char chars[] = "\t\r\n";
  for(int i = 0; i < 3; i++)
    utf8.erase(std::remove(utf8.begin(), utf8.end(), chars[i]), utf8.end());
  return utf8;
}

/*
Uses the map of file records to construct the full file path.
If a file record is not present in the map then the empty stry "" is returned
*/
std::string getFullPath(const std::vector<File>& records, unsigned int record, std::vector<unsigned int>& stack) {
  std::stringstream ss;
  if (record >= records.size())
    return "";
  if (std::find(stack.begin(), stack.end(), record) != stack.end())
    return "CYCLICAL_HARD_LINK";
  File file(records[record]);
  if(record == file.Parent)
    return "";
  stack.push_back(record);
  ss << getFullPath(records, file.Parent, stack);
  ss << "\\" << file.Name;
  return ss.str();
}

std::string getFullPath(const std::vector<File>& records, unsigned int recordNo) {
  std::vector<unsigned int> stack;
  return getFullPath(records, recordNo, stack);
}

/*
prepares the ofstream for writing
opens the stream with whatever necessary flags, and writes any necessary start bits
*/
void prep_ofstream(std::ofstream& out, const std::string& name, bool overwrite) {
  std::ios_base::openmode mode = std::ios::out | std::ios::binary;
  if (overwrite)
    mode |= std::ios::trunc;
  else
    mode |= std::ios::app;

  out.open(name, mode);
//  unsigned char smarker[3];
//  smarker[0] = 0xEF;
//  smarker[1] = 0xBB;
//  smarker[2] = 0xBF;
//  out << smarker;
}

std::ostream& operator<<(std::ostream& out, EventTypes e) {
  switch(e) {
    case EventTypes::CREATE:
      return out << "Create";
    case EventTypes::DELETE:
      return out << "Delete";
    case EventTypes::MOVE:
      return out << "Move";
    default:
      return out << "Rename";
  }
}

std::ostream& operator<<(std::ostream& out, EventSources e) {
  switch(e) {
    case EventSources::USN:
      return out << "$UsnJrnl/$J";
    case EventSources::LOG:
      return out << "$LogFile";
    case EventSources::EMBEDDED_USN:
      return out << "$UsnJrnl entry in $LogFile";
    default:
      return out << "N/A";
  }
}

unsigned int countAscii(std::string name) {
  unsigned int count = 0;
  for (auto c: name) {
    if (!(c & 0x80))
      count++;
  }
  return count;
}

bool compareNames(std::string a, std::string b) {
  // Compares attributes based on the names
  // We prefer names which are ASCII, and after that names which are long.
  // TODO this uses the byte count of the UTF8 string, which could add preference to names with more non ascii characters...
  bool x = countAscii(a) == a.length();
  bool y = countAscii(b) == b.length();
  if (x != y)
    return y;
  return a.length() < b.length();
}

int doFixup(char* buffer, unsigned int len, unsigned int sectorSize) {
  // As a means of detecting sector corruption, NTFS replaces the last two bytes
  // of each sector with some magic, and stores the replaced bytes in the update
  // sequence array. Perform fixup on the buffer and return whether the sector
  // is corrupt. Only performs one record's worth of fixup, regardless of buffer
  // size, but ensures to not attempt to access outside the buffer.
  bool corrupt = false;
  if (len > 8) {
    unsigned int seqOffset = hex_to_long(buffer + 4, 2);
    unsigned int seqLen = hex_to_long(buffer + 6, 2);
    for(unsigned int i = 1; i < seqLen && 2*i + seqOffset < len && sectorSize * i <= len; i++) {
      unsigned int arrayOffset = seqOffset + 2*i;
      unsigned int dataOffset = sectorSize * i - 2;

      if (memcmp(buffer + dataOffset, buffer + seqOffset, 2) == 0) {
        // The magic number matches
        memcpy(buffer + dataOffset, buffer + arrayOffset, 2);
      }
      else {
        corrupt = true;
      }
    }
  }
  return corrupt;
}

int ceilingDivide(int n, int m) {
  // Returns ceil(n/m), without using clunky FP arithmetic
  return (n + m - 1) / m;
}
