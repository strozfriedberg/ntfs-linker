# NTFS-Linker
Author: Zack Weger

Copyright (c) Stroz Friedberg, LLC

Status: Alpha

## Description
NTFS Linker uses the `$MFT`, `$LogFile`, and `$UsnJrnl` to generate a "linked" 
history of file system activity on an NTFS volume. $LogFile and $UsnJrnl track
changes to files and folders over time. Linking the records in these logs with 
`$MFT` allows for the construction of a timeline of activity: 
creates, moves/renames, and deletes. NTFS Linker produces a UTF-8 tab separated 
value (TSV) report of records that can easily be filtered to review different 
types of activity. In addition, NTFS-Linker is able to run across all Volume
Shadow Copies (VSCs) on a volume, and produce output in a unified and
deduplicated manner.


## Usage
```
ntfs-linker, Copyright (c) Stroz Friedberg, LLC
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
both $UsnJrnl and $LogFile, ordered(approximately - see below) by event time,
from most recent to oldest.

### General Notes
All timestamps are in the format YYYY-mm-dd HH:MM:SS.1234567.
Windows stores timestamps as the number of hundred nanoseconds since 1601 
(FILETIME). The routines used by NTFS-Linker to parse the time use standard 
C++ libraries, which may result in incorrect timestamps in some cases.
Specifically, if the time is BEFORE 1970 or AFTER 2038, the timestamp will 
not be displayed properly. The 1234567 refers to the 100-nanosecond part of 
the timestamp.

### usn.txt
The USN Journal reason code uses a bit packing scheme for each possible reason. 
From the time a file is opened to the time it is closed, the reasons will be 
combined. This means that multiple reasons may show up for a particular entry, 
even though only one operation happens at a time. The order the reasons are 
printed is completely arbitrary and has no correlation to the order in which 
they occurred.

#### USN Journal Example:
- USN|FILE_CREATE
- USN|CLOSE|FILE_CREATE
- USN|DATA_EXTEND
- USN|DATA_EXTEND|DATA_OVERWRITE
- USN|BASIC_INFO_CHANGE|DATA_EXTEND|DATA_OVERWRITE
- USN|BASIC_INFO_CHANGE|CLOSE|DATA_EXTEND|DATA_OVERWRITE

### log.txt
$LogFile event analysis is much more complicated.  Each record contains a redo 
and undo op code, as well as redo and undo data. In the case of data write 
events, it is possible to recover the data written for resident files, or the 
file sectors on disc for non-resident files. At this time NTFS-Linker does not 
recover the data written.

### events.txt

#### Event Source
This can be `$UsnJrnl/$J`, `$LogFile`, or "`$UsnJrnl` entry in `$LogFile`". `$LogFile` actually contains complete
`$UsnJrnl` entries.

#### File names and paths
While a file name can be extracted directly from a Log/Usn entry, the paths must be calculated.
The "folder" path is the calculated path to the parent directory, and the "Path" column is the calculated path to the
MFT record number if available.

#### Anchor and Event Ordering
While the exact order of `$LogFile` and `$UsnJrnl` events individually is known, the combined
ordering is not. They must be ordered according to the event timestamps, but NTFS-Linker places more confidence in
some timestamps than others. `$UsnJrnl` timestamps are taken at face value, while $LogFile create timestamps are not.
The timestamp for the `$LogFile` create event is the created timestamp in the MFT at the time of creation, which is not
necessarily the creation time. NTFS-Linker makes a principled best guess of the "true" `$LogFile` creation timestamps, 
and uses those to merge with `$UsnJrnl`. Anchored events denote events with timestamps which NTFS-Linker used for ordering.
The `$LogFile` events are represented in the order they occur, and will almost certainly have occurred between the 
anchoring `$UsnJrnl` events.

For example, the `$LogFile` events in the below snippet will almost certainly have occurred on 2012-04-08 02:55, as they
are anchored by two `$UsnJrnl` events within that time period.

| Timestamp   | Source                         | Event  | File                  |
|-------------|--------------------------------|--------|-----------------------|
| 4/8/12 2:55 | `$UsnJrnl/$J`                  | Delete | SAE.dat-journal       |
| 4/8/12 2:55 | `$UsnJrnl` entry in `$LogFile` | Create | SAE.dat-journal       |
| 4/8/12 2:55 | `$UsnJrnl` entry in `$LogFile` | Create | SAE.dat-journal       |
| 4/8/12 2:55 | `$LogFile`                     | Create | SAE.dat-journal       |
|             | `$LogFile`                     | Delete | 5.tmp                 |
|             | `$LogFile`                     | Create | 5.ini                 |
|             | `$LogFile`                     | Delete | 3.tmp                 |
|             | `$LogFile`                     | Create | 3.ini                 |
|             | `$LogFile`                     | Delete | 1.tmp                 |
|             | `$LogFile`                     | Create | 1.ini                 |
|             | `$LogFile`                     | Delete | SAEPolicy.dat-journal |
| 4/8/12 2:55 | `$UsnJrnl` entry in `$LogFile` | Create | SAE.dat-journal       |
| 4/8/12 2:55 | `$UsnJrnl/$J`                  | Create | SAE.dat-journal       |

#### Offset
The actual file offset (in decimal) to the beginning of the event record in the source file.

#### Created, Modified, Comment
For `$LogFile` create events, the timestamps from the Standard Information attribute
(regardless of the faith NTFS-Linker places in them) and whether those timestamps match the corresponding timestamps
in the File Name Attribute.

#### `$UsnJrnl` events
`$UsnJrnl` records are combined in events.txt to display one event for each logical event that
actually occurred. For instance, for a rename event, `$UsnJrnl` will contain at least two records: one containing the
"old name" and one containing the "new name" (and probably a couple other records, for the same event), but events.txt
displays this event just once. *However*, for the `$UsnJrnl` events embedded in `$LogFile`, this deduplication is not
performed. This means that for an embedded rename/move event, the File Name and MFT Record could be either from before
or after.

## Implementation Details

This section contains notes on the inner-workings of NTFS-Linker. Specifically,
we outline the process by which NTFS-Linker recovers events from `$LogFile`, 
`$UsnJrnl:$J`, and `$MFT`. While the structures of these files are fairly well
known, their inter-related meaning requires explanation.

### Background: Sequencing

In this document, by `sequence`, we mean some ordered collection of objects,
which could possibly repeat. Given a sequence, we can obtain a `subsequence`
by *removing* zero or more elements. For example, the following is a sequence:

    25 67 38 97 58 94 29 66 23 92 60 8 47 50 98 28 13 91 61 72

And the following are all subsequences of the above sequence:

|     | Subsequence                                                | Note                    |
| --- | ---------------------------------------------------------- | ----------------------- |
| S1  | 25 67 38 97 58 94 29 66 23 92 60 8 47 50 98 28 13 91 61 72 | zero elements removed   |
| S2  |                                                            | (all elements removed)  |
| S3  | 25 38 58 29 23 60 47 98 13 61                              | (some elements removed) |
| S4  | 25 38 47 50 72                                             | (some elements removed) |

An `increasing sequence` is a sequence in which every element is larger than the
element which precedes it. While sequence S1 and S3 are not increasing, S4 and
S2 (trivially) are. Thus given a sequence, a natural question which arises is:
what is this sequence's `longest increasing subsequence`? It turns out there is
a polynomial-time algorithm which answers this question. In general, a sequence
can actually have multiple longest increasing subequences (which are all the
same length). In this example, `25 38 58 66 92 98` and `25 38 47 50 61 72` are
both longest increasing subsequences.

### Changes in State
Put simply, a filesystem event implies a change in state of the filesystem. When
reporting on an event, it's often desirable to display that in the context of
the state of the filesystem. This should be done in the context of the
filesystem *at the time the event occurred*. For instance, suppose we know the
state of a filesystem at some point in time (say, from a base image) to be:

| Recordnum | Full Path      |
| --------- | -------------- |
| 10        | /foo/bar       |
| 11        | /foo/bar/a.txt |
| 12        | /foo/bar/b.txt |

Suppose that immediately prior to the filesystem being imaged, we know that
an entry with recordnum 13, parent recordnum 10 named `some_name` was deleted. 
Now suppose that just before *that*, we know an entry with recordnum 14, parent
recordnum 13, named `file.txt` was deleted. What was the full path of this
record *at the time it was deleted*? We know that it must have been
`/foo/bar/some_name/file.txt`! Thus we observe it's necessary to accumulate
changes in state of the filesystem as events are processed, in order to recover
the context of each event.


### Event Ordering
At some point we arrive at this situation: we have a sequence of events from
`$UsnJrnl` and a sequence of events from `$LogFile`. For the `$UsnJrnl` events
we know the event timestamps, but for the `$LogFile` events we have less
information. For `$LogFile` rename, move, and delete events, we are not able to
recover an event timestamp. But for `$LogFile` create events, we are able to
recover the File Creation timestamp *at the time the file was created*. Even
under normal operating conditions, it's possible this is not the event time.

The impact is this: we know the order of events in `$UsnJrnl` and `$LogFile`
separately, and we have some idea of the times these events occurred, but we
don't know how the two sequences fit together. If both sequences were
increasing, then we could perform a standard merge (by say, taking two cursors
on the sequences and taking the smaller value from either cursor). But the
`$LogFile` sequence is not increasing, so this approach wouldn't work. The
solution is to first compute the longest increasing subsequence of the
`$LogFile` events, and using those events to "anchor" the entire sequence for
merging.


### Volume Shadows and Processing Order
A Volume Shadow Copy represents a snapshot of a filesystem at a particular time.
Often, multiple shadow copies may be present which are close together in time
(for example, shadow copies created as a result of running system updates). In
this situation, `$UsnJrnl` and `$LogFile` may actually overlap some between the
snapshots. To deduplicate this, we defer to the older snapshot. We grab all
events from the oldest snapshot, then only the events not present in the oldest
snapshot from the next oldest, etc., until we've processed the base snapshot.

To output events, however, we need to process the Shadow Copies in the reverse
order, with the most recent events first, because the most recent events imply
a change in state of the file system. Thus we process all the events from the
base image (not including events which are found in the most recent shadow
copy!), and then the most recent shadow copy, etc. In the end we get a timeline
from the present extending into the past of unified events which are mostly in
the same order as the events occurred.

Note: As we're producing a timeline going backwards in time, the timestamps
which represent our sequence elements are actually decreasing, so we're actually
interested in the longest non-increasing subsequence. We say non-increasing
rather than decreasing because events are allowed to share the same timestamp.

### Extracting events from `$UsnJrnl`
When a file is created, renamed, moved, or deleted, `$UsnJrnl` will contain
multiple records for the same logical event. The records contain information
such as data being written to the file, security changes, etc. For rename and
move events the old name/parent recordnum and new name/parent recordnum will be
in separate `$UsnJrnl` records. NTFS-Linker will compress all of these records
into one logical event, depending on the event flags. This compression ends when
either the `$UsnJrnl` Record recordnum changes or a CLOSE flag is signalled.
If the flags for a group of records indicate the file was neither created,
deleted, moved, or renamed, then the records are discarded.

### Extracting events from `$LogFile`
`$LogFile` records exist at a lower-level than `$UsnJrnl`, so extracting logical
events is more complicated. A `$LogFile` record consists of a RedoOp and an
UndoOp code each possibly associated with some data.

We break apart the $LogFile records into logical
transactions ending with a record with `RedoOp=FORGET_TRANSACTION` and
`UndoOp=COMPENSATION_LOG_RECORD`. From here, we consider the sequence of OpCodes
and check if it has a subsequence which represents a particular event type. We
list the subsequences associated with each event type below:


| Event Type  | Order | RedoOp Subsequence Entry          | UndoOp Subsequence Entry          |
| ----------- | ----- | --------------------------------- | --------------------------------- |
| Create      | 1     | SET_BITS_IN_NONRESIDENT_BIT_MAP   | CLEAR_BITS_IN_NONRESIDENT_BIT_MAP |
| Create      | 2     | NOOP                              | DEALLOCATE_FILE_RECORD_SEGMENT    |
| Create      | 3     | ADD_INDEX_ENTRY\*                 | DELETE_INDEX_ENTRY\*              |
| Create      | 4     | INITIALIZE_FILE_RECORD_SEGMENT    | NOOP                              |
| Create      | 5     | FORGET TRANSACTION                | COMPENSATION_LOG_RECORD           |
| ----------- | ----- | --------------------------------- | --------------------------------- |
| Delete      | 1     | DELETE_INDEX_ENTRY\*              | ADD_INDEX_ENTRY\*                 |
| Delete      | 2     | DEALLOCATE_FILE_RECORD_SEGMENT    | INITIALIZE_FILE_RECORD_SEGMENT    |
| Delete      | 3     | CLEAR_BITS_IN_NONRESIDENT_BIT_MAP | SET_BITS_IN_NONRESIDENT_BIT_MAP   |
| Delete      | 4     | FORGET_TRANSACTION                | COMPENSATION_LOG_RECORD           |
| ----------- | ----- | --------------------------------- | --------------------------------- |
| Rename/Move | 1     | DELETE_INDEX_ENTRY\*              | ADD_INDEX_ENTRY\*                 |
| Rename/Move | 2     | DELETE_ATTRIBUTE                  | CREATE_ATTRIBUTE                  |
| Rename/Move | 3     | CREATE_ATTRIBUTE                  | DELETR_ATTRIBUTE                  |
| Rename/Move | 4     | ADD_INDEX_ENTRY\*                 | DELETE_INDEX_ENTRY\*              |
| Rename/Move | 5     | FORGET_TRANSACTION                | COMPENSATION_LOG_RECORD           |


For some of the above (RedoOp, UndoOp) pairs there is data associated with the operation
which we add to the candidate event, which we list below:


| RedoOp                          | UndoOp                            | Data Found                                              |
| ------------------------------- | --------------------------------- | ------------------------------------------------------- |
| SET_BITS_IN_NONRESIDENT_BIT_MAP | CLEAR_BITS_IN_NONRESIDENT_BIT_MAP | Recordnum                                               |
| INITIALIZE_FILE_RECORD_SEGMENT  | NOOP                              | Complete MFT entry                                      |
| DELETE_ATTRIBUTE                | CREATE_ATTRIBUTE                  | Previous file name                                      |
| CREATE_ATTRIBUTE                | DELETE_ATTRIBUTE                  | New file name                                           |
| DELETE_INDEX_ENTRY\*            | ADD_INDEX_ENTRY\*                 | File name, parent recordnum                             |
| ADD_INDEX_ENTRY\*               | DELETE_INDEX_ENTRY\*              | Recordnum, Parent recordnum, Timestamp, (new) file name |
| UPDATE_NONRESIDENT_VALUE        | NOOP                              | Embedded `$UsnJrnl` entry                               |


\***Note**: Throughout these tables, we consider ADD_INDEX_ENTRY_ALLOCATION to
be equivalent to ADD_INDEX_ENTRY_ROOT, and denote as ADD_INDEX ENTRY, and
DELETE_INDEX_ENTRY_ALLOCATION equivalent to DELETE_INDEX_ENTRY_ROOT, and 
denote as DELETE_INDEX_ENTRY.

So, the parsing strategy is to collect the above data as each record is
processed, all the while checking if the transaction has ended. If it has, then we
mark a new event if it matches the above event type sequences. Regardless, the
transaction data is cleared.


## Build Notes
The source is in C++ and uses autoconf.