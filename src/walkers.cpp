#include "walkers.h"
#include "vss.h"

#include <fstream>
#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;


struct FileCopy{

  FileCopy(std::string in, std::string attr, std::string out) : In(in), Attr(attr), Out(out) {}

  std::string In, Attr, Out;
  TSK_FS_FILE* File;
};

void write_file(FileCopy& param) {
  TSK_FS_FILE* file = param.File;
  uint16_t id = 0;
  TSK_FS_ATTR_TYPE_ENUM type = TSK_FS_ATTR_TYPE_NOT_FOUND;
  TSK_OFF_T offset = 0;
  bool ads = false;
  if (param.Attr != "") {
    TSK_FS_ATTR* attr = file->meta->attr->head;
    while (attr != NULL) {
      if (attr->name && std::string(attr->name) == param.Attr) {
        id = attr->id;
        type = attr->type;
        ads = true;
        offset = attr->nrd.run->next->offset * file->fs_info->block_size;
        break;
      }
      attr = attr->next;
    }
    if (!ads)
      return;
  }

  std::ofstream out(param.Out, std::ios::out | std::ios::binary | std::ios::trunc);
  const size_t buffer_size = 1048576;
  static char buffer[buffer_size];
  while (1) {
    ssize_t bytesRead;
    if (ads) {
      bytesRead = tsk_fs_file_read_type(file, type, id, offset, reinterpret_cast<char*>(&buffer), buffer_size, TSK_FS_FILE_READ_FLAG_NONE);
    }
    else {
      bytesRead = tsk_fs_file_read(file, offset, reinterpret_cast<char*>(&buffer), buffer_size, TSK_FS_FILE_READ_FLAG_NONE);
    }
    if (bytesRead == -1)
      break;
    out.write(reinterpret_cast<char*>(&buffer), bytesRead);
    offset += bytesRead;
  }
  out.close();
}

int copyFiles(TSK_FS_INFO* fs, fs::path dir) {
  fs::create_directories(dir);
  std::vector<FileCopy> params { FileCopy("/$MFT", "", (dir / fs::path("$MFT")).string()),
                                FileCopy("/$LogFile", "", (dir / fs::path("$LogFile")).string()),
                                FileCopy("/$Extend/$UsnJrnl", "$J", (dir / fs::path("$J")).string())};
  for (auto it = params.begin(); it != params.end(); ++it) {
    it->File = tsk_fs_file_open(fs, NULL, it->In.c_str());
    if (!it->File) {
      std::cerr << tsk_error_get() << std::endl;
      return 1;
    }
  }

  for(auto param: params) {
      write_file(param);
      tsk_fs_file_close(param.File);
  }
  return 0;
}

std::string getFolderName(int i, int n) {
  int width = std::to_string(n).size();
  std::stringstream ss;
  ss << std::setfill('0') << std::setw(width) << i;
  return ss.str();
}
TSK_FILTER_ENUM VolumeWalker::filterFs(TSK_FS_INFO* fs) {
  std::cout << "Copying from base" << std::endl;

  // "base" has the important property that it sorts after numbers
  int rtnVal = copyFiles(fs, Root / fs::path("base"));

  if (rtnVal)
    return TSK_FILTER_SKIP;

  try {
    VSS vShadowVolume(fs);
    int n = vShadowVolume.getNumStores();
    for(int i = 0; i < n; ++i) {
      std::cout << "Copying from store: " << i << std::endl;
      TSK_FS_INFO* snapshot = vShadowVolume.getSnapshot(i);
      copyFiles(snapshot, Root / fs::path(getFolderName(i, n)));
      vShadowVolume.freeSnapshot();
    }

    vShadowVolume.free();
  }
  catch(std::exception& err) {
    std::cerr << err.what() << std::endl;
    return TSK_FILTER_SKIP;
  }

  return TSK_FILTER_SKIP;
}

uint8_t VolumeWalker::openImageUtf8(int a_numImg, const char *const a_images[], TSK_IMG_TYPE_ENUM a_imgType, unsigned int a_sSize) {
  uint8_t rtnVal = TskAuto::openImageUtf8(a_numImg, a_images, a_imgType, a_sSize);
  if (rtnVal) {
    std::cout << "TSK Error! Stopping." << std::endl;
    std::cout << tsk_error_get() << std::endl;
    exit(1);
  }
  return rtnVal;
}
