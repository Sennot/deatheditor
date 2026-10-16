#pragma once

#include <Geode/Geode.hpp>
#include <nodes/OptionsNode.hpp>
#include <nodes/SimpleToggler.hpp>
#include <nodes/SessionSelector.hpp>
#include <nodes/SwitcherButton.hpp>
#include <editor/EditorManager.hpp>

#include <functional>
#include <optional>

using namespace geode::prelude;

// The pages below only exist in the Level Options popup while "Editor Mode" is enabled.
// All of them are thin forms: parsing the inputs, calling EditorManager::apply with an engine
// operation and showing what the engine did. The rules themselves live in editor/EditorEngine.

class EditorPage : public OptionsNode {
    public:
        void onOpened() override;
        void onClosed() override;

    protected:
        struct Cursor {
            float x;
            float y;
        };

        // refresh every readout/input from the current data
        virtual void refreshPage() = 0;

        // widget helpers: every one places the widget at the cursor and moves the cursor right
        CCLabelBMFont* addLabel(Cursor& c, std::string const& text, float scale = .4f);
        TextInput* addInput(Cursor& c, float width, std::string const& placeholder, CommonFilter filter = CommonFilter::Uint, std::string const& initial = "");
        CCMenuItemSpriteExtra* addButton(Cursor& c, std::string const& text, std::function<void()> onClick, char const* background = "GJ_button_01.png", float scale = .5f);
        SwitcherButton* addSwitcher(Cursor& c, CCSize size, std::vector<std::string> const& options, std::function<void(int)> onChanged = nullptr);
        SessionSelector* addSessionSelector(Cursor& c, std::function<void(int)> onChanged = nullptr);
        CCLabelBMFont* addNote(Cursor& c, std::string const& text, float scale = .5f);

        // the box at the bottom that tells the user what an action did
        void buildResultArea(float height);
        void showMessage(std::string const& text, bool good);
        void showResult(dted::Result const& result, std::string const& successTitle);

        // runs an engine operation on the current level, shows the outcome and refreshes the page
        void runOperation(EditorManager::Operation op, std::string const& successTitle = "Done");

        GJGameLevel* level() const;
        static std::optional<int> readInt(TextInput* input);

        // the session picked in `selector` (1 = newest) as an engine scope; shows an error and returns false if there is none
        bool resolveSession(SessionSelector* selector, long long& dateOut);
        // refreshes a selector's maximum and the label that shows the picked session's date
        void updateSessionPicker(SessionSelector* selector, CCLabelBMFont* dateLabel);

        void setPageActive(bool active);

        float m_gap = 8.f;
        std::vector<TextInput*> m_inputs{};
        std::vector<SessionSelector*> m_selectors{};

    private:
        CCScale9Sprite* m_resultBG = nullptr;
        CCLabelBMFont* m_resultLabel = nullptr;
        int m_resultLines = 3;
};

/* Edit: deaths, runs and new bests, in the lifetime totals or in one session */
class EditorEditPage : public EditorPage {
    public:
        static EditorEditPage* create(const CCSize& size);
    private:
        bool setup() override;
        void refreshPage() override;

        bool resolveScope(dted::Scope& scopeOut);

        SwitcherButton* m_scopeSwitch = nullptr;
        SessionSelector* m_sessionSelector = nullptr;
        CCLabelBMFont* m_dateLabel = nullptr;
        CCLabelBMFont* m_readout = nullptr;

        TextInput* m_deathPercent = nullptr;
        TextInput* m_deathAmount = nullptr;
        TextInput* m_runStart = nullptr;
        TextInput* m_runEnd = nullptr;
        TextInput* m_runAmount = nullptr;
        TextInput* m_bestPercent = nullptr;
};

/* GD: values that live in the real level (attempts, best %, practice best %) */
class EditorGdPage : public EditorPage {
    public:
        static EditorGdPage* create(const CCSize& size);
    private:
        bool setup() override;
        void refreshPage() override;

        TextInput* m_attemptsInput = nullptr;
        TextInput* m_bestInput = nullptr;
        TextInput* m_practiceInput = nullptr;
        SimpleToggler* m_forceToggler = nullptr;
        CCLabelBMFont* m_trackedLabel = nullptr;
        CCLabelBMFont* m_linkLabel = nullptr;
};

/* Sessions: create / move / delete sessions and edit playtime */
class EditorSessionsPage : public EditorPage {
    public:
        static EditorSessionsPage* create(const CCSize& size);
    private:
        bool setup() override;
        void refreshPage() override;

        void updatePlaytimeReadout();
        void onDelete(bool alsoRemoveFromTotals);

        SessionSelector* m_sessionSelector = nullptr;
        CCLabelBMFont* m_dateLabel = nullptr;
        TextInput* m_dateInput = nullptr;

        SwitcherButton* m_ptScope = nullptr;
        SwitcherButton* m_ptKind = nullptr;
        SwitcherButton* m_ptMode = nullptr;
        TextInput* m_ptHours = nullptr;
        TextInput* m_ptMinutes = nullptr;
        TextInput* m_ptSeconds = nullptr;
        CCLabelBMFont* m_ptReadout = nullptr;
};

/* Check: verify, fix, undo and backups */
class EditorCheckPage : public EditorPage {
    public:
        static EditorCheckPage* create(const CCSize& size);
    private:
        bool setup() override;
        void refreshPage() override;

        CCLabelBMFont* m_infoLabel = nullptr;
};
