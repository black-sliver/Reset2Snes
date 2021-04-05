#include "USB2SNES.h"
#include <cstdio>
#include <thread>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <ctime>
#include <vector>
#include "json.hpp"
using json = nlohmann::json;


#define MINIMAL_LOGGING
//#define VERBOSE
//#define TIME_READ


bool USB2SNES::wsConnected()
{
    std::lock_guard<std::mutex> statelock(statemutex);
    return ws_connected;
}
bool USB2SNES::snesConnected()
{
    std::lock_guard<std::mutex> statelock(statemutex);
    return snes_connected;
}

static const json jSCAN = {
    { "Opcode", "DeviceList" },
    { "Space", "SNES" }
};
static const json jGETVERSION = {
    { "Opcode", "AppVersion" },
    { "Space", "SNES" }
};
static const json jINFO = {
    { "Opcode", "Info" },
    { "Space", "SNES" }
};
static const json jRESET = {
    { "Opcode", "Reset" },
    { "Space", "SNES" }
};

USB2SNES::USB2SNES(const std::string& name)
{
    // set name sent to (Q)usb2snes
    appname = name;
    // append a random sequence to it
    std::srand(std::time(nullptr));
    const char idchars[] = "0123456789abcdef";
    for (int i=0; i<4; i++) appid += idchars[std::rand()%strlen(idchars)];
    
#ifdef MINIMAL_LOGGING
    client.clear_access_channels(websocketpp::log::alevel::all);
    client.set_access_channels(websocketpp::log::alevel::none);
#else
	client.set_access_channels(websocketpp::log::alevel::all);
	client.clear_access_channels(websocketpp::log::alevel::frame_payload | websocketpp::log::alevel::frame_header);
#endif
    client.clear_error_channels(websocketpp::log::elevel::all);
    client.set_error_channels(websocketpp::log::elevel::warn|websocketpp::log::elevel::rerror|websocketpp::log::elevel::fatal);
    
    client.init_asio();
    
    client.set_message_handler([this] (websocketpp::connection_hdl hdl, WSClient::message_ptr msg)
    {
        std::unique_lock<std::mutex> lock(workmutex);
        {
            std::lock_guard<std::mutex> wslock(wsmutex);
            if (!ws_open) return; // shutting down
        }
        
        switch (last_op) {
            case Op::GET_VERSION:
            {
                json res = json::parse(msg->get_payload());
                auto results = res.find("Results");
                if (results != res.end() && results->size()>0) {
                    usb2snes_version = results->at(0).get<std::string>();
                    if (is_qusb2snes_uri && usb2snes_version.rfind("QUsb2Snes-",0)==0)
                        qusb2snes_version = usb2snes_version.substr(10);
                    else
                        qusb2snes_version.clear();
                }
                if (!qusb2snes_version.empty())
                    printf("QUsb2Snes version: %s\n", qusb2snes_version.to_string().c_str());
                else
                    printf("Usb2Snes version: %s\n", usb2snes_version.c_str());
                break;
            }
            case Op::SCAN:
            {
                json res = json::parse(msg->get_payload());
                auto results = res.find("Results");
                // HANDLE RESULT
                if (results != res.end()) {
                    if (results->size() > 0) {
                        printf("Got %u scan results: %s\n", (unsigned)results->size(), results->dump().c_str());
                        if (last_dev>=results->size()) last_dev=0;
                        printf("Connecting to %s\n", results->at(last_dev).dump().c_str());
                        json jCONN = {
                            {"Opcode", "Attach"},
                            {"Space", "SNES"},
                            {"Operands", {results->at(last_dev)}}
                        };
                        last_op = Op::CONNECT;
                        client.send(hdl,jCONN.dump(),websocketpp::frame::opcode::text);
                        client.send(hdl,jINFO.dump(),websocketpp::frame::opcode::text);
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        return;
                    }
                }
                break;
            }
            case Op::CONNECT:
            {
                features.clear();
                if (strncmp(msg->get_payload().c_str(),"USBA", 4)==0) {
                    // if we get this, there was probably crap in the receive buffer of qusb2snes.
                    printf("Received invalid response. Ignoring.\n");
                    return; // Actual reply should follow
                }
#ifdef VERBOSE
                printf("Connect result: [%u] %s\n", msg->get_payload().size(),
                                                    msg->get_payload().c_str());
#endif
                try {
                    std::lock_guard<std::mutex> statelock(statemutex);
                    json res = json::parse(msg->get_payload());
                    auto results = res.find("Results");
                    if (results != res.end() && results->size()>0) { // check more
                        snes_connected = true;
                        state_changed = true;
                        if (results->size()>1)
                            backend = results->at(1).get<std::string>();
                        else
                            backend = "SD2SNES";
                        backend_version = Version(results->at(0).get<std::string>());
                        for (auto& res: *results) {
                            if (res.is_string()) {
                                std::string s = res.get<std::string>();
                                if (s.rfind("FEAT_",0)==0 || s.rfind("NO_",0)==0) {
                                    features[s] = true;
                                }
                            }
                        }
                        printf("Connected to %s %s\n", backend.c_str(), backend_version.to_string().c_str());
                    } else {
                        last_dev++; // try next device
                    }
                } catch (...) {
                    last_dev++; // try next device
                }
                break;
            }
            case Op::PING:
            {
                //printf("Ping result\n");
                // ignore message
                break;
            }
            case Op::RESET:
            {
                std::lock_guard<std::mutex> resetlock(resetmutex);
                was_reset = true;
                break;
            }
            default:
                printf("unhandled message: %s\n", msg->get_payload().c_str());
        }
        
        bool tmp_snes_connected;
        {
            std::lock_guard<std::mutex> statelock(statemutex);
            tmp_snes_connected = snes_connected;
        }
        if (!tmp_snes_connected) {
            // limit to 10 times a second
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            // rescan
            last_op = Op::SCAN;
            client.send(hdl,jSCAN.dump(),websocketpp::frame::opcode::text);
        } else {
            for (;;) { // websocket client idle loop
                {
                    std::lock_guard<std::mutex> resetlock(resetmutex);
                    if (want_reset) {
                        was_reset = false;
                        want_reset = false;
                        last_op = Op::RESET;
                        client.send(hdl,jRESET.dump(),websocketpp::frame::opcode::text);
                        // Qusb2Snes does not reply to reset, so instead we send INFO to get back an answer
                        client.send(hdl,jINFO.dump(),websocketpp::frame::opcode::text);
                        return;
                    }
                }
                {
                    std::lock_guard<std::mutex> wslock(wsmutex);
                    if (!ws_open) return; // shutting down
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    });
    
    client.set_open_handler([this] (websocketpp::connection_hdl hdl)
    {
        std::lock_guard<std::mutex> lock(workmutex);
        {
            std::lock_guard<std::mutex> wslock(wsmutex);
            if (!ws_open) return; // shutting down
        }
        std::string uri = client.get_con_from_hdl(hdl)->get_uri()->str();
        is_qusb2snes_uri = (uri.length()>=7 && uri.compare(uri.length()-7, 7, ":23074/")==0);
        {
            std::lock_guard<std::mutex> statelock(statemutex);
            ws_connected = true;
            snes_connected = false;
            state_changed = true;
        }
        printf("* connection to %s opened *\n", uri.c_str());
                
        static const json jNAME = {
            { "Opcode", "Name" },
            { "Space", "SNES" },
            { "Operands", {appname+" "+appid}}
        };
        client.send(hdl,jNAME.dump(),websocketpp::frame::opcode::text);
        last_op = Op::GET_VERSION;
        client.send(hdl,jGETVERSION.dump(),websocketpp::frame::opcode::text);
    });
    
    client.set_fail_handler([this] (websocketpp::connection_hdl hdl)
    {
        std::lock_guard<std::mutex> lock(workmutex);
#ifdef VERBOSE // will generate an error in websocket's logging facility anyway
        printf("* connection to %s failed *\n",
                client.get_con_from_hdl(hdl)->get_uri()->str().c_str());
#endif
        next_uri++;
        {
            std::lock_guard<std::mutex> statelock(statemutex);
            ws_connected = false;
            snes_connected = false;
            state_changed = true;
        }
        last_op = Op::NONE;
    });
    
    client.set_close_handler([this] (websocketpp::connection_hdl hdl)
    {
        std::lock_guard<std::mutex> lock(workmutex);
        std::string uri = client.get_con_from_hdl(hdl)->get_uri()->str();
        next_uri=0;
        features.clear();
        usb2snes_version.clear();
        qusb2snes_version.clear();
        backend.clear();
        backend_version.clear();
        {
            std::lock_guard<std::mutex> statelock(statemutex);
            if (ws_connected || snes_connected) state_changed = true;
            ws_connected = false;
            snes_connected = false;
        }
        last_op = Op::NONE;
        printf("* connection to %s closed *\n", uri.c_str());
    });
}

USB2SNES::~USB2SNES()
{
    disconnect();
    if (worker.joinable()) worker.join();
}
bool USB2SNES::connect(std::vector<std::string> uris)
{
    {
        std::lock_guard<std::mutex> wslock(wsmutex);
        if (ws_open) return false;
        ws_open = true;
    }
    
    worker = std::thread([uris,this]() {
        do {
            {
                std::lock_guard<std::mutex> lock(workmutex);
                {
                    std::lock_guard<std::mutex> wslock(wsmutex);
                    if (!ws_open) return true; // shutting down
                }
                if (next_uri>=uris.size()) next_uri=0;
                auto uri = uris[next_uri];
                websocketpp::lib::error_code ec;
                conn = client.get_connection(uri, ec);
                if (ec) {
                    printf("Could not create connection because: %s\n", ec.message().c_str());
                    {
                        std::lock_guard<std::mutex> wslock(wsmutex);
                        ws_open = false;
                        return false;
                    }
                }
                client.connect(conn);
                {
                    std::lock_guard<std::mutex> wslock(wsmutex);
                    ws_open = true;
                    ws_connecting = true;
                }
            }
            client.run();
            {
                std::lock_guard<std::mutex> lock(workmutex);
                ws_connecting = false;
            }
            client.reset();
            if (next_uri>=uris.size()) { // last in list -> pause
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        } while (true);
    });
    return true;
}
bool USB2SNES::disconnect()
{
    {
        std::lock_guard<std::mutex> wslock(wsmutex);
        if (!ws_open) return true;
        ws_open = false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    {
        std::lock_guard<std::mutex> lock(workmutex);
        std::string res;
        try {
            if (ws_connecting) conn->close(websocketpp::close::status::going_away, res);
        } catch (...) {}
    }
    return true;
}
bool USB2SNES::reset()
{
    if (!snesConnected()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(resetmutex);
        want_reset = true;
    }
    bool res = false;
    for (int i=0; i<100; i++) {
        {
            std::lock_guard<std::mutex> lock(resetmutex);
            res = was_reset;
            if (res) {
                was_reset = false;
                return true;
            }
        }
        if (!snesConnected()) {
            printf("Error: qusb2snes closed the connection\n");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    printf("Timeout resetting!\n");
    return false;
}

