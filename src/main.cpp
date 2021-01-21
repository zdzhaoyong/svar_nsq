#include "Messenger.h"
#include "Registry.h"

#include "evnsq/consumer.h"
#include "evnsq/producer.h"
#include "evpp/event_loop.h"

#include <regex>

using namespace sv;
using namespace GSLAM;

class MessengerNSQ{
public:
    MessengerNSQ(sv::Svar config)
        : client(&loop,evnsq::Option())
    {
        worker=std::thread([this](){loop.Run();});
        server=config.arg<std::string>("server","127.0.0.1:4150","the nsqd tcp server address");
        matcher=config.arg<std::string>("pattern","^@(.+)$","The regex pattern for matching topic name");
        std::string serializer_name=config.arg<std::string>("serializer","cbor","The serializer typename: json, cbor");

        serializer=svar["serializers"][serializer_name];

        if(serializer.isUndefined()){
            LOG(FATAL) <<"Unkown serializer "<<serializer_name;
        }

        try {
            client.ConnectToNSQDs(server);

            while (!client.IsReady()) {
               usleep(1);
            }
        } catch (std::exception& e) {
            LOG(FATAL)<<e.what();
        }
    }

    ~MessengerNSQ(){
        loop.Stop();
        worker.join();
    }

    void publish(std::string topic, sv::Svar msg){
        sv::SvarBuffer buf=serializer.call("dump",msg).castAs<sv::SvarBuffer>();
        client.Publish(filter_name(topic),std::string(buf.ptr(),buf.size()));
    }

    sv::Svar subscribe(std::string topic, const sv::SvarFunction& callback){
        auto ret= std::make_shared<evnsq::Consumer>(&loop,filter_name(topic),"messenger",options);
        sv::Svar cbk=callback;
        ret->SetMessageCallback([cbk,this](const evnsq::Message* msg){
            Svar msg1=serializer.call("load",sv::SvarBuffer(msg->body.data(),msg->body.size()));
            cbk(msg1);
            return 0;
        });
        try {
            ret->ConnectToNSQDs(server);
        } catch (std::exception& e) {
            LOG(FATAL)<<e.what();
        }
        return ret;
    }

    std::string filter_name(std::string topic,bool silent=false){
        std::smatch result;
        if(!std::regex_match(topic,result,matcher))
        {
            CHECK(silent)<<"Topic "<<topic<<" not match regex " << config.get<std::string>("patten","^@(.+)$");
            return "";
        }

        return result[1];
    }

    evpp::EventLoop loop;
    evnsq::Option   options;
    sv::Svar        config;
    evnsq::Producer client;
    std::string     server;

    std::thread     worker;
    std::regex      matcher;
    sv::Svar        serializer;
};

class BridgeNSQ{
public:
    BridgeNSQ(sv::Svar config)
        : nsq(config),subs(Svar::object()){
        std::string patten=config.get<std::string>("patten","@");

        data["newpub"]=GSLAM::Messenger::instance().subscribe("messenger/newpub",0,[this](GSLAM::Publisher pub){
            std::string nsq_topic=nsq.filter_name(pub.getTopic(),true);
            if(nsq_topic.empty()) return;
            reassignPublisher(pub,pub.getTopic());
        });

        data["newsub"]=GSLAM::Messenger::instance().subscribe("messenger/newsub",0,[this,patten](GSLAM::Subscriber sub){
            std::string nsq_topic=nsq.filter_name(sub.getTopic(),true);
            if(nsq_topic.empty()) return;
            reassignSubscriber(sub.getTopic(),nsq_topic);
        });
    }

    void reassignPublisher(GSLAM::Publisher pub, std::string topic){
        pub.impl_->pubFunc=[this,topic](const sv::Svar& msg){
            nsq.publish(topic,msg);
        };
    }

    void reassignSubscriber(std::string msg_topic, std::string nsq_topic){
        if(subs.exist(msg_topic)) return;
        sv::Svar subnsq=nsq.subscribe(msg_topic,[msg_topic](const sv::Svar& msg){
            Messenger::instance().publish(msg_topic,msg);
        });
        subs[msg_topic]=subnsq;
    }

    MessengerNSQ  nsq;
    sv::Svar      subs,data;
};

REGISTER_SVAR_MODULE(nsq){
    Class<MessengerNSQ>("MessengerNSQ")
            .unique_construct<Svar>()
            .def("publish",&MessengerNSQ::publish)
            .def("subscribe",&MessengerNSQ::subscribe);

    Class<BridgeNSQ>("BridgeNSQ")
            .unique_construct<Svar>();

    Class<Messenger>("Messenger")
            .construct<>()
            .def_static("instance",&Messenger::instance)
            .def("getPublishers",&Messenger::getPublishers)
            .def("getSubscribers",&Messenger::getSubscribers)
            .def("introduction",&Messenger::introduction)
            .def("advertise",[](Messenger msg,const std::string& topic,int queue_size){
        return msg.advertise<sv::Svar>(topic,queue_size);
    })
    .def("subscribe",[](Messenger msger,
         const std::string& topic, int queue_size,
         const SvarFunction& callback){
        return msger.subscribe(topic,queue_size,callback);
    })
    .def("publish",[](Messenger* msger,std::string topic,sv::Svar msg){return msger->publish(topic,msg);});

    Class<Publisher>("Publisher")
            .def("shutdown",&Publisher::shutdown)
            .def("getTopic",&Publisher::getTopic)
            .def("getTypeName",&Publisher::getTypeName)
            .def("getNumSubscribers",&Publisher::getNumSubscribers)
            .def("publish",[](Publisher* pubptr,sv::Svar msg){return pubptr->publish(msg);});

    Class<Subscriber>("Subscriber")
            .def("shutdown",&Subscriber::shutdown)
            .def("getTopic",&Subscriber::getTopic)
            .def("getTypeName",&Subscriber::getTypeName)
            .def("getNumPublishers",&Subscriber::getNumPublishers);

    auto global=sv::Registry::load("svar_messenger");
    auto msg=global["messenger"];

    if(!global["logger"].isUndefined())
    {
        GSLAM::getLogSinksGlobal()=global["logger"].as<std::shared_ptr<std::set<GSLAM::LogSink *> >>();
    }
    else
        global["logger"]=GSLAM::getLogSinksGlobal();

    if(msg.is<GSLAM::Messenger>()){
        GSLAM::Messenger::instance()=msg.as<GSLAM::Messenger>();
    }
    svar["messenger"]=Messenger::instance();
}

EXPORT_SVAR_INSTANCE

int run(sv::Svar config){
    BridgeNSQ bridge(config);

    MessengerNSQ& nsq=bridge.nsq;

    std::string topic=config.arg<std::string>("topic","@test","the topic for pubsub");
    int sleepTime=config.arg("sleep",10,"the time to sleep");

    if(config.get("help",false)) return config.help();

    auto subnsq=bridge.nsq.subscribe(topic,[](sv::Svar msg){
            std::cerr<<"received by nsq:"<<msg<<std::endl;
    });

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

    bridge.nsq.publish(topic,"publish from nsq");

    if(config.Get<std::string>("serializer")=="cbor"){
        auto subObject=messenger.subscribe("@buffer",0,[](sv::SvarBuffer buf){
            LOG(INFO)<<"Received buffer "<<sv::Svar(buf);
        });

        auto publisher=messenger.advertise("@buffer",0);
        publisher.publish(sv::SvarBuffer(1024));
        sleep(sleepTime);
    }

    sleep(sleepTime);
    return 0;
}

int main(int argc,char** argv){
    svar.parseMain(argc,argv);

    return run(svar);
}
