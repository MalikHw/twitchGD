#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

struct TwitchAuthChangedEvent : public Event {
    bool loggedIn;
    std::string username;   

    std::string token;      

    explicit TwitchAuthChangedEvent(bool loggedIn, std::string username = {}, std::string token = {})
        : loggedIn(loggedIn), username(std::move(username)), token(std::move(token)) {}
};

class TwitchManager {
public:
    static TwitchManager& get();

    void startLogin();
    void logout();

    bool isLoggedIn()          const { return !m_accessToken.empty(); }
    std::string username()     const { return m_username; }
    std::string accessToken()  const { return m_accessToken; }

    void connectToChat(const std::string& channel);
    void disconnectChat();

    using ChatCallback = std::function<void(const std::string& username,
                                            const std::string& message,
                                            const std::string& color)>;
    void setChatCallback(ChatCallback cb) { m_chatCallback = std::move(cb); }

private:
    TwitchManager() = default;

    void startCallbackServer();
    void stopCallbackServer();
    void handleToken(const std::string& token);
    void fetchUserInfo(const std::string& token);

    std::string m_accessToken;
    std::string m_username;

    int         m_serverSock   = -1;
    bool        m_serverRunning = false;

    void ircThreadFunc(std::string channel);
    void sendIRC(const std::string& line);
    std::string readIRCLine();

    int  m_ircSock    = -1;
    bool m_ircRunning = false;
    std::string m_ircChannel;

    ChatCallback m_chatCallback;

    TaskHolder<web::WebResponse> m_webTask;
};
