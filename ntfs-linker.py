import os
import sys
import subprocess
import argparse

def checkFolder(path):
    if os.path.isdir(path):
        return True
    else:
        print "Folder was not found at the location specified: %s" % path
        return False

def checkCreateFolder(path):
    if os.path.exists(path):
        return True
    else:
        try:
            os.makedirs(path)
            return True
        except IOError, e:
            print 'Could not to create output directory at: %s' % path
            return False
        except WindowsError, e:
            print 'Could not to create output directory at: %s' % path
            return False

def checkFile(path):
    if os.path.isfile(path):
        return True
    else:
        print "File was not found at the location specified: %s" % path
        return False

def getOptions(argv, desc):
    parser = argparse.ArgumentParser(description=desc)
    parser.add_argument('-i', '--input', required=True,
        help='Path to directory containing $MFT, $LogFile, and $UsnJrnl (or $J)')
    parser.add_argument('-o', '--output', required=True,
        help='Path to output directory')
    parser.add_argument('--append', action='store_true',
        help='Append output instead of overwriting')
    parser.add_argument('-v', '--version', action='store_true',
        help='Print version information and exit')

    args = parser.parse_args()

    if args.version:
        print desc

    if not checkFolder(args.input):
        parser.error('Input folder cannot be accessed (%s)' % args.input)

    if not checkCreateFolder(args.output):
        parser.error('Output folder cannot be accessed (%s)' % args.output)

    return args

def main(argv):

    desc = 'NTFS Linker v4.3'
    args = getOptions(argv, desc)

    aliases = ['$J', '$USNJR~1']
    f = None
    dir = args.input
    for name in aliases:
        path = os.path.join(dir, name)
        if checkFile(path):
            f = open(path, 'rb')
            break
    if f:
        print "Trimming USN Journal file"
        f.seek(0, 2) #seek 0 relative to end
        done = False
        while not done:
            f.seek(-(1 << 20), 1) #seek backwards relative to current
            buffer = f.read(4096)
            done = True
            for x in buffer:
                if x != '\x00':
                    done = False
                    break
        
        path = os.path.join(dir, '$USN')
        out = open(path, 'wb')
        out.write(f.read())
        out.close()
        f.close()
        subargs = ['ntfs-linker-v4.3.exe', '--python', '-i', args.input, '-o', args.output]
        if not args.append:
            subargs.append('--overwrite')
        print 'NTFS Linker running with arguments: %s' % subargs
        subprocess.call(subargs, stdout=sys.stdout)
    else:
        print 'USN Journal file not found at', dir

if __name__ == '__main__':
    main(sys.argv)
