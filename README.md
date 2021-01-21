# svar_nsq

[NSQ](https://nsq.io/) is a realtime distributed messaging platform.

[Svar](https://github.com/zdzhaoyong/Svar) is a lightweighted single header designed for an unified interface for multiple coding languages and an dynamic_coding_style implementation for modern C++.

[Messenger] is a lightweighted and high efficient pub/sub implementation for intra/inter process message passing. Which is designed to provide easy to use and efficient pub/sub ability.

This plugin provides bridge to seemlessly use NSQ through Messenger.

## Compile and Install

This source code now depend [libevent](https://libevent.org/), which is required by evpp.

```
sudo apt install libevent-dev
```

Compile and install use cmake:

```
cd svar_nsq&& mkdir build
cd build&&cmake .. 
sudo make install
```

## Usage

This plugin can be used across different languages, which is powered by [Svar](https://github.com/zdzhaoyong/Svar).

### Start server

The server binaries for different platforms can be found at: https://nsq.io/deployment/installing.html

You can also found the windows and linux binaries in folder [server_binary](./server_binary).

```
cd server_binary/linux && ./nsqd
```

### Python

Install svar firstly:

```
pip3 install git+https://github.com/zdzhaoyong/Svar.git
```

Subscribe a topic (python3 python/sub_bridge.py):

```
import svar
import time

svar_nsq=svar.load('svar_nsq')

bridge=svar_nsq.BridgeNSQ({"server":"127.0.0.1:4150","pattern":"^@(.+)$"})

def callback_msg(msg):
  print("received by messenger:", msg)

sub= svar_nsq.messenger.subscribe('@test',0,callback_msg)

while True:
  time.sleep(1)

```

Publish messages (python3 python/pub_bridge.py):

```
import svar

svar_nsq=svar.load('svar_nsq')
bridge=svar_nsq.BridgeNSQ({"server":"127.0.0.1:4150","patten":"^@(.+)$"})

pub=svar_nsq.messenger.advertise('@test',0)

pub.publish("publish through messenger bridge")
pub.publish(None)
pub.publish(True)
pub.publish(1.2)
pub.publish({"name":"zhaoyong","age":28})
m=memoryview(b"this is bytes")
pub.publish(m)
```

### C++

Try out:

```
cd build && ./nsq_sample
```

Install svar:

```
git clone https://github.com/zdzhaoyong/Svar
cd Svar && mkdir build
cd build&& cmake ..
sudo make install
```

Use through svar:

```
#include "Registry.h"
#include "Messenger.h"

using namespace sv;

auto nsq = sv::Registry::load("svar_nsq");

int main(int argc,char** argv){
    Svar config;
    config.parseMain(argc,argv);

    std::string topic = config.arg<std::string>("topic","@test","the topic for pubsub");
    int sleepTime     = config.arg("sleep",10,"the time to sleep");

    if(config.get("help",false)) return config.help();

    auto bridge=nsq["BridgeNSQ"](config);

    messenger=nsq["messenger"].castAs<GSLAM::Messenger>();

    auto subscriber=messenger.subscribe(topic,0,[](sv::Svar msg){
        std::cerr<<"received by native messenger:"<<msg<<std::endl;
    });

    auto publisher=messenger.advertise(topic,0);
    publisher.publish(nullptr);
    publisher.publish(true);
    publisher.publish(1);
    publisher.publish(3.14);
    publisher.publish("hello world");
    publisher.publish(sv::Svar({1,2,"hello"}));

    if(config.Get<std::string>("serializer")=="cbor"){
        auto subObject=messenger.subscribe("@buffer",0,[](sv::SvarBuffer buf){
            std::cerr<<"Received buffer "<<sv::Svar(buf)<<std::endl;
        });

        auto publisher=messenger.advertise("@buffer",0);
        publisher.publish(sv::SvarBuffer(1024));
        sleep(sleepTime);
    }

    sleep(sleepTime);
    return 0;
}

```

## Important parameters

Parameters can be set through the interface when construct the MessengerNSQ or BridgeNSQ object.

### Server (server)

svar_nsq use the tcp protocal and default use the local server: "127.0.0.1:4150"

### Topic Regex (pattern)

For better intra-process messaging efficiency, only topics matches a typical pattern are transfered.
The default regex is "^@(.+)$", which transfer topic "@topic_name" to "topic_name" and please add '@' in your topic name if you want to publish your messages to other process.

### Serializers (serializer)

Now two serializers are implemented:

- json: Only support json types and buffer are not supported
- cbor: Default option, support buffer and even user defined types


