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

#include <sstream>

#include "util.h"
#include "mft.h"
#include "file.h"
#include "progress.h"
#include "sqlite_util.h"

MFTRecord::MFTRecord(char* buffer, unsigned int len) {
  // MFT entries must begin with FILE
  if(hex_to_long(buffer, 4) != 0x454C4946) {
    return;
  }

  // Parse basic information from file record segment header
  Lsn                          = hex_to_long(buffer + 8,    8);
  Record                       = hex_to_long(buffer + 0x2c, 4);
  uint64_t allocation_flag     = hex_to_long(buffer + 0x16, 2);
  uint64_t mft_space_allocated = hex_to_long(buffer + 0x18, 4);
  uint64_t offset              = hex_to_long(buffer + 0x14, 2);

  isAllocated                  = allocation_flag & 0x1;
  isDir                        = allocation_flag & 0x2;

  // Parse the attributes
  while(offset + 0x16 <= len && offset + 0x16 <= mft_space_allocated) {

    uint64_t type_id          = hex_to_long(buffer + offset,        4);
    uint64_t attribute_length = hex_to_long(buffer + offset + 4,    4);
    uint64_t content_offset   = hex_to_long(buffer + offset + 0x14, 2);
    char* attribute_data      = buffer + offset + content_offset;

    switch(type_id) {
      case 0x10:
        Sia = SIAttribute(attribute_data);
        break;
      case 0x30:
        // Use the fna which is "largest" (based on ASCII-ness and size)
        FNAttribute fna2(attribute_data);
        if (!Fna.Valid)
          Fna = fna2;

        if (compareNames(Fna.Name, fna2.Name))
          Fna = fna2;
        break;
    }

    //check for valid attribute length value
    if(attribute_length > 0 && attribute_length < len)
      offset += attribute_length;
    else {
      break;
    }
  }
}

SIAttribute::SIAttribute(char* buffer) {
  Created     = hex_to_long(buffer + 0x0,  8);
  Modified    = hex_to_long(buffer + 0x8,  8);
  MFTModified = hex_to_long(buffer + 0x10, 8);
  Accessed    = hex_to_long(buffer + 0x18, 8);
  Usn         = hex_to_long(buffer + 0x40, 8);
  Valid       = true;
}

FNAttribute::FNAttribute(char* buffer) {
  Parent                = hex_to_long(buffer,        6);
  Created               = hex_to_long(buffer + 0x08, 8);
  Modified              = hex_to_long(buffer + 0x10, 8);
  MFTModified           = hex_to_long(buffer + 0x18, 8);
  Accessed              = hex_to_long(buffer + 0x20, 8);
  LogicalSize           = hex_to_long(buffer + 0x28, 8);
  PhysicalSize          = hex_to_long(buffer + 0x30, 8);
  unsigned int name_len = hex_to_long(buffer + 0x40, 1);
  Name                  = mbcatos    (buffer + 0x42, 2*name_len);
  Valid                 = true;
}

File MFTRecord::asFile() {
  return File(Fna.Name, Record, Fna.Parent, filetime_to_iso_8601(Sia.MFTModified));
}

void parseMFT(std::vector<File>& records, std::istream& input) {
  char buffer[1024];

  int records_processed = 0;

  input.clear();
  input.seekg(0, std::ios::end);
  uint64_t end = input.tellg();
  input.seekg(0, std::ios::beg);
  ProgressBar status(end);

  //scan through the $MFT one record at a time. Each record is 1024 bytes.
  while(!input.eof()) {
    status.setDone((uint64_t) input.tellg());
    records_processed++;
    input.read(buffer, 1024);
    doFixup(buffer, 1024, 512);
    MFTRecord record(buffer);
    for(int i = record.Record - records.size(); i >= 0; i--)
      records.push_back(File());
    records[record.Record] = record.asFile();
  }

  status.finish();
}
