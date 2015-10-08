#include "vss_handler.h"

#include <libbfio.h>
#include <tsk/libtsk.h>

TskVolumeBfioShim* globalShim;

int shim_free_wrapper(intptr_t** io_handle, libbfio_error_t **error)
  { return globalShim->free(io_handle, error); }
int shim_clone_wrapper(intptr_t **destination_io_handle, intptr_t *source_io_handle, libbfio_error_t **error)
  { return globalShim->clone(destination_io_handle, source_io_handle, error); }
int shim_open_wrapper(intptr_t *io_handle, int access_flags, libbfio_error_t **error)
  { return globalShim->open(io_handle, access_flags, error); }
int shim_close_wrapper(intptr_t *io_handle, libbfio_error_t **error)
  { return globalShim->close(io_handle, error); }
ssize_t shim_read_wrapper(intptr_t *io_handle, uint8_t *buffer, size_t size, libbfio_error_t **error)
  { return globalShim->read(io_handle, buffer, size, error); }
ssize_t shim_write_wrapper(intptr_t *io_handle, const uint8_t *buffer, size_t size, libbfio_error_t **error)
  { return globalShim->write(io_handle,  buffer, size, error); }
ssize_t shim_seek_offset_wrapper(intptr_t *io_handle, off64_t offset, int whence, libbfio_error_t **error)
  { return globalShim->seek_offset(io_handle, offset, whence, error); }
int shim_exists_wrapper(intptr_t *io_handle, libbfio_error_t **error)
  { return globalShim->exists(io_handle, error); }
int shim_is_open_wrapper(intptr_t *io_handle, libbfio_error_t **error)
  { return globalShim->is_open(io_handle, error); }
int shim_get_size_wrapper(intptr_t *io_handle, size64_t *size, libbfio_error_t **error)
  { return globalShim->get_size(io_handle, size, error); }

TSK_FILTER_ENUM VolumeWalker::filterVol(const TSK_VS_PART_INFO* part) {
  TskVolumeBfioShim shim(part->vs->img_info, part);

  libbfio_handle_t* handle = NULL;
  intptr_t tag = part->tag;
  intptr_t* io_handle = &tag;

  int rtnVal = libbfio_handle_initialize(&handle,
                                         io_handle,
                                         &shim_free_wrapper,
                                         &shim_clone_wrapper,
                                         &shim_open_wrapper,
                                         &shim_close_wrapper,
                                         &shim_read_wrapper,
                                         &shim_write_wrapper,
                                         &shim_seek_offset_wrapper,
                                         &shim_exists_wrapper,
                                         &shim_is_open_wrapper,
                                         &shim_get_size_wrapper,
                                         0,
                                         NULL);
  (void)rtnVal;
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


