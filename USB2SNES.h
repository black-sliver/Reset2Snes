#ifndef _USB2SNES_H_INCLUDED
#define _USB2SNES_H_INCLUDED

#define ASIO_STANDALONE
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <string>

typedef websocketpp::client<websocketpp::config::asio_client> WSClient;

class USB2SNES {
    public:
        USB2SNES(const std::string& appname);
        ~USB2SNES();
        bool connect(std::vector<std::string> uris = {QUSB2SNES_URI,LEGACY_URI});
        bool disconnect();
        static constexpr auto QUSB2SNES_URI = "ws://localhost:23074";
        static constexpr auto LEGACY_URI = "ws://localhost:8080";
        bool wsConnected();
        bool snesConnected();
        bool reset();
        
    protected:
        struct Version {
            int vmajor;
            int vminor;
            int vrevision;
            std::string extra;
            Version(const std::string& vs) {
                char* next = NULL;
                vmajor = (int)strtol(vs.c_str(), &next, 10);
                if (next && *next) vminor = (int)strtol(next+1, &next, 10);
                if (next && *next) vrevision = (int)strtol(next+1, &next, 10);
                if (next && *next) extra = next+1;
            }
            Version(int ma=0,int mi=0, int rev=0, const std::string& ex="")
                    : vmajor(ma), vminor(mi), vrevision(rev), extra(ex) {}
            void clear() { *this = {}; }
            bool empty() const { 
                return vmajor==0 && vminor==0 && vrevision==0 && extra.empty();
            }
            std::string to_string() const {
                return std::to_string(vmajor) + "." +
                       std::to_string(vminor) + "." + 
                       std::to_string(vrevision) + (extra.empty()?"":"-") +
                       extra;
            }
            int compare(const Version& other) const {
                if (other.vmajor>vmajor) return -1;
                else if (other.vmajor<vmajor) return 1;
                if (other.vminor>vminor) return -1;
                else if (other.vminor<vminor) return 1;
                if (other.vrevision>vrevision) return -1;
                else if (other.vrevision<vrevision) return 1;
                return 0;
            }
            bool operator<(const Version& other) const { return compare(other)<0; }
            bool operator>(const Version& other) const { return compare(other)>0; }
            bool operator>=(const Version& other) const { return !(*this<other); }
            bool operator<=(const Version& other) const { return !(*this>other); }
            bool operator==(const Version& other) const { return compare(other)==0; }
            bool operator!=(const Version& other) const { return !(*this==other); }
        };
        WSClient client;
        WSClient::connection_ptr conn;
        // we have 1 mutex per data access
        // + 1 mutex for checking/requesting socket state (wsmutex)
        // + 1 mutex for the actual work(er/socket) (we can not destroy it while it's busy)
        std::mutex wsmutex; // FIXME: this is getting out of hand
        std::mutex workmutex;
        std::mutex resetmutex;
        std::mutex statemutex;
        
        std::thread worker;
        std::string appname;
        std::string appid;
        bool ws_open = false;
        bool ws_connected = false;
        bool ws_connecting = false;
        bool snes_connected = false;
        enum class Op {
            NONE,
            GET_VERSION,
            SCAN,
            CONNECT,
            RESET,
            PING,
        };
        Op last_op = Op::NONE;
        size_t last_dev = 0;
        std::string rxbuf;
        bool state_changed = true;
        std::map<std::string,bool> features;
        std::string usb2snes_version;
        Version qusb2snes_version;
        std::string backend;
        Version backend_version;
        bool is_qusb2snes_uri = false;
        size_t next_uri = 0;
        bool want_reset = false;
        bool was_reset = false;
};

#endif // _USB2SNES_H_INCLUDED

