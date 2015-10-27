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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <tsk/libtsk.h>
#include <libbfio.h>
#include <libvshadow.h>

#include <memory>

static const uint64_t TVB_SHIM_TAG = 0x96c5565f;
class TskVolumeBfioShim {
  public:
    TskVolumeBfioShim(const TSK_FS_INFO* fs);

    int free(libbfio_error_t **error);
    int clone(intptr_t **destination_io_handle, libbfio_error_t **error);
    int open(int access_flags, libbfio_error_t **error);
    int close(libbfio_error_t **error);
    ssize_t read(uint8_t *buffer, size_t size, libbfio_error_t **error);
    ssize_t write(const uint8_t *buffer, size_t size, libbfio_error_t **error);
    off64_t seek_offset(off64_t offset, int whence, libbfio_error_t **error);
    int exists(libbfio_error_t **error);
    int is_open(libbfio_error_t **error);
    int get_size(size64_t *size, libbfio_error_t **error);

    const uint32_t Tag;
  private:
    const TSK_FS_INFO* Fs;
    off64_t Offset;
    size64_t Size;
};
typedef std::unique_ptr<TskVolumeBfioShim> TskVolumeBfioShimPtr;

typedef std::unique_ptr<TSK_IMG_INFO> TskImgInfoPtr;

class VShadowTskVolumeShim {
  public:
    VShadowTskVolumeShim(libvshadow_store_t* store) : Store(store) {}
    void close() {}
    void imgstat(FILE* file) { (void) file; }
    ssize_t read(TSK_OFF_T off, char* buf, size_t len);
    TSK_FS_INFO* getTskFsInfo(TSK_IMG_INFO* img);
  private:
    libvshadow_store_t* Store;
};
typedef std::unique_ptr<VShadowTskVolumeShim> VShadowTskVolumeShimPtr;

static const uint32_t IMG_VSS_INFO_TAG = 0xd70a6a3b;
struct IMG_VSS_INFO {
  IMG_VSS_INFO() : Tag(IMG_VSS_INFO_TAG) {}
  TSK_IMG_INFO img_info;
  VShadowTskVolumeShimPtr VstvShim;
  const uint32_t Tag;
};
typedef std::unique_ptr<IMG_VSS_INFO> ImgVssInfoPtr;

class VSS {
  public:
    VSS(TSK_FS_INFO* fs);
    ~VSS();
    TSK_FS_INFO* getSnapshot(uint8_t n);
    int getNumStores();
  private:
    void freeSnapshot();

    TskVolumeBfioShimPtr TvbShim;
    libbfio_handle_t* Handle;
    libvshadow_volume_t* Volume;
    int NumStores;
    libvshadow_store_t* Store;
    ImgVssInfoPtr VssInfo;
    TSK_FS_INFO* VssFs;

};
