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
