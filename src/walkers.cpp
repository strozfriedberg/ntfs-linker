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

#include "walkers.h"
#include "util.h"
#include "vss.h"

#include <fstream>
#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;


struct FileCopy{

  FileCopy(std::string in, std::string attr, std::string out) : In(in), Attr(attr), Out(out), File(NULL) {}

  std::string In, Attr, Out;
  TSK_FS_FILE* File;
};

int write_file(FileCopy& param) {
  TSK_FS_FILE* file = param.File;
  uint16_t id = 0;
  TSK_FS_ATTR_TYPE_ENUM type = TSK_FS_ATTR_TYPE_NOT_FOUND;
  TSK_OFF_T offset = 0;
  bool ads = false;
  if (!param.Attr.empty()) {
    if (!(file && file->meta && file->meta->attr && file->meta->attr->head))
      return 1;
    TSK_FS_ATTR* attr = file->meta->attr->head;
    while (attr != NULL) {
      if (attr->name && std::string(attr->name) == param.Attr) {
        id = attr->id;
        type = attr->type;
        ads = true;

        // Get the offset of the start of the second attribute run, if the _first_ run is sparse
        if (attr->nrd.run && (attr->nrd.run->flags & TSK_FS_ATTR_RUN_FLAG_SPARSE) && attr->nrd.run->next) {
          offset = attr->nrd.run->next->offset * file->fs_info->block_size;
        }
        break;
      }
      attr = attr->next;
    }
    if (!ads)
      return 1;
  }

  std::ofstream out(param.Out, std::ios::out | std::ios::binary | std::ios::trunc);
  const size_t buffer_size = 1048576;
  std::unique_ptr<char[]> buffer(new char[buffer_size]);
  while (1) {
    ssize_t bytesRead;
    if (ads) {
      bytesRead = tsk_fs_file_read_type(file, type, id, offset, buffer.get(), buffer_size, TSK_FS_FILE_READ_FLAG_NONE);
    }
    else {
      bytesRead = tsk_fs_file_read(file, offset, buffer.get(), buffer_size, TSK_FS_FILE_READ_FLAG_NONE);
    }
    if (bytesRead == -1)
      break;
    out.write(buffer.get(), bytesRead);
    offset += bytesRead;
  }
  out.close();
  return 0;
}

int copyFiles(TSK_FS_INFO* fs, fs::path dir) {
  std::vector<FileCopy> params { FileCopy("/$MFT", "", (dir / fs::path("$MFT")).string()),
                                FileCopy("/$LogFile", "", (dir / fs::path("$LogFile")).string()),
                                FileCopy("/$Extend/$UsnJrnl", "$J", (dir / fs::path("$J")).string())};
  bool hasAll = true;
  for (auto& param: params) {
    param.File = tsk_fs_file_open(fs, NULL, param.In.c_str());
    if (!param.File) {
      hasAll = false;
      std::cerr << "TSK error when opening file: " << param.In << ": " << tsk_error_get() << std::endl;
      break;
    }
  }

  if (!hasAll) {
    for (auto& param: params) {
      if (param.File) {
        tsk_fs_file_close(param.File);
      }
    }
    return 1;
  }

  fs::create_directories(dir);
  for(auto& param: params) {
      if (write_file(param)) {
        std::cerr << param.In << param.Attr << " file present, but we failed to copy it." << std::endl;
        return 1;
      }
      tsk_fs_file_close(param.File);
  }
  return 0;
}

std::string zeroPad(int i, int n) {
  int width = std::to_string(n).size();
  std::stringstream ss;
  ss << std::setfill('0') << std::setw(width) << i;
  return ss.str();
}

TSK_FILTER_ENUM VolumeWalker::filterFs(TSK_FS_INFO* fs) {
  NumCopied[fs->offset] = 0;
  if (!TSK_FS_TYPE_ISNTFS(fs->ftype)) {
    std::cout << "Skipping volume with fs offset " << fs->offset << " since it is not NTFS." << std::endl;
    return TSK_FILTER_SKIP;
  }

  fs::path dir(Root / ("volume_" + std::to_string(fs->offset)));
  std::cout << "Copying from volume " << fs->offset << ", base." << std::endl;

  // "base" has the important property that it sorts after numbers
  int rtnVal = copyFiles(fs, dir / fs::path("vss_base"));

  if (rtnVal) {
    std::cerr << "Unable to copy files out of volume with fs offset " << fs->offset << ". Skipping" << std::endl;
    return TSK_FILTER_SKIP;
  }
  NumCopied[fs->offset]++;
  DidItWork = true;

  try {
    VSS vShadowVolume(fs);
    int n = vShadowVolume.getNumStores();
    for(int i = 0; i < n; ++i) {
      std::cout << "Copying from volume " << fs->offset << ", VSC store " << i << "." << std::endl;
      TSK_FS_INFO* snapshot = vShadowVolume.getSnapshot(i);
      int rtnVal = copyFiles(snapshot, dir / fs::path("vss_" + zeroPad(i, n)));
      if (!rtnVal)
        NumCopied[fs->offset]++;
    }
  }
  catch(std::exception& err) {
    std::cerr << "=====================================================" << std::endl;
    std::cerr << "Could not read Volume Shadows from fs: " << fs->offset << ". Error: " << std::endl;
    std::cerr << err.what() << std::endl;
    std::cerr << "=====================================================" << std::endl;
    return TSK_FILTER_SKIP;
  }
  return TSK_FILTER_SKIP;
}

uint8_t VolumeWalker::openImageUtf8(int a_numImg, const char *const a_images[], TSK_IMG_TYPE_ENUM a_imgType, unsigned int a_sSize) {
  uint8_t rtnVal = TskAuto::openImageUtf8(a_numImg, a_images, a_imgType, a_sSize);
  if (rtnVal) {
    std::cerr << "TSK Error opening image." << std::endl;
    std::cerr << tsk_error_get() << std::endl;
    std::cerr << "Stopping." << std::endl;
    exit(1);
  }
  return rtnVal;
}

std::string VolumeWalker::getSummary() {
  std::ostringstream ss;
  int sum = 0;
  int count = 0;
  for (auto const& mapEntry: NumCopied) {
    sum += mapEntry.second;
    ss << "Volume " << mapEntry.first << ": ";
    if (mapEntry.second) {
      ss << "copied from " << pluralize("snapshot", mapEntry.second) << "\n";
      ++count;
    }
    else {
      ss << "no files were copied\n";
    }
  }
  ss << "Total: copied " << pluralize("volume", count) << ", "
     << pluralize("snapshot", sum) << ".";
  return ss.str();
}
