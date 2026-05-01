#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

using namespace geode::prelude;

std::string levelKey(int levelID, const char* suffix);
float loadPercentForLevel(int levelID, const char* suffix, float defaultValue);
bool loadDisabledForLevel(int levelID, const char* suffix, bool defaultValue);
void reloadPlayLayerThresholds();

class TwitchConfigPopup : public geode::Popup<> {
protected:
    geode::TextInput* m_holdInput    = nullptr;
    geode::TextInput* m_goInput      = nullptr;
    geode::TextInput* m_superGoInput = nullptr;
    geode::TextInput* m_ggInput      = nullptr;
    CCMenuItemToggler* m_enableToggle = nullptr;

    bool setup() override;
    void onToggle(CCObject*);

public:
    static TwitchConfigPopup* create();
    void onClose(CCObject* sender) override;
};
