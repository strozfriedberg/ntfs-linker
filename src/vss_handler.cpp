#include "vss_handler.h"
#include "helper_functions.h"

#include <libbfio.h>
#include <libvshadow.h>
#include <tsk/libtsk.h>

#include <string>
#include <fstream>

TskVolumeBfioShim* globalTVBShim;
VShadowTskVolumeShim* globalVSTVShim;

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

void write_file(TSK_FS_FILE* file, const char* attr_name, std::string out_name) {
  uint16_t id = 0;
  TSK_FS_ATTR_TYPE_ENUM type = TSK_FS_ATTR_TYPE_NOT_FOUND;
  TSK_OFF_T offset = 0;
  bool ads = false;
  if (std::string(attr_name) != "") {
    TSK_FS_ATTR* attr = file->meta->attr->head;
    while (attr != NULL) {
      if (attr->name == attr_name) {
        id = attr->id;
        type = attr->type;
        ads = true;
        offset = attr->nrd.run->next->offset;
        break;
      }
      attr = attr->next;
    }
    if (!ads)
      return;
  }

  std::ofstream out(out_name, std::ios::out | std::ios::binary | std::ios::trunc);
  const size_t buffer_size = 4096;
  static char buffer[buffer_size];
  while (offset < file->meta->size) {
    ssize_t bytesRead;
    if (ads) {
      bytesRead = tsk_fs_file_read_type(file, type, id, offset, reinterpret_cast<char*>(&buffer), buffer_size, TSK_FS_FILE_READ_FLAG_NONE);
    }
    else {
      bytesRead = tsk_fs_file_read(file, offset, reinterpret_cast<char*>(&buffer), buffer_size, TSK_FS_FILE_READ_FLAG_NONE);
    }
    out.write(reinterpret_cast<char*>(&buffer), bytesRead);
    if (!bytesRead)
      break;
  }
  out.close();


}

void copyFiles(TSK_IMG_INFO* img) {
  TSK_FS_INFO* fs = tsk_fs_open_img(img, 0, TSK_FS_TYPE_NTFS);
  TSK_FS_FILE mft, usn, log;

  tsk_fs_file_open(fs, &mft, "/$MFT");
  tsk_fs_file_open(fs, &usn, "/$Extend/$UsnJrnl");
  tsk_fs_file_open(fs, &log, "/$LogFile");

  write_file(&mft, "", "$MFT");
  write_file(&usn, "$J", "$UsnJrnl");
  write_file(&log, "", "$LogFile");
}


TSK_FILTER_ENUM VolumeWalker::filterVol(const TSK_VS_PART_INFO* part) {
  int rtnVal;
  TskVolumeBfioShim tvbShim(part->vs->img_info, part);
  globalTVBShim = &tvbShim;

  libbfio_handle_t* handle = NULL;
  intptr_t tag = part->tag;
  intptr_t* io_handle = &tag;

  rtnVal = libbfio_handle_initialize(&handle,
                                     io_handle,
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
                                     0,
                                     NULL);
  copyFiles(part->vs->img_info);

  if (rtnVal == 1) {
    libvshadow_volume_t volume;
    libvshadow_volume_open_file_io_handle(&volume, handle, 0, NULL);

    int n;
    libvshadow_volume_get_number_of_stores(&volume, &n, NULL);
    for (int i = 0; i < n; i++) {
      libvshadow_store_t* store;
      libvshadow_volume_get_store(&volume, i, &store, NULL);

      VShadowTskVolumeShim bvtShim(store);
      globalVSTVShim = &bvtShim;

      TSK_IMG_INFO vss_img;
      bvtShim.getTskImgInfo(&vss_img);
      copyFiles(&vss_img);
    }
  }
  return TSK_FILTER_SKIP;
}

int TskVolumeBfioShim::free(intptr_t **io_handle, libbfio_error_t ** error) {
  (void)error;
  if (Part->tag != **io_handle) {
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
  if (Part->tag != *io_handle) {
    return -1;
  }
  return 1;
}

int TskVolumeBfioShim::close(intptr_t *io_handle, libbfio_error_t ** error) {
  (void)error;
  if (Part->tag != *io_handle) {
    return -1;
  }
  return 1;
}


ssize_t TskVolumeBfioShim::read(intptr_t *io_handle, uint8_t *buffer, size_t size, libbfio_error_t **error) {
  (void)error;
  if (Part->tag != *io_handle) {
    return -1;
  }
  ssize_t rtnVal = tsk_vs_part_read(Part, Offset, reinterpret_cast<char*>(buffer), size);
  if (rtnVal == -1)
    return -1;
  Offset += rtnVal;
  return 1;
}

ssize_t TskVolumeBfioShim::write(intptr_t *io_handle, const uint8_t *buffer, size_t size, libbfio_error_t **error) {
  (void)buffer;
  (void)size;
  (void)error;
  if (Part->tag != *io_handle) {
    return -1;
  }
  return -1;
}

ssize_t TskVolumeBfioShim::seek_offset(intptr_t *io_handle, off64_t offset, int whence, libbfio_error_t **error) {
  (void)error;
  if (Part->tag != *io_handle) {
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
      return -1;
  }

  return 1;
}

int TskVolumeBfioShim::exists(intptr_t *io_handle, libbfio_error_t ** error) {
  (void)error;
  if (Part->tag != *io_handle) {
    return -1;
  }
  return 1;
}

int TskVolumeBfioShim::is_open(intptr_t *io_handle, libbfio_error_t ** error) {
  (void)error;
  if (Part->tag != *io_handle) {
    return -1;
  }
  return 1;
}


int TskVolumeBfioShim::get_size(intptr_t *io_handle, size64_t *size, libbfio_error_t ** error) {
  (void)error;
  if (Part->tag != *io_handle) {
    return -1;
  }
  *size = Size;
  return 1;
}

TskVolumeBfioShim::TskVolumeBfioShim(const TSK_IMG_INFO* img, const TSK_VS_PART_INFO* part):
  Img(img),
  Part(part) {
    Size = Part->len * Img->sector_size;
}

ssize_t VShadowTskVolumeShim::read(TSK_IMG_INFO *img, TSK_OFF_T off, char* buf, size_t len) {
  if (img->tag != VSS_HANDLE_MAGIC)
    return -1;
  return libvshadow_store_read_buffer_at_offset(Store, buf, len, off, NULL);
}

void VShadowTskVolumeShim::getTskImgInfo(TSK_IMG_INFO* img) {
  img->close = &vstv_shim_close;
  img->imgstat = &vstv_shim_imgstat;
  img->itype = TSK_IMG_TYPE_EXTERNAL;
  img->page_size = 2048;
  img->read = &vstv_shim_read;
  img->sector_size = 512;
  libvshadow_store_get_size(Store, reinterpret_cast<size64_t*>(&img->size), NULL);
  img->spare_size = 64;
  img->tag = VSS_HANDLE_MAGIC;
}
