import svar
import time

svar_nsq=svar.load('svar_nsq')

bridge=svar_nsq.BridgeNSQ({"server":"127.0.0.1:4150","pattern":"^@(.+)$"})

def callback_msg(msg):
  print("received by messenger:", msg)

sub= svar_nsq.messenger.subscribe('@test',0,callback_msg)

while True:
  time.sleep(1)
