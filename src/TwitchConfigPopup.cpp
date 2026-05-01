#include "TwitchConfigPopup.hpp"

std::string levelKey(int levelID, const char* suffix) {
    return std::to_string(levelID) + suffix;
}

float loadPercentForLevel(int levelID, const char* suffix, float defaultValue) {
    auto key = levelKey(levelID, suffix);
    auto legacy = Mod::get()->getSavedValue<int>(key, static_cast<int>(defaultValue));
    return Mod::get()->getSavedValue<float>(key, static_cast<float>(legacy));
}

bool loadDisabledForLevel(int levelID, const char* suffix, bool defaultValue) {
    return Mod::get()->getSavedValue<bool>(levelKey(levelID, suffix), defaultValue);
}

bool TwitchConfigPopup::setup() {
    this->setTitle("Level Chat Settings");

    auto center = m_mainLayer->getContentSize() / 2;

    float holdPct    = 22.0f;
    float goPct      = 37.0f;
    float superGoPct = 80.0f;
    float ggPct      = 99.9999f;
    bool  enabled    = true;

    if (auto pl = PlayLayer::get(); pl && pl->m_level) {
        int id = pl->m_level->m_levelID;
        holdPct    = loadPercentForLevel(id, "hold-percent",    22.0f);
        goPct      = loadPercentForLevel(id, "go-percent",      37.0f);
        superGoPct = loadPercentForLevel(id, "supergo-percent", 80.0f);
        ggPct      = loadPercentForLevel(id, "gg-percent",      99.9999f);
        enabled    = loadDisabledForLevel(id, "enabled",        true);
    }

    auto makeRow = [&](const char* labelText, float yOff,
                       const std::string& value, bool allowDot,
                       int maxChars) -> geode::TextInput* {

        auto lbl = CCLabelBMFont::create(labelText, "bigFont.fnt");
        lbl->setScale(0.28f);
        lbl->setAnchorPoint({1.0f, 0.5f});
        lbl->setPosition({center.width - 10.f, center.height + yOff});
        m_mainLayer->addChild(lbl);

        std::string filter = allowDot ? "0123456789." : "0123456789";
        auto input = geode::TextInput::create(110.0f, "");
        input->setFilter(filter);
        input->setMaxCharCount(maxChars);
        input->setString(value);
        input->setAnchorPoint({0.0f, 0.5f});
        input->setPosition({center.width + 14.f, center.height + yOff});
        m_mainLayer->addChild(input);
        return input;
    };

    m_holdInput    = makeRow("Hold %",    55.f, std::to_string(static_cast<int>(holdPct)),    false, 3);
    m_goInput      = makeRow("Go %",      18.f, std::to_string(static_cast<int>(goPct)),      false, 3);
    m_superGoInput = makeRow("Super Go %",-18.f, std::to_string(static_cast<int>(superGoPct)), false, 3);
    m_ggInput      = makeRow("GG %",     -55.f, std::to_string(ggPct),                        true,  7);

    auto enableLabel = CCLabelBMFont::create("Enabled", "bigFont.fnt");
    enableLabel->setScale(0.28f);
    enableLabel->setAnchorPoint({1.0f, 0.5f});
    enableLabel->setPosition({center.width - 10.f, center.height - 92.f});
    m_mainLayer->addChild(enableLabel);

    auto toggleMenu = CCMenu::create();
    toggleMenu->setPosition({center.width + 26.f, center.height - 92.f});
    toggleMenu->setContentSize({40.f, 20.f});
    m_enableToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(TwitchConfigPopup::onToggle), 0.55f);
    m_enableToggle->toggle(!enabled);
    toggleMenu->addChild(m_enableToggle);
    m_mainLayer->addChild(toggleMenu);

    return true;
}

void TwitchConfigPopup::onToggle(CCObject*) {}

TwitchConfigPopup* TwitchConfigPopup::create() {
    auto ret = new TwitchConfigPopup();
    if (ret->initAnchored(280.0f, 240.0f)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void TwitchConfigPopup::onClose(CCObject* sender) {
    auto pl = PlayLayer::get();
    if (!pl || !pl->m_level) {
        log::error("TwitchGD: Could not save config – no active level");
        geode::Popup::onClose(sender);
        return;
    }

    int id = pl->m_level->m_levelID;
    auto saveFloat = [&](geode::TextInput* inp, const char* suffix) {
        if (!inp) return;
        Mod::get()->setSavedValue(levelKey(id, suffix),
            geode::utils::numFromString<float>(inp->getString()).unwrapOrDefault());
    };

    saveFloat(m_holdInput,    "hold-percent");
    saveFloat(m_goInput,      "go-percent");
    saveFloat(m_superGoInput, "supergo-percent");
    saveFloat(m_ggInput,      "gg-percent");

    if (m_enableToggle)
        Mod::get()->setSavedValue(levelKey(id, "enabled"), !m_enableToggle->isToggled());

    reloadPlayLayerThresholds();
    geode::Popup::onClose(sender);
}
