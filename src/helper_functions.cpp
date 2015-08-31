
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <new>
#include <sstream>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "file.h"
#include "utf8.h"
#include "helper_functions.h"


/*
Returns the first SIZE characters as a hex string.
*/
std::string byte_to_str(char* bytes, int size) {
  char const hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
  std::string str;
  for(int i = 0; i < size; i++) {
    const char ch = bytes[i];
    str.append(&hex[(ch & 0xF0) >> 4], 1);
    str.append(&hex[ch & 0x0F], 1);
  }
  return str;
}

/*
Returns the first SIZE bytes of the character array as a long long
If the result is too large to fit into a long long then overflow will occur
Reads the bytes as Little Endian
*/
unsigned long long hex_to_long(const char* arr, int size) {
  unsigned long long result = 0;
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
long long filetime_to_unixtime(long long t) {
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
std::string filetime_to_iso_8601(unsigned long long t) {
  long long unixtime = filetime_to_unixtime(t);
  time_t* time = (time_t*) &unixtime;
  struct tm* date = gmtime(time);

  char str[21];

  if (!strftime(str, 20, "%Y-%m-%d %H:%M:%S", date))
    return "";
  std::stringstream ss;
  ss << str << " ";
  ss << std::setw(7) << std::setfill('0') << (t % 10000000);
  return ss.str();
}

/*
Multi-byte character array to UTF8 string
Reads each 2 bytes of the charater array as a wide character and converts the UTF16 (??) result wstring to UTF8 string
Length of output string is len, reads len*2 bytes
*/
std::string mbcatos(const char* arr, unsigned long long len) {
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
Unpacks the flag meaning from standard information attribute and file information attribute
The same flags are used for both sia flags and fna flags.
In order returned, flag meaning is:
Read only, Hidden, System, Archive, Device, Normal, Temporary, Sparse File, Reparse Point, Compressed, Offline, Not Indexed, Encrypted
*/
std::string getFlagMeaning(int flags) {
  std::stringstream ss;
  ss << (flags & 0x1) << ","<< (flags & 0x2) << "," <<(flags & 0x4) << "," << (flags & 0x20) << ","
    << (flags & 0x40) << "," << (flags & 0x80) << "," << (flags & 0x100) << "," << (flags & 0x200) << ","
    << (flags & 0x400) << "," << (flags & 0x800) << "," << (flags & 0x1000) << "," << (flags & 0x2000)
    << (flags & 0x4000);
  return ss.str();
}

int max(int a, int b) {
  return a > b? a: b;
}

/*
writes the buffer to the stream
*/
void mem_dump(char* buffer, int length, std::ostream& output) {
  for(int i = 0; i < length; i++)
    output << buffer[i];
}

/*
Uses the map of file records to construct the full file path.
If a file record is not present in the map then the empty stry "" is returned
*/
std::string getFullPath(const std::vector<File>& records, unsigned int recordNo, std::vector<unsigned int>& stack) {
  std::stringstream ss;
  if (recordNo >= records.size() || ! records[recordNo].valid)
    return "";
  if (std::find(stack.begin(), stack.end(), recordNo) != stack.end())
    return "CYCLICAL_HARD_LINK";
  File record = records[recordNo];
  if(recordNo == record.par_record_no)
    return record.name;
  stack.push_back(recordNo);
  ss << getFullPath(records, record.par_record_no, stack);
  ss << "\\" << record.name;
  return ss.str();
}

std::string getFullPath(const std::vector<File>& records, unsigned int recordNo) {
  std::vector<unsigned int> stack;
  return getFullPath(records, recordNo, stack);
}


char* getCmdOption(char** begin, char** end, const std::string& option) {
  char** itr = find(begin, end, option);
  if(itr != end && ++itr != end)
    return *itr;
  return 0;
}

bool cmdOptionExists(char**begin, char** end, const std::string& option) {
  return std::find(begin, end, option) != end;
}


/*
Returns the directory separator for the current OS.
e.g., '/' on unix and '\' on windows
*/
char getPathSeparator() {
  if(isUnix())
    return '/';
  return '\\';
}

/*
32-bit and 64-bit windows versions both define _WIN32
*/
bool isUnix() {
#ifdef _WIN32
  return false;
#else
  return true;
#endif
}

/*
prepares the ofstream for writing
opens the stream with whatever necessary flags, and writes any necessary start bits
*/
void prep_ofstream(std::ofstream& out, const char* name, bool overwrite) {
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
    default:
      return out << "$LogFile";
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
