# NTFS-Linker
Author: Zack Weger

Copyright (c) 2015, [Stroz Friedberg, LLC](http://www.strozfriedberg.com)

Status: Alpha

License: [LGPLv3](LICENSE-LGPLv3.txt)

## Description
NTFS Linker uses the `$MFT`, `$LogFile`, and `$UsnJrnl` to generate a "linked" 
history of file system activity on an NTFS volume. `$LogFile` and `$UsnJrnl` track
changes to files and folders over time. Linking the records in these logs with 
`$MFT` allows for the construction of a timeline of activity: 
creates, moves/renames, and deletes. NTFS Linker produces records that can 
easily be filtered to review different types of activity. In addition, 
NTFS-Linker is able to run across all Volume Shadow Copies (VSCs) on a volume, 
and produce output in a unified and deduplicated manner.


## Usage
```
ntfs-linker, Copyright (c) 2015, Stroz Friedberg, LLC
Version 0.1.0
Allowed options:
  --help                display help and exit
  --output arg          directory in which to dump output files
  --input arg           If no image specified, location of directory containing
                        input files: $MFT, $UsnJrnl, $LogFile. Otherwise, root 
                        directory in which to dump files extracted from image.
  --image arg           Path to image
  --version             display version number and exit
  --overwrite           overwrite files in the output directory. Default: 
                        append
  --extra               Outputs supplemental lower-level parsed data from 
                        $UsnJrnl and $LogFile
  ```


## Output

NTFS-Linker produces three TSV reports: events.txt, log.txt, and usn.txt.

- log.txt: contains listing of log record headers. Does not contain the
redo/undo operation data, so this report is of limited use.

- usn.txt: contains a "raw" view of `$UsnJrnl` entries in more detail than 
what events.txt provides, including all of the Reason flags.

- events.txt: contains a unified view of all file system events, as parsed from
both $UsnJrnl and $LogFile, ordered by event time from most recent to oldest 
(approximately--see below).

NTFS-Linker _also_ produces a SQLite database containing all of the above data. 
The database schema is designed for ease of querying, not full normalization.

## Installation
The source is in C++ and uses autotools for building. C++11 compiler support is
required. On a sane Unix, this should work:
```
./bootstrap.sh
./configure
make
sudo make install
```

NTFS-linker has dependencies on 
[SQLite](http://www.sqlite.org), 
[Boost](http://www.boost.org), 
[libtsk](http://www.sleuthkit.org), 
[libewf](http://github.com/libyal/libewf), 
[libbfio](http://github.com/libyal/libbfio), 
[libcerror](http://github.com/libyal/libcerror), 
and [libvshadow](http://github.com/libyal/libvshadow). The `configure` script 
should detect these dependencies on your system and warn you if any are missing.

`libewf` should be installed before building and installing `libtsk`.

Note that libvshadow must be compiled with libbfio enabled, and The Sleuthkit
must be compiled with ` --disable-multithreading` (which is only available in
version 4.3).

After installing the dependencies you may need to run:
```
sudo ldconfig
```

With sufficient wizardry, NTFS-linker can be built for Windows using mingw. For 
the impatient, prebuilt binaries can be downloaded from [somewhere?]().
