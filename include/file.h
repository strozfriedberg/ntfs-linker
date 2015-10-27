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

#include <string>

/*
Represents a file. A map of record numbers to these file objects can be used to reconstruct the full path
*/
class File {
  public:
    File () :
      Name(""),
      Record(0),
      Parent(0),
      Timestamp(""),
      Valid(false) {}
    File(std::string name, unsigned int record, unsigned int parent, std::string timestamp) :
      Name(name),
      Record(record),
      Parent(parent),
      Timestamp(timestamp),
      Valid(true) {}
    std::string Name;
    unsigned int Record, Parent;
    std::string Timestamp;
    bool Valid;
};
