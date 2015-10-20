#pragma once

#include <tsk/libtsk.h>

#include <boost/filesystem.hpp>
#include <string>

namespace fs = boost::filesystem;

class VolumeWalker: public TskAuto {
  public:
    VolumeWalker(fs::path root) : Root(root) {}
    virtual TSK_FILTER_ENUM filterFs(TSK_FS_INFO* fs);
    virtual uint8_t openImageUtf8(int, const char *const images[], TSK_IMG_TYPE_ENUM, unsigned int a_ssize);
    virtual TSK_RETVAL_ENUM processFile(TSK_FS_FILE*, const char*) { return TSK_OK; }
  private:
    fs::path Root;
    bool Processed;
};

