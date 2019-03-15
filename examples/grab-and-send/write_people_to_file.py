import time
import signal
import sys
import subprocess
from io import StringIO

def termination_handler(sig, frame):
   try:   
      print("Killing child process " + str(sp))
      sp.kill()
   except Exception as e:
      print("Error closing subprocess, Exception was:\n {0}".format(e))
   print("Exiting!")
   sys.exit(0)


signal.signal(signal.SIGINT, termination_handler)
#signal.signal(signal.SIGTERM, termination_handler)
try:
   subprocess.call(["rm", "people.txt"]);
except:
   pass
while 1:
   try:
         sp = subprocess.Popen(["howmanypeoplearearound", "-a", "wlan0", "-o","OutputFile","--allmacaddresses"], stdout=subprocess.PIPE)
         out, err = sp.communicate()
         peopleout = out.decode('ascii')         
         print('Got output:\n' + peopleout)
         numpeople = int(peopleout[peopleout.find('\n')+1:])
   except Exception as e:
      numpeople = -1
      print("Got exception while running howmanypeoplearearound:\n {0}".format(e))
   with open('people.txt', 'w') as writer:
      outstr = str(int(time.time())) + ' ' + str(numpeople) + '\n'      
      writer.write(outstr)
      print('Wrote entry: ' + outstr)
