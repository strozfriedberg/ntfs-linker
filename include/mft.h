/*
 * ntfs-linker
 * Copyright 2015 Stroz Friedberg, LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License is available at
 * <http://www.gnu.org/licenses/>.
 *
 * You can contact Stroz Friedberg by electronic and paper mail as follows:
 *
 * Stroz Friedberg, LLC
 * 32 Avenue of the Americas
 * 4th Floor
 * New York, NY, 10013
 * info@strozfriedberg.com
 */

#pragma once

#include "file.h"
#include "sqlite_util.h"

#include <iostream>
#include <string>

/*
Returns the column headers used in the MFT csv file
*/
std::string getMFTColumnHeaders();

/*
Parses all the MFT records
*/
void parseMFT(std::vector<File>& records, std::istream& input);

class SIAttribute {
public:
  SIAttribute() : Created(0), Modified(0), MFTModified(0), Accessed(0),
                  Usn(0), Valid(false) {}
  SIAttribute(char* buffer);

  uint64_t Created, Modified, MFTModified, Accessed;
  uint64_t Usn;
private:
  bool Valid;
};

class FNAttribute {
public:
  FNAttribute() : Parent(0), Created(0), Modified(0), MFTModified(0), Accessed(0),
                  LogicalSize(0), PhysicalSize(0), Name(""), Valid(false), NameType(0) {}
  FNAttribute(char* buffer);
  unsigned int Parent;
  uint64_t Created, Modified, MFTModified, Accessed;
  uint64_t LogicalSize, PhysicalSize;
  std::string Name;
  bool Valid;
  int NameType;

  bool operator<(const FNAttribute& other) const;
};

class MFTRecord {
public:
  MFTRecord(char* buffer, unsigned int len=1024);
  std::string toString(std::vector<File>& records);
  void insert(sqlite3_stmt* stmt, std::vector<File>& records);
  File asFile();

  unsigned int Record;
  SIAttribute Sia;

  // There may be multiple, but we'll pick just one.
  FNAttribute Fna;

private:
  uint64_t Lsn;
  bool isDir, isAllocated;
};
