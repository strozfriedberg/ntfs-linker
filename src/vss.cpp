#include "vss.h"

#include <libbfio.h>
#include <libvshadow.h>
#include <tsk/libtsk.h>
#include <libcerror.h>

#include <sstream>
#include <iostream>

TskVolumeBfioShimPtr globalTVBShim;
VshadowTskVolumeShimPtr globalVSTVShim;

libcerror_error_t* error;

class VSSException : public std::exception {
  public:
    VSSException(libcerror_error_t* error) : Error(error) {}
    virtual const char* what() const throw() {
      static char errStr[1024];
      std::stringstream ss;
      libcerror_error_sprint(Error, errStr, 1024);
      ss << "VSS Exception: " << errStr;
      return ss.str().c_str();
    }

  private:
    libcerror_error_t* Error;
};

int tvb_shim_free_wrapper(intptr_t** io_handle, libbfio_error_t **error)
  { return globalTVBShim->free(io_handle, error); }
int tvb_shim_clone_wrapper(intptr_t **destination_io_handle, intptr_t *source_io_handle, libbfio_error_t **error)
  { return globalTVBShim->clone(destination_io_handle, source_io_handle, error); }
int tvb_shim_open_wrapper(intptr_t *io_handle, int access_flags, libbfio_error_t **error)
  { return globalTVBShim->open(io_handle, access_flags, error); }
int tvb_shim_close_wrapper(intptr_t *io_handle, libbfio_error_t **error)
  { return globalTVBShim->close(io_handle, error); }
ssize_t tvb_shim_read_wrapper(intptr_t *io_handle, uint8_t *buffer, size_t size, libbfio_error_t **error)
  { return globalTVBShim->read(io_handle, buffer, size, error); }
ssize_t tvb_shim_write_wrapper(intptr_t *io_handle, const uint8_t *buffer, size_t size, libbfio_error_t **error)
  { return globalTVBShim->write(io_handle,  buffer, size, error); }
ssize_t tvb_shim_seek_offset_wrapper(intptr_t *io_handle, off64_t offset, int whence, libbfio_error_t **error)
  { return globalTVBShim->seek_offset(io_handle, offset, whence, error); }
int tvb_shim_exists_wrapper(intptr_t *io_handle, libbfio_error_t **error)
  { return globalTVBShim->exists(io_handle, error); }
int tvb_shim_is_open_wrapper(intptr_t *io_handle, libbfio_error_t **error)
  { return globalTVBShim->is_open(io_handle, error); }
int tvb_shim_get_size_wrapper(intptr_t *io_handle, size64_t *size, libbfio_error_t **error)
  { return globalTVBShim->get_size(io_handle, size, error); }

void vstv_shim_close(TSK_IMG_INFO* img)
  { return globalVSTVShim->close(img); }
void vstv_shim_imgstat(TSK_IMG_INFO* img, FILE* file)
  { return globalVSTVShim->imgstat(img, file); }
ssize_t vstv_shim_read(TSK_IMG_INFO *img, TSK_OFF_T off, char* buf, size_t len)
  { return globalVSTVShim->read(img, off, buf, len); }

int TskVolumeBfioShim::free(intptr_t **io_handle, libbfio_error_t ** error) {
  (void)error;
  if (Fs->tag != **io_handle) {
    std::cerr << "Invalid tag at line: " << __LINE__ << std::endl;
    return -1;
  }
  return 1;
}

int TskVolumeBfioShim::clone(intptr_t **destination_io_handle, intptr_t *source_io_handle, libbfio_error_t **error) {
  (void)destination_io_handle;
  (void)source_io_handle;
  (void)error;
  return -1;
}

int TskVolumeBfioShim::open(intptr_t *io_handle, int access_flags, libbfio_error_t ** error) {
  (void)error;
  (void)access_flags;
  if (Fs->tag != *io_handle) {
    std::cerr << "Invalid tag at line: " << __LINE__ << std::endl;
    return -1;
  }
  return 1;
}

int TskVolumeBfioShim::close(intptr_t *io_handle, libbfio_error_t ** error) {
  (void)error;
  if (Fs->tag != *io_handle) {
    std::cerr << "Invalid tag at line: " << __LINE__ << std::endl;
    return -1;
  }
  return 1;
}


ssize_t TskVolumeBfioShim::read(intptr_t *io_handle, uint8_t *buffer, size_t size, libbfio_error_t **error) {
  (void)error;
  if (Fs->tag != *io_handle) {
    std::cerr << "Invalid tag at line: " << __LINE__ << std::endl;
    return -1;
  }
  ssize_t rtnVal = tsk_img_read(Fs->img_info, Fs->offset + Offset, reinterpret_cast<char*>(buffer), size);
  if (rtnVal == -1) {
    std::cerr << tsk_error_get() << std::endl;
    return -1;
  }
  Offset += rtnVal;
  return rtnVal;
}

ssize_t TskVolumeBfioShim::write(intptr_t *io_handle, const uint8_t *buffer, size_t size, libbfio_error_t **error) {
  (void)buffer;
  (void)size;
  (void)error;
  if (Fs->tag != *io_handle) {
    std::cerr << "Invalid tag at line: " << __LINE__ << std::endl;
    return -1;
  }
  return -1;
}

ssize_t TskVolumeBfioShim::seek_offset(intptr_t *io_handle, off64_t offset, int whence, libbfio_error_t **error) {
  (void)error;
  if (Fs->tag != *io_handle) {
    std::cerr << "Invalid tag at line: " << __LINE__ << std::endl;
    return -1;
  }
  switch(whence) {
    case 0:
      Offset = offset;
      break;
    case 1:
      Offset += offset;
      break;
    case 2:
      Offset = Size + offset;
      break;
    default:
      std::cerr << "Invalid argument to seek" << std::endl;
      return -1;
  }

  return 1;
}

int TskVolumeBfioShim::exists(intptr_t *io_handle, libbfio_error_t ** error) {
  (void)error;
  if (Fs->tag != *io_handle) {
    std::cerr << "Invalid tag at line: " << __LINE__ << std::endl;
    return -1;
  }
  return 1;
}

int TskVolumeBfioShim::is_open(intptr_t *io_handle, libbfio_error_t ** error) {
  (void)error;
  if (Fs->tag != *io_handle) {
    std::cerr << "Invalid tag at line: " << __LINE__ << std::endl;
    return -1;
  }
  return 1;
}


int TskVolumeBfioShim::get_size(intptr_t *io_handle, size64_t *size, libbfio_error_t ** error) {
  (void)error;
  if (Fs->tag != *io_handle) {
    std::cerr << "Invalid tag at line: " << __LINE__ << std::endl;
    return -1;
  }
  *size = Size;
  return 1;
}

TskVolumeBfioShim::TskVolumeBfioShim(const TSK_FS_INFO* fs) : Fs(fs) {
  Size = Fs->block_count * Fs->block_size;
}

ssize_t VShadowTskVolumeShim::read(TSK_IMG_INFO *img, TSK_OFF_T off, char* buf, size_t len) {
  if (img->tag != TSK_IMG_INFO_TAG) {
    std::cerr << "Invalid tag at line: " << __LINE__ << std::endl;
    return -1;
  }
  ssize_t rtnVal = libvshadow_store_read_buffer_at_offset(Store, buf, len, off, &error);
  if (rtnVal == -1) {
    throw VSSException(error);
  }
  return rtnVal;
}

TSK_FS_INFO* VShadowTskVolumeShim::getTskFsInfo(TSK_IMG_INFO* img) {
  img->close = &vstv_shim_close;
  img->imgstat = &vstv_shim_imgstat;
  img->itype = TSK_IMG_TYPE_EXTERNAL;
  img->page_size = 2048;
  img->read = &vstv_shim_read;
  img->sector_size = 512;
  libvshadow_store_get_size(Store, reinterpret_cast<size64_t*>(&img->size), NULL);
  img->spare_size = 64;
  img->tag = TSK_IMG_INFO_TAG;

  TSK_FS_INFO* fs = tsk_fs_open_img(img, 0, TSK_FS_TYPE_NTFS);
  if (!fs) {
    std::cerr << "TSK error at " << __FILE__ << ":" << __LINE__ << ": "
              << tsk_error_get() << std::endl;
  }
  return fs;

}

VSS::VSS(TSK_FS_INFO* fs) : Volume(NULL), Store(NULL), VssFs(NULL), NumStores(0), Tag(fs->tag), Handle(NULL) {
  int rtnVal;
  globalTVBShim = TskVolumeBfioShimPtr(new TskVolumeBfioShim(fs));

  rtnVal = libbfio_handle_initialize(&Handle,
                                     &Tag,
                                     &tvb_shim_free_wrapper,
                                     &tvb_shim_clone_wrapper,
                                     &tvb_shim_open_wrapper,
                                     &tvb_shim_close_wrapper,
                                     &tvb_shim_read_wrapper,
                                     &tvb_shim_write_wrapper,
                                     &tvb_shim_seek_offset_wrapper,
                                     &tvb_shim_exists_wrapper,
                                     &tvb_shim_is_open_wrapper,
                                     &tvb_shim_get_size_wrapper,
                                     LIBBFIO_FLAG_IO_HANDLE_NON_MANAGED,
                                     &error);
  if (rtnVal != 1) {
    throw VSSException(error);
  }

  rtnVal = libvshadow_volume_initialize(&Volume, &error);
  if (rtnVal != 1) {
    throw VSSException(error);
  }

  rtnVal = libvshadow_volume_open_file_io_handle(Volume, Handle, LIBVSHADOW_ACCESS_FLAG_READ, &error);
  if (rtnVal != 1) {
    throw VSSException(error);
  }
  rtnVal = libvshadow_volume_get_number_of_stores(Volume, &NumStores, &error);
  if (rtnVal != 1) {
    throw VSSException(error);
  }
}

TSK_FS_INFO* VSS::getSnapshot(uint8_t n) {
  int rtnVal;
  freeSnapshot();

  rtnVal = libvshadow_volume_get_store(Volume, n, &Store, &error);
  if (rtnVal != 1) {
    throw VSSException(error);
  }

  globalVSTVShim = VshadowTskVolumeShimPtr(new VShadowTskVolumeShim(Store));
  VssImg = TskImgInfoPtr(new TSK_IMG_INFO);
  VssFs = globalVSTVShim -> getTskFsInfo(VssImg.get());
  return VssFs;
}

void VSS::freeSnapshot() {
  int rtnVal;

  if (VssFs) {
    tsk_fs_close(VssFs);
    VssFs = NULL;
  }

  if (VssImg) {
    tsk_img_close(VssImg.get());
    VssImg = NULL;
  }

  if (Store) {
    rtnVal = libvshadow_store_free(&Store, &error);
    if (rtnVal != 1)
      throw VSSException(error);
  }
}

VSS::~VSS() {
  std::cout << "~VSS" << std::endl;
  int rtnVal;
  freeSnapshot();
  if (Volume) {
    rtnVal = libvshadow_volume_free(&Volume, &error);
    if (rtnVal != 1)
      throw VSSException(error);
  }
  if (Handle) {
    rtnVal = libbfio_handle_free(&Handle, &error);
    if (rtnVal != 1)
      throw VSSException(error);
  }
}

int VSS::getNumStores() {
  return NumStores;
}
