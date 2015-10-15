#pragma once

#include <tsk/libtsk.h>
#include <libbfio.h>
#include <libvshadow.h>

const int VSS_HANDLE_MAGIC = 0xBEEF;

TSK_FS_INFO* getVSS(TSK_FS_INFO* base, unsigned int num);
void closeVSS();

class TskVolumeBfioShim {
  public:
    TskVolumeBfioShim(const TSK_FS_INFO* fs);

    int free(intptr_t** io_handle, libbfio_error_t **error);
    int clone(intptr_t **destination_io_handle, intptr_t *source_io_handle, libbfio_error_t **error);
    int open(intptr_t *io_handle, int access_flags, libbfio_error_t **error);
    int close(intptr_t *io_handle, libbfio_error_t **error);
    ssize_t read(intptr_t *io_handle, uint8_t *buffer, size_t size, libbfio_error_t **error);
    ssize_t write(intptr_t *io_handle, const uint8_t *buffer, size_t size, libbfio_error_t **error);
    ssize_t seek_offset(intptr_t *io_handle, off64_t offset, int whence, libbfio_error_t **error);
    int exists(intptr_t *io_handle, libbfio_error_t **error);
    int is_open(intptr_t *io_handle, libbfio_error_t **error);
    int get_size(intptr_t *io_handle, size64_t *size, libbfio_error_t **error);
  private:
    const TSK_FS_INFO* Fs;
    off64_t Offset;
    size64_t Size;
};

class VShadowTskVolumeShim {
  public:
    VShadowTskVolumeShim(libvshadow_store_t* store) : Store(store) {}
    void close(TSK_IMG_INFO* img) { (void)img; }
    void imgstat(TSK_IMG_INFO* img, FILE* file) { (void)img; (void) file; }
    ssize_t read(TSK_IMG_INFO *img, TSK_OFF_T off, char* buf, size_t len);
    TSK_FS_INFO* getTskFsInfo(TSK_IMG_INFO* img);
  private:
    libvshadow_store_t* Store;
};

class VSS {
  public:
    VSS(TSK_FS_INFO* fs);
    TSK_FS_INFO* getSnapshot(uint8_t n);
    void freeSnapshot();
    void free();
    int getNumStores();
  private:
    libvshadow_store_t* Store;
    libvshadow_volume_t* Volume;
    TSK_IMG_INFO VssImg;
    TSK_FS_INFO* VssFs;
    int NumStores;
    intptr_t Tag;
    libbfio_handle_t* Handle;
};
