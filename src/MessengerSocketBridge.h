#include "Messenger.h"
#include <evpp/libevent.h>
#include <evpp/event_watcher.h>
#include <evpp/event_loop.h>
#include <evpp/event_loop_thread.h>
#include <evpp/tcp_server.h>
#include <evpp/buffer.h>
#include <evpp/tcp_conn.h>
#include <evpp/tcp_client.h>
#include <evpp/udp/udp_server.h>
#include <evpp/udp/udp_message.h>
#include <evpp/udp/sync_udp_client.h>

/**
 * Workflow:
 * 1. Create socket server of this node
 * 2. Tell everyone I am now online
 * 3. On connection: Add node meta to NetNode, send information about this node
 * 4. On :Other nodes send information of node graph
 */

namespace GSLAM {

class MessengerSocketBridge
{
public:
    enum MessageType{
        MSG_GRAPH,
        MSG_NODE,
        MSG_JSON,
        MSG_BUFFER,
        MSG_NEWPUB,
        MSG_NEWSUB
    };

    class SvarSocketWrapper: public SvarValue// The wrapper is used to distinguish where is the message come from
    {
    public:
        SvarSocketWrapper(GSLAM::Svar var):_var(var){}

        virtual TypeID          cpptype()const{return _var.cpptype();}
        virtual const void*     ptr() const{return _var.value()->ptr();}
        virtual const Svar&     classObject()const{return _var.value()->classObject();}
        virtual size_t          length() const {return _var.value()->length();}
        virtual std::mutex*     accessMutex()const{return _var.value()->accessMutex();}

        static Svar wrap(GSLAM::Svar var){
            return Svar((SvarValue*)new SvarSocketWrapper(var));
        }

        GSLAM::Svar _var;
    };

    MessengerSocketBridge(){
        Subscriber subNewPub=Messenger::instance().subscribe("messenger.newpub",0,
                                                             &MessengerSocketBridge::onNewPublisher,this);
        subs["messenger.newpub"]=subNewPub;
    }

    ~MessengerSocketBridge(){
        if(server)
            server->Stop();
        client->Disconnect();
    }

    void start(){
        loop.Start(true);
        initServer();
    }

    /// Create server of this node
    void initServer(){
        std::string serverAddr=svar.GetString("server","127.0.0.1:5000");

        if(!svar.get<bool>("client",false)){
            server=std::shared_ptr<evpp::TCPServer>(new evpp::TCPServer(loop.loop(),serverAddr,"Server",0));
            server->SetConnectionCallback([this](const evpp::TCPConnPtr& conn){onNewConnectionServer(conn);});
            server->SetMessageCallback([this](const evpp::TCPConnPtr& conn, evpp::Buffer* buf){onNewMessageServer(conn,buf);});
            if(!server->Init()) ;
            if(!server->Start()) ;
        }

        client=std::shared_ptr<evpp::TCPClient> (new evpp::TCPClient(loop.loop(), serverAddr, "Client"));
        client->SetConnectionCallback([this](const evpp::TCPConnPtr& conn) {onNewConnectionClient(conn);});
        client->SetMessageCallback([this](const evpp::TCPConnPtr& conn, evpp::Buffer* buf){onNewMessageClient(conn,buf);});
        client->set_auto_reconnect(true);
        client->Connect();
    }

    /// Another node is tring to access me
    void onNewConnectionServer(const evpp::TCPConnPtr& conn){
        if(conn->IsDisconnected()){
            LOG(INFO)<<conn->remote_addr()<<" is Down.";
            serverConns.erase(conn->remote_addr());
            graph.erase(conn->remote_addr());
            for(std::pair<std::string,evpp::TCPConnPtr> con:serverConns)
            {
                if(con.second->remote_addr()==clientConn->remote_addr())
                    continue;// skip self node

                send(con.second,{int(MSG_GRAPH),graph});
            }
        }
        else{
            LOG(INFO)<<conn->remote_addr()<<" is Up.";
            serverConns[conn->remote_addr()]=conn;
        }
    }


    /// Received message from others
    void onNewMessageServer(const evpp::TCPConnPtr& conn, evpp::Buffer* buf)
    {
        Svar var=parse(buf);
        if(var.isUndefined()) return;
//        LOG(INFO)<<"Server received "<<var;
        int  msgType=var[0].as<int>();
        switch (msgType) {
        case MSG_NODE:// Update the graph and tell everyone
        {
            graph[conn->remote_addr()]=var[1];
            for(std::pair<std::string,evpp::TCPConnPtr> con:serverConns)
            {
                if(clientConn->AddrToString().find(con.second->remote_addr())!=std::string::npos)
                    continue;// skip self node

                send(con.second,{int(MSG_GRAPH),graph});
            }
        }
            break;
        case MSG_JSON:// Received a message, publish to everyone except source and server node
        {
            std::string topic=var[1].as<std::string>();
            std::map<std::string,Svar> map=graph.castAs<std::map<std::string,Svar>>();
            for(std::pair<std::string,Svar> it:map){
                if(conn->AddrToString().find(it.first)!=std::string::npos)
                    continue;// skip source node
//                if(it.second["subs"][topic].isUndefined()) continue;// only to nodes contains this topic
                evpp::TCPConnPtr con=serverConns[it.first];
                if(!con){
                    LOG(INFO)<<"Can't find connection "<<it.first;
                    continue;
                }
//                con->Send(buf);// transform to everyone, FIXME: can this buf be used again?
                send(con,var);
                LOG(INFO)<<"Sent to "<<it.first;
            }
        }
            break;
        default:
            break;
        }
    }

    /// Another node is tring to access me
    void onNewConnectionClient(const evpp::TCPConnPtr& conn){
        if(conn->IsConnected())
        {
            clientConn=conn;
            send(conn,{int(MSG_NODE),getNodeInfo()});
        }
        else clientConn.reset();
    }


    /// Received message from others
    void onNewMessageClient(const evpp::TCPConnPtr& conn, evpp::Buffer* buf)
    {
        Svar var=parse(buf);
        if(var.isUndefined()) return;

//        LOG(INFO)<<"client received"<<var;
        int  msgType=var[0].as<int>();
        switch (msgType) {
        case MSG_GRAPH:
        {
            graph=var[1];
            LOG(INFO)<<"graph is "<<graph;
        }
            break;
        case MSG_JSON:
            Messenger::instance().publish(var[1],SvarSocketWrapper::wrap(var[2]));
            break;
        default:
            break;
        }
    }

    void onNewPublisher(const Publisher& pub){
        LOG(INFO)<<"New pub "<<pub.getTopic();
        // Check if transfer needed

        // Do transfer, How to distinguish whether the message if from network or local,
        // should not tranfer the message back server
        Subscriber sub=Messenger::instance().subscribe(pub.getTopic(),0,[this,pub](Svar var)->void{
            if(std::dynamic_pointer_cast<SvarSocketWrapper>(var.value())) return;
            if(serverConns.empty())
            {
                LOG(INFO)<<"Sending "<<var<<" to server";
                send(clientConn,{int(MSG_JSON),pub.getTopic(),var});
            }
            else // this is the master node, directly send to clients
            {
                std::string topic=pub.getTopic();
                std::map<std::string,Svar> map=graph.castAs<std::map<std::string,Svar>>();
                for(std::pair<std::string,Svar> it:map){
                    if(clientConn->AddrToString().find(it.first)!=std::string::npos)
                        continue;// skip this node
                    //                if(it.second["subs"][topic].isUndefined()) continue;// only to nodes contains this topic
                    evpp::TCPConnPtr con=serverConns[it.first];
                    if(!con){
                        LOG(INFO)<<"Can't find connection "<<it.first;
                        continue;
                    }
                    send(con,{int(MSG_JSON),pub.getTopic(),var});
                    LOG(INFO)<<"Sending "<<var<<" to "<<it.first;
                }
            }

        });
        subs.insert(std::pair<std::string,Subscriber>(pub.getTopic(),sub));
    }

    void onNewSubscriber(const Subscriber& sub){

    }

    Svar getNodeInfo(){
        Svar pubs=Svar::object();
        for(Publisher pub:Messenger::instance().getPublishers())
        {
            pubs[pub.getTopic()]=pub.getTypeName();
        }
        Svar subs=Svar::object();
        for(Subscriber sub:Messenger::instance().getSubscribers())
        {
            subs[sub.getTopic()]=sub.getTypeName();
        }
        Svar var;
        var["subs"]=subs;
        var["pubs"]=pubs;
        if(svar.exist("argv"))
            var["name"]=std::string(svar["argv"].castAs<char**>()[0]);
        LOG(INFO)<<"NodeInfo:"<<var;
        return var;
    }

    void send(const evpp::TCPConnPtr& conn,const Svar& v){
        std::stringstream sst;
        sst<<v;
        std::string str=sst.str();
        evpp::Buffer buf;
        buf.AppendInt32(str.size());
        buf.Append(str.data(),str.size());
        conn->Send(&buf);
    }

    static Svar parse(evpp::Buffer* buf){
        int length=buf->PeekInt32();
        if(length+4>buf->size()) {
            LOG(INFO)<<"Should wait.";
            return Svar();// should wait
        }
        buf->Skip(sizeof(length));
        std::stringstream sst(buf->NextString(length));

//        LOG(INFO)<<"Parsing "<<sst.str();
        GSLAM::Svar v;
        try{
            sst>>v;
        }
        catch(std::exception& e){
            return Svar();
        }
        return v;
    }

    evpp::EventLoopThread            loop;

    // server data
    std::shared_ptr<evpp::TCPServer> server;
    std::map<std::string,evpp::TCPConnPtr> serverConns;

    // client data
    std::shared_ptr<evpp::TCPClient> client;
    evpp::TCPConnPtr                 clientConn;

    // node data
    GSLAM::Svar                      graph;
    std::map<std::string,GSLAM::Subscriber> subs;// topic, fake subs aim to publish to other nodes
};

}
