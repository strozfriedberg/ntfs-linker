#include "vss_handler.h"

#include <libbfio.h>
#include <libvshadow.h>
#include <tsk/libtsk.h>

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

void initPartFromImg(TSK_IMG_INFO* img, TSK_VS_INFO* vs, TSK_VS_PART_INFO* part, TSK_VS_INFO* baseVol) {
  vs->block_size = baseVol-> block_size;
  vs->endian = TSK_BIG_ENDIAN;
  vs->img_info = img;
  vs->offset = 0;
  vs->part_count = 1;
  vs->part_list = part;
  vs->tag = VSS_HANDLE_MAGIC;
  vs->vstype = TSK_VS_TYPE_DBFILLER;

  part->addr = 0;
  part->desc = const_cast<char*>("");
  part->flags = TSK_VS_PART_FLAG_ALLOC;
  part->len = img->size / img->sector_size;
  part->next = NULL;
  part->prev = NULL;
  part->slot_num = 0;
  part->start = 0;
  part->table_num = 0;
  part->tag = VSS_HANDLE_MAGIC;
  part->vs = vs;

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
  if (rtnVal == 1) {
    libvshadow_volume_t volume;
    //rtnVal = libvshadow_volume_open_file_io_handle(&volume, handle, 0, NULL);
    (void)volume;
    libvshadow_store_t store;


    VShadowTskVolumeShim bvtShim(&store);
    TSK_IMG_INFO vss_img;
    bvtShim.getTskImgInfo(&vss_img);
    TSK_VS_INFO vss_vs;
    TSK_VS_PART_INFO vss_part;
    initPartFromImg(&vss_img, &vss_vs, &vss_part, part->vs);
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
