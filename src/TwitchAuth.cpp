#include "TwitchAuth.hpp"

#ifdef GEODE_IS_WINDOWS
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using SockT = SOCKET;
    #define INVALID_SOCK INVALID_SOCKET
    #define SOCK_ERR     SOCKET_ERROR
    #define CLOSE_SOCK   closesocket
    static void initSockets() {
        WSADATA w; WSAStartup(MAKEWORD(2,2), &w);
    }
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    using SockT = int;
    #define INVALID_SOCK (-1)
    #define SOCK_ERR     (-1)
    #define CLOSE_SOCK   ::close
    static void initSockets() {}
#endif

#include <thread>
#include <atomic>
#include <sstream>

using namespace geode::prelude;

static constexpr int CALLBACK_PORT = 42069;
static constexpr const char* REDIRECT_URI = "http://localhost:42069/callback";
static constexpr const char* IRC_HOST     = "irc.chat.twitch.tv";
static constexpr int         IRC_PORT     = 6667;

static std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ) {
        if (s[i] == '%' && i + 2 < s.size()) {
            char hex[3] = { s[i+1], s[i+2], 0 };
            out += static_cast<char>(strtol(hex, nullptr, 16));
            i += 3;
        } else if (s[i] == '+') {
            out += ' '; ++i;
        } else {
            out += s[i++];
        }
    }
    return out;
}

static std::string queryParam(const std::string& query, const std::string& key) {

    size_t pos = 0;
    while (pos < query.size()) {
        size_t eq  = query.find('=', pos);
        if (eq == std::string::npos) break;
        size_t amp = query.find('&', eq);
        if (amp == std::string::npos) amp = query.size();
        std::string k = query.substr(pos, eq - pos);
        std::string v = query.substr(eq + 1, amp - eq - 1);
        if (k == key) return urlDecode(v);
        pos = amp + 1;
    }
    return {};
}

TwitchManager& TwitchManager::get() {
    static TwitchManager inst;
    return inst;
}

void TwitchManager::startLogin() {
    auto clientId = Mod::get()->getSettingValue<std::string>("twitch-client-id");
    if (clientId.empty()) {
        FLAlertLayer::create("Error",
            "Please enter your <cb>Twitch Client ID</c> in the mod settings first.", "OK")->show();
        return;
    }

    startCallbackServer();

    std::string authUrl =
        "https://id.twitch.tv/oauth2/authorize"
        "?response_type=token"
        "&client_id=" + clientId +
        "&redirect_uri=" + REDIRECT_URI +
        "&scope=chat%3Aread"
        "&force_verify=true";

#ifdef GEODE_IS_WINDOWS
    ShellExecuteA(nullptr, "open", authUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(GEODE_IS_MACOS) || defined(GEODE_IS_IOS)
    system(("open \"" + authUrl + "\"").c_str());
#else
    system(("xdg-open \"" + authUrl + "\"").c_str());
#endif
}

void TwitchManager::logout() {
    disconnectChat();
    m_accessToken.clear();
    m_username.clear();
    Mod::get()->setSavedValue<std::string>("twitch-token",    "");
    Mod::get()->setSavedValue<std::string>("twitch-username", "");
    TwitchAuthChangedEvent(false).post();
    log::info("TwitchGD: Logged out");
}

static const char* FRAGMENT_RELAY_HTML = R"(<!DOCTYPE html>
<html><head><title>Logging in...</title></head><body>
<script>
  var hash = window.location.hash.substring(1); 

  var params = new URLSearchParams(hash);
  var token = params.get('access_token');
  if (token) {
    window.location.replace('/callback?token=' + encodeURIComponent(token));
  } else {
    document.body.innerText = 'Error: no token found in URL.';
  }
</script>
<p>Completing login, please wait...</p>
</body></html>)";

static const char* SUCCESS_HTML = R"(<!DOCTYPE html>
<html><head><title>Logged in!</title></head><body style="font-family:sans-serif;padding:2em">
<h2 style="color:#9146ff">&#10003; Logged in to TwitchGD!</h2>
<p>You can close this tab and return to the game.</p>
</body></html>)";

static std::string httpResponse(int code, const char* status,
                                const char* contentType, const std::string& body) {
    return "HTTP/1.1 " + std::to_string(code) + " " + status + "\r\n"
           "Content-Type: " + contentType + "\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "Connection: close\r\n\r\n" + body;
}

void TwitchManager::startCallbackServer() {
    if (m_serverRunning) return;
    m_serverRunning = true;

    std::thread([this]() {
        initSockets();

        SockT server = socket(AF_INET, SOCK_STREAM, 0);
        if (server == INVALID_SOCK) { m_serverRunning = false; return; }

        int opt = 1;
#ifdef GEODE_IS_WINDOWS
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port        = htons(CALLBACK_PORT);

        if (bind(server, (sockaddr*)&addr, sizeof(addr)) == SOCK_ERR ||
            listen(server, 5) == SOCK_ERR) {
            CLOSE_SOCK(server);
            m_serverRunning = false;
            return;
        }

        m_serverSock = server;
        log::info("TwitchGD: Callback server listening on :{}", CALLBACK_PORT);

        while (m_serverRunning) {
            SockT client = accept(server, nullptr, nullptr);
            if (client == INVALID_SOCK) break;

            std::string req;
            char buf[1];
            while (true) {
                int n = recv(client, buf, 1, 0);
                if (n <= 0) break;
                req += buf[0];
                if (req.size() >= 4 &&
                    req.substr(req.size()-4) == "\r\n\r\n") break;
                if (req.size() > 8192) break;
            }

            std::string path;
            {
                auto sp1 = req.find(' ');
                auto sp2 = req.find(' ', sp1+1);
                if (sp1 != std::string::npos && sp2 != std::string::npos)
                    path = req.substr(sp1+1, sp2 - sp1 - 1);
            }

            std::string query;
            auto qpos = path.find('?');
            if (qpos != std::string::npos) {
                query = path.substr(qpos+1);
                path  = path.substr(0, qpos);
            }

            std::string response;
            if (path == "/callback" && !query.empty()) {
                std::string token = queryParam(query, "token");
                if (!token.empty()) {
                    response = httpResponse(200, "OK", "text/html; charset=utf-8", SUCCESS_HTML);
                    std::string resp = response;
                    send(client, resp.c_str(), (int)resp.size(), 0);
                    CLOSE_SOCK(client);

                    Loader::get()->queueInMainThread([this, token]() {
                        handleToken(token);
                    });
                    break; 

                }
            }

            response = httpResponse(200, "OK", "text/html; charset=utf-8", FRAGMENT_RELAY_HTML);
            send(client, response.c_str(), (int)response.size(), 0);
            CLOSE_SOCK(client);
        }

        CLOSE_SOCK(server);
        m_serverSock    = -1;
        m_serverRunning = false;
    }).detach();
}

void TwitchManager::stopCallbackServer() {
    m_serverRunning = false;
    if (m_serverSock != -1) {
        CLOSE_SOCK(m_serverSock);
        m_serverSock = -1;
    }
}

void TwitchManager::handleToken(const std::string& token) {
    m_accessToken = token;
    Mod::get()->setSavedValue<std::string>("twitch-token", token);
    log::info("TwitchGD: Got access token, fetching user info...");
    fetchUserInfo(token);
}

void TwitchManager::fetchUserInfo(const std::string& token) {
    auto clientId = Mod::get()->getSettingValue<std::string>("twitch-client-id");
    auto req = web::WebRequest();
    req.header("Authorization", "Bearer " + token);
    req.header("Client-Id",     clientId);
    m_webTask.spawn(
        req.get("https://api.twitch.tv/helix/users"),
        [this](web::WebResponse res) {
            if (!res.ok()) {
                log::error("TwitchGD: /helix/users failed: {}", res.code());
                return;
            }
            auto json = res.json().unwrapOr(matjson::Value{});
            auto data = json["data"].asArray().unwrapOr({});
            if (data.empty()) {
                log::error("TwitchGD: empty user data");
                return;
            }
            m_username = data[0]["login"].asString().unwrapOrDefault();
            Mod::get()->setSavedValue<std::string>("twitch-username", m_username);
            log::info("TwitchGD: Logged in as {}", m_username);
            TwitchAuthChangedEvent(true, m_username, m_accessToken).post();
        }
    );
}

void TwitchManager::connectToChat(const std::string& channel) {
    if (m_accessToken.empty() || m_username.empty()) {
        log::warn("TwitchGD: connectToChat called before login");
        return;
    }
    disconnectChat();
    m_ircChannel  = "#" + channel;
    m_ircRunning  = true;
    std::thread([this, channel]() { ircThreadFunc(channel); }).detach();
}

void TwitchManager::disconnectChat() {
    m_ircRunning = false;
    if (m_ircSock != -1) {
        CLOSE_SOCK(m_ircSock);
        m_ircSock = -1;
    }
}

void TwitchManager::sendIRC(const std::string& line) {
    if (m_ircSock == -1) return;
    std::string msg = line + "\r\n";
    send(m_ircSock, msg.c_str(), (int)msg.size(), 0);
}

std::string TwitchManager::readIRCLine() {
    std::string line;
    char c;
    while (m_ircRunning) {
        int n = recv(m_ircSock, &c, 1, 0);
        if (n <= 0) return {};
        if (c == '\r') continue;
        if (c == '\n') return line;
        line += c;
        if (line.size() > 4096) return line; 

    }
    return {};
}

void TwitchManager::ircThreadFunc(std::string channel) {
    initSockets();

    addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(IRC_HOST, std::to_string(IRC_PORT).c_str(), &hints, &res) != 0) {
        log::error("TwitchGD IRC: DNS lookup failed");
        m_ircRunning = false;
        return;
    }

    SockT sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCK) {
        freeaddrinfo(res);
        m_ircRunning = false;
        return;
    }

    if (connect(sock, res->ai_addr, (int)res->ai_addrlen) == SOCK_ERR) {
        freeaddrinfo(res);
        CLOSE_SOCK(sock);
        m_ircRunning = false;
        return;
    }
    freeaddrinfo(res);
    m_ircSock = sock;

    log::info("TwitchGD IRC: Connected");

    sendIRC("CAP REQ :twitch.tv/tags");        

    sendIRC("PASS oauth:" + m_accessToken);
    sendIRC("NICK " + m_username);
    sendIRC("JOIN #" + channel);

    while (m_ircRunning) {
        std::string line = readIRCLine();
        if (line.empty()) break;

        if (line.rfind("PING", 0) == 0) {
            sendIRC("PONG :tmi.twitch.tv");
            continue;
        }

        std::string tags, prefix, command, params;
        size_t pos = 0;

        if (!line.empty() && line[0] == '@') {
            size_t sp = line.find(' ', 1);
            tags = line.substr(1, sp - 1);
            pos  = sp + 1;
        }

        if (pos < line.size() && line[pos] == ':') {
            size_t sp = line.find(' ', pos+1);
            prefix = line.substr(pos+1, sp - pos - 1);
            pos    = sp + 1;
        }

        {
            size_t sp = line.find(' ', pos);
            command = line.substr(pos, sp - pos);
            pos     = sp + 1;
        }

        params = (pos < line.size()) ? line.substr(pos) : "";

        if (command != "PRIVMSG") continue;

        std::string msgText;
        auto colonPos = params.find(" :");
        if (colonPos != std::string::npos)
            msgText = params.substr(colonPos + 2);

        std::string displayName;
        std::string color;
        std::stringstream ss(tags);
        std::string tag;
        while (std::getline(ss, tag, ';')) {
            if (tag.rfind("display-name=", 0) == 0)
                displayName = tag.substr(13);
            else if (tag.rfind("color=", 0) == 0)
                color = tag.substr(6);
        }
        if (displayName.empty()) {

            auto bang = prefix.find('!');
            displayName = (bang != std::string::npos) ? prefix.substr(0, bang) : prefix;
        }

        if (m_chatCallback) {
            std::string usr = displayName, msg = msgText, clr = color;
            Loader::get()->queueInMainThread([this, usr, msg, clr]() {
                if (m_chatCallback) m_chatCallback(usr, msg, clr);
            });
        }
    }

    CLOSE_SOCK(sock);
    m_ircSock    = -1;
    m_ircRunning = false;
    log::info("TwitchGD IRC: Disconnected");
}
