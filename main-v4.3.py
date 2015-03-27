import os.path
import sys
import subprocess

def main(args):
	#print args
	#return
	aliases = ['$J', '$USNJR~1']
	f = None
	dir = ''
	try:
		dir = args[args.index('-u') + 1]
	except ValueError:
		pass
	try:
		dir = args[args.index('-i') + 1]
	except ValueError:
		pass
	
	for name in aliases:
		path = os.path.join(dir, name)
		if os.path.isfile(path):
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
		
		subprocess.call(['ntfs-linker-v4.3.exe', '--python'] + args[1:], stdout=sys.stdout)
	else:
		print 'USN Journal file not found at', dir
if __name__ == '__main__':
	main(sys.argv)