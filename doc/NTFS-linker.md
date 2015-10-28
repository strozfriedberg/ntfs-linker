# NTFS-Linker
Author: Zack Weger

Copyright (c) 2015, [Stroz Friedberg, LLC](http://www.strozfriedberg.com)

Status: Alpha

## Basic usage:

    C:\> ntfs-linker.exe --input .\journals\ --output .\parsed-output\

    C:\> ntfs-linker.com --image MyEvidence.E01 --output .\parsed-output\

## Input

`ntfs-linker` operates off of a directory of input containing $UsnJrnl, 
$Logfile, and $MFT. The $UsnJrnl should be the $J alternate data stream and it 
can be clipped to avoid copying the sparse portions.

`ntfs-linker` can *also* work off of a nested directory structure like the one
below. It can process multiple volumes, and each volume can have multiple
volume shadow copy directories. If a volume has no volume shadow copies, the
intermediate `vss_base` folder can be omitted.

    INPUT
    └── volume_0
        ├── vss_0
        │   ├── $J
        │   ├── $LogFile
        │   └── $MFT
        ├── vss_1
        │   ├── $J
        │   ├── $LogFile
        │   └── $MFT
        ├── vss_2
        │   ├── $J
        │   ├── $LogFile
        │   └── $MFT
        └── vss_base
            ├── $J
            ├── $LogFile
            └── $MFT

When presented with a disk image as input, `ntfs-linker` will automatically run 
against all NTFS volumes and retrieve the respective occurrences of $UsnJrnl,
$Logfile, and $MFT. If a volume contains Volume Shadow Copies, the NTFS files
will be retrieved from each VSC and then the entire collection will be parsed.
The files will be copied out into a directory structure like the one above.

## Output

The output will look like this:

    OUTPUT
    ├── ntfs.db
    └── volume_0
        ├── events.txt
        ├── vss_0
        │   ├── logfile.txt
        │   └── usnjrnl.txt
        ├── vss_1
        │   ├── logfile.txt
        │   └── usnjrnl.txt
        ├── vss_2
        │   ├── logfile.txt
        │   └── usnjrnl.txt
        └── vss_base
            ├── logfile.txt
            └── usnjrnl.txt

`ntfs.db` is a SQLite database which contains data from all volume shadow copies
on all volumes. `events.txt` is a tab-separated report on all of the events from
a particular volume. If `--extra` is specified, then `logfile.txt` and `usnjrnl.txt`
will contain detailed information about the $LogFile and $UsnJrnl for a particular
snapshot.


## Database schema

The SQLite database created by `ntfs-linker` will have the following structure:

```
CREATE TABLE event (
    Position            int, 
    Timestamp           text, 
    EventSource         text, 
    EventType           text, 
    FileName            text, 
    Folder              text, 
    Full_Path           text, 
    MFT_Record          int, 
    Parent_MFT_Record   int, 
    USN_LSN             int, 
    Old_File_Name       text, 
    Old_Folder          text, 
    Old_Parent_ Record  int, 
    Offset              int, 
    Created             text, 
    Modified            text, 
    Comment             text, 
    Snapshot            text, 
    Volume              text
)

CREATE TABLE log (
    CurrentLSN      int, 
    PrevLSN         int, 
    UndoLSN         int, 
    ClientID        int, 
    RecordType      int, 
    RedoOP          text, 
    UndoOP          text, 
    TargetAttribute int, 
    MFTClusterIndex int, 
    Offset          int, 
    Snapshot        text, 
    Volume          text
)

CREATE TABLE usn (
    MFTRecNo        int, 
    ParRecNo        int, 
    USN             int, 
    Timestamp       text, 
    Reason          text, 
    FileName        text, 
    PossiblePath    text, 
    PossibleParPath text, 
    Offset          int, 
    Snapshot        text, 
    Volume          text
)
```

### Useful queries

The following are useful queries.

### Get all events
    SELECT *
    FROM EVENT

#### CCleaner

    SELECT *
    FROM EVENT
    WHERE filename REGEXP "^[\.zZ]+$"

#### Daily histogram

    SELECT substr(event.Timestamp, 0, 11) AS day, count(*) AS count
    FROM event
    GROUP BY day


## Understanding the output

All timestamps are in [ISO 8601 format](https://en.wikipedia.org/wiki/ISO_8601#Combined_date_and_time_representations), 
i.e., YYYY-mm-dd HH:MM:SS.1234567.
Windows stores timestamps as the number of hundred nanoseconds since 1601 
(FILETIME). The routines used by NTFS-Linker to parse the time use standard 
C++ libraries, which may result in incorrect timestamps in some cases.
Specifically, if the time is _before_ 1970 or _after_ 2038, the timestamp will 
not be displayed properly.

### usn.txt
The [USN Journal reason code](https://msdn.microsoft.com/en-us/library/aa365722%28VS.85%29.aspx)
 uses a bit packing scheme for each possible 
reason. From the time a file is opened to the time it is closed, the reasons 
will be combined. This means that multiple reasons may show up for a particular 
entry, even though only one operation happens at a time. The order the reasons 
are printed is completely arbitrary and has no correlation to the order in which 
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
This can be `$UsnJrnl/$J`, `$LogFile`, or "`$UsnJrnl` entry in `$LogFile`". 
`$LogFile` actually contains complete `$UsnJrnl` entries.

#### File names and paths
While a file name can be extracted directly from a Log/Usn entry, the paths must
be calculated. The "folder" path is the calculated path to the parent directory,
and the "Path" column is the calculated path to the MFT record number if 
available.

```gotta come back to this and edit for clarity... maybe an example?```
NTFS-Linker tracks each record's parent over time. For events where a recordnum 
and a parent recordnum can be recovered, it's possible that, in ntfs-linker's
current conception of the file system, these records are unrelated! In this case
the "Folder" and "Full Path" columns of the event will be mismatched. Generally,
the "Folder" will represent the path to the parent folder where the event 
occurred, and "Full Path" will represent the path to the file which NTFS-Linker 
previously thought was at that record. 

#### Event Ordering
While the exact order of `$LogFile` and `$UsnJrnl` events is known, 
respectively, the combined ordering is not. They must be ordered according to 
the event timestamps. While the  timestamps for all `$UsnJrnl` events are known,
the timestamps for `$LogFile` rename, move, and delete events is not known.
Note that, due to file system tunneling, the event time for a `$LogFile` creation
event is not the `$STANDARD_INFORMATION` attribute creation time. It is the SIA
*modified* time.

NTFS-Linker performs a "zipper-merge" of these two event sequences, which preserves
the relative position of events from the same sequence. This is accomplished by
maintaining a cursor at each sequence, and choosing the next event to be the one
with the larger timestamp, when both sequences are non-increasing. Since timestamps
for `$LogFile` rename, delete, and move events are not known, they are always placed
directly after the preceding `$LogFile` creation event.

This can leave some uncertainty surrounding the exact time of an event,
but in practice there are almost always surrounding events which limit the uncertainty.
For example, the `$LogFile` events in the below snippet will almost certainly 
have occurred between 2012-04-07 21:40:26.5203196 and 2012-04-07 21:39:07.3474312.
Since identical events were found in `$UsnJrnl/$J`, we suspect these 3 files
were deleted at 2012-04-07 21:40:26.5203196.



| Timestamp                   | Source      | Event  | File         |
| --------------------------- | ----------- | ------ | ------------ |
| 2012-04-07 21:40:26.5359448 |	$LogFile    | Create | gpt00000.dom |
| 2012-04-07 21:40:26.5359448 |	$UsnJrnl/$J	| Create | gpt00000.dom |
| 2012-04-07 21:40:26.5203196 |	$LogFile    | Create | tmpgptfl.inf |
|                             | $LogFile    | Delete | tmpgptfl.inf |
|                             | $LogFile    | Delete | gpt00000.dom |
|                             | $LogFile"	| Delete | 5.tmp        |
| 2012-04-07 21:40:26.5203196 |	$UsnJrnl/$J	| Create | tmpgptfl.inf |
| 2012-04-07 21:40:26.5203196 |	$UsnJrnl/$J	| Delete | tmpgptfl.inf |
| 2012-04-07 21:40:26.5203196 |	$UsnJrnl/$J | Delete | gpt00000.dom |
| 2012-04-07 21:39:07.3474312 | $LogFile    | Create | 5.ini        |



#### Offset
The actual file offset (in decimal) to the beginning of the event record in the 
source file.

#### Created, Modified, Comment
For `$LogFile` create events, the timestamps from the Standard Information 
attribute (regardless of the faith NTFS-Linker places in them) and whether those 
timestamps match the corresponding timestamps in the File Name Attribute. If 
not, the "Comment" field will say ``. This allows for easy detection of 
timestomping.

#### `$UsnJrnl` event collapsing
`$UsnJrnl` records are combined in events.txt to display one event for each 
logical event that actually occurred. For instance, for a rename event, 
`$UsnJrnl` will contain at least two records: one containing the old name and 
one containing the new name (and probably a couple other records, for the same 
event). In contrast, `events.txt` displays this event just once. For 
the `$UsnJrnl` events embedded in `$LogFile`, this same deduplication is performed,
but only amongst other `$UsnJrnl` events embedded in `$LogFile`. Since these
embedded events are found less often, for rename and move events, it is generally
not possible to retrieve both the file name before and the file name after.

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
| S2  | 25 38 58 29 23 60 47 98 13 61                              | (some elements removed) |
| S3  | 25 38 47 50 72                                             | (some elements removed) |


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
recover an event timestamp. Only for `$LogFile` create events are we able to
recover an event timestamp.

The impact is this: we know the order of events in `$UsnJrnl` and `$LogFile`
separately, and we have some idea of the times these events occurred, but we
don't know exactly how the two sequences fit together. In order to create a 
unified timeline of events, we do a standard zipper merge of the two sequences, 
but only considering the `$Logfile` create event timestamps. Events of other 
types found in `$LogFile` will always be output directly after the preceding
`$LogFile` create event.


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
the same order as the events occurred. _[ed: *mostly???*]_

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
processed, all the while checking if the transaction has ended. If it has, 
then we mark a new event if it matches the above event type sequences. 
Regardless, the transaction data is cleared.

## Further reading

There are a number of good resources online about NTFS, $MFT, $UsnJrnl, 
$Logfile, and Volume Shadow Copies. Among them:
 - [David Cowen](http://www.hecfblog.com/) has blogged in-depth about his 
 research into NTFS linking and offers a [tool](http://www.gettriforce.com) 
 that has features beyond the scope of NTFS-linker.
   - [NTFS Triforce - A deeper look inside the artifacts](http://hackingexposedcomputerforensicsblog.blogspot.com/2013/01/ntfs-triforce-deeper-look-inside.html)
   - [CEIC 2013 and the public beta of the NTFS Triforce](http://hackingexposedcomputerforensicsblog.blogspot.com/2013/05/ceic-2013-and-public-beta-of-ntfs.html)
 - The [Linux-NTFS documentation](http://0cch.net/ntfsdoc/) 
 - [MSDN on the $UsnJrnl](http://www.microsoft.com/msj/0999/journal/journal.aspx)
 - Mike Wilkinson's [NTFS Cheat Sheet](http://www.writeblocked.org/resources/ntfs_cheat_sheets.pdf) 
 is a succinct reference to various NTFS structures.
 - [NTFS Log Tracker](http://forensicinsight.org/wp-content/uploads/2013/06/F-INSIGHT-NTFS-Log-TrackerEnglish.pdf)
 for details on $LogFile transactions