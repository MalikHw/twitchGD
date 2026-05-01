#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include "TwitchConfigPopup.hpp"
#include "TwitchAuth.hpp"

using namespace geode::prelude;

static const float CHAT_WIDTH      = 135.0f;
static const float CHAT_HEIGHT     = 160.0f;  

static const float MSG_LINE_HEIGHT = 11.0f;
static const float CHAT_PADDING    = 4.0f;
static const float TEXT_SCALE      = 0.27f;
static const int   MAX_MESSAGES    = 14;

static const ccColor3B FALLBACK_COLORS[] = {
    {255, 0,   0},   {0, 255, 0},    {30, 144, 255},
    {178, 70,  255}, {255, 105, 180},{0,  255, 127},
    {255, 165, 0},   {255, 215, 0},  {255, 127, 80},
    {100, 149, 237}, {255, 20,  147},{64, 224, 208},
    {255, 99,  71},  {0,  206, 209}, {154, 205,  50},
};

static ccColor3B parseHexColor(const std::string& hex) {

    std::string h = hex;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    if (h.size() == 6) {
        unsigned r = strtoul(h.substr(0,2).c_str(), nullptr, 16);
        unsigned g = strtoul(h.substr(2,2).c_str(), nullptr, 16);
        unsigned b = strtoul(h.substr(4,2).c_str(), nullptr, 16);
        return {(uint8_t)r, (uint8_t)g, (uint8_t)b};
    }

    size_t hash = 0;
    for (char c : hex) hash = hash * 31 + c;
    return FALLBACK_COLORS[hash % 15];
}

static std::string wrapText(const std::string& msg, int maxChars = 25, int firstLineOffset = 0) {
    std::string result;
    int lineLen = firstLineOffset;
    for (size_t i = 0; i < msg.size(); ) {
        size_t wordEnd = msg.find(' ', i);
        if (wordEnd == std::string::npos) wordEnd = msg.size();
        std::string word = msg.substr(i, wordEnd - i);

        while ((int)word.size() > maxChars) {
            int space = maxChars - lineLen;
            if (space <= 0) {
                if (!result.empty()) result += '\n';
                lineLen = 0;
                continue;
            }
            if (!result.empty() && lineLen > 0) result += '\n';
            result += word.substr(0, space);
            word    = word.substr(space);
            result += '\n';
            lineLen = 0;
        }

        if (lineLen > 0 && lineLen + 1 + (int)word.size() > maxChars) {
            result += '\n'; lineLen = 0;
        }
        if (lineLen > 0) { result += ' '; lineLen++; }
        result  += word;
        lineLen += (int)word.size();
        i = wordEnd;
        while (i < msg.size() && msg[i] == ' ') i++;
    }
    return result;
}

class $modify(MyPlayLayer, PlayLayer) {
    struct Fields {
        CCNode*      m_chatRoot    = nullptr;
        CCLayerColor* m_chatBg    = nullptr;
        CCNode*      m_msgContainer = nullptr;
        std::vector<CCNode*> m_messageRows;
        std::vector<int>     m_rowHeights;

        float holdPercent    = 22.0f;
        float goPercent      = 37.0f;
        float superGoPercent = 80.0f;
        float ggPercent      = 99.9999f;
        bool  enabled        = true;

        EventListener<EventFilter<TwitchAuthChangedEvent>> m_authListener;

        std::string m_font = "bigFont";
    };

public:

    void reloadThresholds() {
        if (!m_level) return;
        auto f   = m_fields.self();
        int  id  = m_level->m_levelID;
        f->holdPercent    = loadPercentForLevel(id, "hold-percent",    22.0f);
        f->goPercent      = loadPercentForLevel(id, "go-percent",      37.0f);
        f->superGoPercent = loadPercentForLevel(id, "supergo-percent", 80.0f);
        f->ggPercent      = loadPercentForLevel(id, "gg-percent",      99.9999f);
        f->enabled        = loadDisabledForLevel(id, "enabled",
                                Mod::get()->getSettingValue<int>("enabled-by-default"));
        f->m_font         = Mod::get()->getSettingValue<std::string>("font") + ".fnt";
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        reloadThresholds();
        auto f = m_fields.self();

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        float chatX  = winSize.width - CHAT_WIDTH - 5.0f + Mod::get()->getSettingValue<int>("x-off");
        float chatY  = 5.0f                               + Mod::get()->getSettingValue<int>("y-off");

        f->m_chatRoot = CCNode::create();
        f->m_chatRoot->setPosition({chatX, chatY});
        f->m_chatRoot->setZOrder(100);
        m_uiLayer->addChild(f->m_chatRoot);

        f->m_chatBg = CCLayerColor::create({14, 14, 18, 200}, CHAT_WIDTH, CHAT_HEIGHT);
        f->m_chatBg->setPosition({0, 0});
        f->m_chatRoot->addChild(f->m_chatBg);

        f->m_msgContainer = CCNode::create();
        f->m_msgContainer->setPosition({0, 0});
        f->m_chatRoot->addChild(f->m_msgContainer);

        auto& mgr = TwitchManager::get();
        mgr.setChatCallback([this](const std::string& user,
                                   const std::string& msg,
                                   const std::string& colorHex) {
            addChatMessage(user, msg, colorHex);
        });

        if (mgr.isLoggedIn()) {
            auto channel = Mod::get()->getSettingValue<std::string>("twitch-channel");
            if (!channel.empty()) mgr.connectToChat(channel);
        }

        f->m_authListener.bind([this](TwitchAuthChangedEvent* ev) {
            if (ev->loggedIn) {
                auto ch = Mod::get()->getSettingValue<std::string>("twitch-channel");
                if (!ch.empty()) TwitchManager::get().connectToChat(ch);
            } else {
                TwitchManager::get().disconnectChat();
            }
            return ListenerResult::Propagate;
        });

        return true;
    }

    float msgAreaHeight() const {
        return CHAT_HEIGHT - CHAT_PADDING * 2;
    }

    void rebuildLayout() {
        auto f    = m_fields.self();
        float y   = CHAT_PADDING;
        float maxY = msgAreaHeight();

        for (int i = 0; i < (int)f->m_messageRows.size(); i++) {
            float rowTop = y + f->m_rowHeights[i] * MSG_LINE_HEIGHT;
            bool visible = (rowTop <= maxY);
            f->m_messageRows[i]->setVisible(visible);
            if (visible) f->m_messageRows[i]->setPositionY(y);
            y += f->m_rowHeights[i] * MSG_LINE_HEIGHT;
        }
    }

    void trimOverflow() {
        auto f = m_fields.self();
        float maxY = msgAreaHeight();
        float total = 0.0f;
        for (int h : f->m_rowHeights) total += h * MSG_LINE_HEIGHT;

        while (total > maxY && !f->m_messageRows.empty()) {
            total -= f->m_rowHeights.back() * MSG_LINE_HEIGHT;
            f->m_msgContainer->removeChild(f->m_messageRows.front(), true);
            f->m_messageRows.erase(f->m_messageRows.begin());
            f->m_rowHeights.erase(f->m_rowHeights.begin());
        }
    }

    void addChatMessage(const std::string& username,
                        const std::string& text,
                        const std::string& colorHex) {
        auto f = m_fields.self();
        if (!f->m_chatRoot || !f->m_chatRoot->isVisible()) return;

        ccColor3B nameColor = parseHexColor(colorHex.empty() ? username : colorHex);

        auto row = CCNode::create();
        row->setPosition({CHAT_PADDING, CHAT_PADDING});

        float cursorX = 0.0f;

        if (!username.empty()) {
            std::string nameStr = username + ": ";
            auto nameLbl = CCLabelBMFont::create(nameStr.c_str(), f->m_font.c_str());
            nameLbl->setScale(TEXT_SCALE);
            nameLbl->setColor(nameColor);
            nameLbl->setAnchorPoint({0.0f, 0.0f});
            nameLbl->setPosition({0.0f, 0.0f});
            row->addChild(nameLbl);
            cursorX = nameLbl->getContentSize().width * TEXT_SCALE;
        }

        std::string wrapped = wrapText(text, 25, (int)(username.size() + 2));
        auto textLbl = CCLabelBMFont::create(wrapped.c_str(), f->m_font.c_str());
        textLbl->setScale(TEXT_SCALE);
        textLbl->setColor({210, 210, 210});
        textLbl->setAnchorPoint({0.0f, 0.0f});
        textLbl->setPosition({cursorX, 0.0f});
        row->addChild(textLbl);

        int lines = 1;
        for (char c : wrapped) if (c == '\n') lines++;

        f->m_msgContainer->addChild(row);
        f->m_messageRows.push_back(row);
        f->m_rowHeights.push_back(lines);

        trimOverflow();
        rebuildLayout();
    }

    void checkProgress(float) {
        auto f = m_fields.self();
        bool inPractice = m_isPracticeMode &&
                          !Mod::get()->getSettingValue<bool>("enabled-in-practice");
        bool visible = !inPractice && !f->enabled;
        f->m_chatRoot->setVisible(visible);
    }

    bool init_schedule(GJGameLevel* lvl, bool r, bool d) {

        return true;
    }
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
    }
};

class $modify(MyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto rightMenu = this->getChildByID("right-button-menu");
        if (!rightMenu) return;
        auto spr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn02_001.png");
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(MyPauseLayer::onChatConfig));
        rightMenu->addChild(btn);
        static_cast<CCMenu*>(rightMenu)->updateLayout();
    }
    void onChatConfig(CCObject*) {
        TwitchConfigPopup::create()->show();
    }
};

class $modify(MyMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto& mgr   = TwitchManager::get();
        auto  token = Mod::get()->getSavedValue<std::string>("twitch-token");
        auto  user  = Mod::get()->getSavedValue<std::string>("twitch-username");
        if (!token.empty() && !user.empty()) {

            Loader::get()->queueInMainThread([token]() {
                TwitchManager::get().handleToken_public(token);
            });
        }

        return true;
    }
};

void reloadPlayLayerThresholds() {
    if (auto pl = PlayLayer::get())
        static_cast<MyPlayLayer*>(pl)->reloadThresholds();
}
