import svar
import time

svar_nsq=svar.load('svar_nsq')
bridge=svar_nsq.BridgeNSQ({"server":"127.0.0.1:4150","pattern":"^@(.+)$"})

pub=svar_nsq.messenger.advertise('@test',0)

pub.publish("publish through messenger bridge")
pub.publish(None)
pub.publish(True)
pub.publish(1.2)
pub.publish({"name":"zhaoyong","age":28})
m=memoryview(b"this is bytes")
print(m)
pub.publish(m)