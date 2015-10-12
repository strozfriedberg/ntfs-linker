#include "vss_handler.h"
#include "helper_functions.h"

#include <libbfio.h>
#include <libvshadow.h>
#include <tsk/libtsk.h>

#include <string>
#include <fstream>
#include <tuple>

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
    std::cout << offset << " " << bytesRead << std::endl;
    out.write(reinterpret_cast<char*>(&buffer), bytesRead);
    offset += bytesRead;
  }
  out.close();
}

int copyFiles(TSK_FS_INFO* fs) {
  std::vector<FileCopy> params { FileCopy("/$MFT", "", "MFT_FILE"),
                                FileCopy("/$LogFile", "", "LOGFILE_FILE"),
                                FileCopy("/$Extend/$UsnJrnl", "$J", "USNJRNL_FILE")};
  for (auto it = params.begin(); it != params.end(); ++it) {
    it->File = tsk_fs_file_open(fs, NULL, it->In.c_str());
    if (!it->File)
      return 1;
  }

  for(auto param: params) {
      write_file(param);
      tsk_fs_file_close(param.File);
  }
  return 0;
}

TSK_FILTER_ENUM VolumeWalker::filterFs(TSK_FS_INFO* fs) {
  int rtnVal;
  static char errStr[1024];
  std::cout << "Filterfs" << std::endl;
  TskVolumeBfioShim tvbShim(fs);
  globalTVBShim = &tvbShim;

  libbfio_handle_t* handle = NULL;
  intptr_t tag = fs->tag;
  intptr_t* io_handle = &tag;

  rtnVal = copyFiles(fs);
  if (rtnVal)
    return TSK_FILTER_SKIP;

  libbfio_error_t* error;
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
                                     LIBBFIO_FLAG_IO_HANDLE_NON_MANAGED,
                                     &error);
  libbfio_error_sprint(error, errStr, 1024);
  std::cout << errStr << std::endl;


  if (rtnVal == 1) {
    libvshadow_volume_t volume;
    libvshadow_error_t* error;
    libvshadow_volume_open_file_io_handle(&volume, handle, LIBVSHADOW_ACCESS_FLAG_READ, &error);

    libvshadow_error_sprint(error, errStr, 1024);
    std::cout << errStr << std::endl;
    int n;
    libvshadow_volume_get_number_of_stores(&volume, &n, NULL);
    for (int i = 0; i < n; i++) {
      libvshadow_store_t* store;
      libvshadow_volume_get_store(&volume, i, &store, NULL);

      VShadowTskVolumeShim bvtShim(store);
      globalVSTVShim = &bvtShim;

      TSK_IMG_INFO vss_img;
      TSK_FS_INFO* vss_fs = bvtShim.getTskFsInfo(&vss_img);
      copyFiles(vss_fs);
    }
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

int TskVolumeBfioShim::free(intptr_t **io_handle, libbfio_error_t ** error) {
  (void)error;
  if (Fs->tag != **io_handle) {
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
    return -1;
  }
  return 1;
}

int TskVolumeBfioShim::close(intptr_t *io_handle, libbfio_error_t ** error) {
  (void)error;
  if (Fs->tag != *io_handle) {
    return -1;
  }
  return 1;
}


ssize_t TskVolumeBfioShim::read(intptr_t *io_handle, uint8_t *buffer, size_t size, libbfio_error_t **error) {
  (void)error;
  if (Fs->tag != *io_handle) {
    return -1;
  }
  ssize_t rtnVal = tsk_img_read(Fs->img_info, Fs->offset + Offset, reinterpret_cast<char*>(buffer), size);
  if (rtnVal == -1)
    return -1;
  Offset += rtnVal;
  return 1;
}

ssize_t TskVolumeBfioShim::write(intptr_t *io_handle, const uint8_t *buffer, size_t size, libbfio_error_t **error) {
  (void)buffer;
  (void)size;
  (void)error;
  if (Fs->tag != *io_handle) {
    return -1;
  }
  return -1;
}

ssize_t TskVolumeBfioShim::seek_offset(intptr_t *io_handle, off64_t offset, int whence, libbfio_error_t **error) {
  (void)error;
  if (Fs->tag != *io_handle) {
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
  if (Fs->tag != *io_handle) {
    return -1;
  }
  return 1;
}

int TskVolumeBfioShim::is_open(intptr_t *io_handle, libbfio_error_t ** error) {
  (void)error;
  if (Fs->tag != *io_handle) {
    return -1;
  }
  return 1;
}


int TskVolumeBfioShim::get_size(intptr_t *io_handle, size64_t *size, libbfio_error_t ** error) {
  (void)error;
  if (Fs->tag != *io_handle) {
    return -1;
  }
  *size = Size;
  return 1;
}

TskVolumeBfioShim::TskVolumeBfioShim(const TSK_FS_INFO* fs) : Fs(fs) {
  Size = Fs->block_count * Fs->block_size;
}

ssize_t VShadowTskVolumeShim::read(TSK_IMG_INFO *img, TSK_OFF_T off, char* buf, size_t len) {
  if (img->tag != VSS_HANDLE_MAGIC)
    return -1;
  return libvshadow_store_read_buffer_at_offset(Store, buf, len, off, NULL);
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
  img->tag = VSS_HANDLE_MAGIC;

  return tsk_fs_open_img(img, 0, TSK_FS_TYPE_NTFS);

}
