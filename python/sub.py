import svar
import time

svar_nsq=svar.load('svar_nsq')

messenger=svar_nsq.MessengerNSQ({"server":"127.0.0.1:4150","pattern":"^@(.+)$"})

def callback_msg(msg):
  print("received by nsq:", msg)

sub= messenger.subscribe('@test',callback_msg)

while True:
  time.sleep(1)
