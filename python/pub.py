import svar
import time

svar_nsq=svar.load('svar_nsq')

messenger=svar_nsq.MessengerNSQ({"server":"127.0.0.1:4150","pattern":"^@(.+)$"})

for i in range(0,10):
  messenger.publish('@test',"publish from nsq")
  time.sleep(1)