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

#include <tsk/libtsk.h>

#include <boost/filesystem.hpp>
#include <string>

namespace fs = boost::filesystem;

class VolumeWalker: public TskAuto {
  public:
    VolumeWalker(fs::path root) : DidItWork(false), Root(root) {}
    virtual TSK_FILTER_ENUM filterFs(TSK_FS_INFO* fs);
    virtual TSK_RETVAL_ENUM processFile(TSK_FS_FILE*, const char*) { return TSK_OK; }
    virtual uint8_t openImageUtf8(int, const char *const images[], TSK_IMG_TYPE_ENUM, unsigned int a_ssize);
    bool DidItWork;
  private:
    fs::path Root;
};
