#include <nodes/optionNodes/EditorPages.hpp>

#include <managers/StatsManager.hpp>
#include <nodes/layers/DTLayer.hpp>
#include <utils/Settings.hpp>

#include <algorithm>
#include <chrono>

/* EditorPage
============== */
GJGameLevel* EditorPage::level() const {
    auto dtLayer = DTLayer::get();
    return dtLayer ? dtLayer->m_Level : nullptr;
}

void EditorPage::setPageActive(bool active) {
    this->setVisible(active);
    this->setEnabled(active);
    for (auto input : m_inputs) input->setEnabled(active);
    for (auto selector : m_selectors) selector->setEnabled(active);
}

void EditorPage::onOpened() {
    setPageActive(true);
    refreshPage();
}

void EditorPage::onClosed() {
    setPageActive(false);
}

CCLabelBMFont* EditorPage::addLabel(Cursor& c, std::string const& text, float scale) {
    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(scale);
    label->setAnchorPoint({0, .5f});
    label->setPosition({c.x, c.y});
    this->addChild(label);
    c.x += label->getScaledContentWidth() + m_gap;
    return label;
}

CCLabelBMFont* EditorPage::addNote(Cursor& c, std::string const& text, float scale) {
    auto label = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
    label->setScale(scale);
    label->setAnchorPoint({0, .5f});
    label->setPosition({c.x, c.y});
    this->addChild(label);
    c.x += label->getScaledContentWidth() + m_gap;
    return label;
}

TextInput* EditorPage::addInput(Cursor& c, float width, std::string const& placeholder, CommonFilter filter, std::string const& initial) {
    auto input = TextInput::create(width, placeholder.c_str(), "gjFont17.fnt");
    input->setCommonFilter(filter);
    input->setPosition({c.x + width / 2, c.y});
    if (!initial.empty()) input->setString(initial);
    this->addChild(input);
    m_inputs.push_back(input);
    c.x += width + m_gap;
    return input;
}

CCMenuItemSpriteExtra* EditorPage::addButton(Cursor& c, std::string const& text, std::function<void()> onClick, char const* background, float scale) {
    auto sprite = ButtonSprite::create(text.c_str(), "bigFont.fnt", background);
    sprite->setScale(scale);
    sprite->setCascadeOpacityEnabled(true);

    auto button = CCMenuItemExt::createSpriteExtra(sprite, [callback = std::move(onClick)](CCMenuItemSpriteExtra*) {
        if (callback) callback();
    });
    button->setAnchorPoint({0, .5f});
    button->setPosition({c.x, c.y});
    this->addChild(button);
    c.x += button->getScaledContentWidth() + m_gap;
    return button;
}

SwitcherButton* EditorPage::addSwitcher(Cursor& c, CCSize switcherSize, std::vector<std::string> const& options, std::function<void(int)> onChanged) {
    auto switcher = SwitcherButton::create(switcherSize, "GJ_button_01.png", options);
    switcher->setPosition({c.x + switcher->getScaledContentWidth() / 2, c.y});
    if (onChanged) {
        switcher->setOptionChangedCallback([callback = std::move(onChanged)](int option) {
            callback(option);
        });
    }
    this->addChild(switcher);
    c.x += switcher->getScaledContentWidth() + m_gap;
    return switcher;
}

SessionSelector* EditorPage::addSessionSelector(Cursor& c, std::function<void(int)> onChanged) {
    auto selector = SessionSelector::create(1);
    selector->setScale(.5f);
    selector->ignoreAnchorPointForPosition(false);
    selector->setPosition({c.x + selector->getScaledContentWidth() / 2, c.y});
    if (onChanged) {
        selector->setCallback([callback = std::move(onChanged)](int session) {
            callback(session);
        });
    }
    this->addChild(selector);
    m_selectors.push_back(selector);
    c.x += selector->getScaledContentWidth() + m_gap;
    return selector;
}

void EditorPage::buildResultArea(float height) {
    m_resultLines = std::max(2, static_cast<int>(height / 10.5f));

    m_resultBG = CCScale9Sprite::create("square02_001.png");
    m_resultBG->setContentSize({size.width - 10, height});
    m_resultBG->setAnchorPoint({0, 0});
    m_resultBG->setPosition({5, 3});
    m_resultBG->setOpacity(110);
    this->addChild(m_resultBG);

    m_resultLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_resultLabel->setScale(.55f);
    m_resultLabel->setAnchorPoint({0, 1});
    m_resultLabel->setWidth((size.width - 30) / .55f);
    m_resultLabel->setPosition({12, 3 + height - 5});
    this->addChild(m_resultLabel);
}

void EditorPage::showMessage(std::string const& text, bool good) {
    if (!m_resultLabel) return;
    m_resultLabel->setString(text.c_str());
    m_resultLabel->setColor(good ? ccColor3B{150, 255, 150} : ccColor3B{255, 110, 110});
}

void EditorPage::showResult(dted::Result const& result, std::string const& successTitle) {
    if (!result.ok) {
        showMessage(result.error, false);
        return;
    }

    std::string text = successTitle;
    int shown = 0;
    for (auto const& note : result.notes) {
        if (shown + 1 >= m_resultLines) {
            text += "\n- ...";
            break;
        }
        text += "\n- " + note;
        shown++;
    }
    showMessage(text, true);
}

void EditorPage::runOperation(EditorManager::Operation op, std::string const& successTitle) {
    auto lvl = level();
    if (!lvl) {
        showMessage("No level is loaded", false);
        return;
    }

    auto result = EditorManager::apply(lvl, op);
    showResult(result, successTitle);
    if (result.ok) refreshPage();
}

std::optional<int> EditorPage::readInt(TextInput* input) {
    std::string text = std::string(input->getString());
    if (text.empty()) return std::nullopt;

    auto parsed = geode::utils::numFromString<int>(text);
    if (parsed.isErr()) return std::nullopt;
    return parsed.unwrap();
}

bool EditorPage::resolveSession(SessionSelector* selector, long long& dateOut) {
    auto lvl = level();
    if (!lvl) {
        showMessage("No level is loaded", false);
        return false;
    }

    auto state = EditorManager::load(lvl);
    if (state.isErr()) {
        showMessage(state.unwrapErr(), false);
        return false;
    }

    auto dates = EditorManager::sessionDatesNewestFirst(state.unwrap());
    if (dates.empty()) {
        showMessage("This level has no sessions yet", false);
        return false;
    }

    int index = selector->getCurrentCount();
    if (index < 1 || index > static_cast<int>(dates.size())) {
        showMessage("Pick a session between 1 and " + std::to_string(dates.size()), false);
        return false;
    }

    dateOut = dates[index - 1];
    return true;
}

void EditorPage::updateSessionPicker(SessionSelector* selector, CCLabelBMFont* dateLabel) {
    auto lvl = level();
    if (!lvl) return;

    auto state = EditorManager::load(lvl);
    if (state.isErr()) {
        dateLabel->setString("?");
        return;
    }

    auto dates = EditorManager::sessionDatesNewestFirst(state.unwrap());
    selector->setMaximumCount(std::max(1, static_cast<int>(dates.size())), false);

    int index = selector->getCurrentCount();
    if (dates.empty()) dateLabel->setString("no sessions");
    else if (index < 1 || index > static_cast<int>(dates.size())) dateLabel->setString("?");
    else dateLabel->setString(EditorManager::formatDate(dates[index - 1]).c_str());
}

/* Edit
======== */
EditorEditPage* EditorEditPage::create(const CCSize& size) {
    auto ret = new EditorEditPage();
    if (ret && ret->initWithSetup(size)) {
        ret->autorelease();
    } else {
        delete ret;
        ret = nullptr;
    }
    return ret;
}

bool EditorEditPage::resolveScope(dted::Scope& scopeOut) {
    if (m_scopeSwitch->getCurrentOptionIndex() == 0) {
        scopeOut = std::nullopt;
        return true;
    }

    long long date = 0;
    if (!resolveSession(m_sessionSelector, date)) return false;
    scopeOut = date;
    return true;
}

bool EditorEditPage::setup() {
    const float top = size.height;

    // scope: lifetime totals or one session
    Cursor c{8, top - 20};
    m_scopeSwitch = addSwitcher(c, {70, 20}, {"Lifetime", "Session"}, [this](int) { refreshPage(); });
    m_sessionSelector = addSessionSelector(c, [this](int) { refreshPage(); });
    m_dateLabel = addNote(c, "", .6f);

    // deaths
    c = {8, top - 54};
    addLabel(c, "Death at");
    m_deathPercent = addInput(c, 42, "%");
    addLabel(c, "x");
    m_deathAmount = addInput(c, 52, "n");
    addButton(c, "Add", [this] {
        auto percent = readInt(m_deathPercent);
        auto amount = readInt(m_deathAmount);
        if (!percent || !amount) return showMessage("Enter a percent and an amount", false);
        dted::Scope scope;
        if (!resolveScope(scope)) return;
        runOperation([=](dted::Level& lv, dted::Options const& o) { return dted::addDeaths(lv, o, scope, *percent, *amount); }, "Death added");
    }, "GJ_button_01.png");
    addButton(c, "Remove", [this] {
        auto percent = readInt(m_deathPercent);
        auto amount = readInt(m_deathAmount);
        if (!percent || !amount) return showMessage("Enter a percent and an amount", false);
        dted::Scope scope;
        if (!resolveScope(scope)) return;
        runOperation([=](dted::Level& lv, dted::Options const& o) { return dted::removeDeaths(lv, o, scope, *percent, *amount); }, "Deaths removed");
    }, "GJ_button_06.png");
    addButton(c, "Set", [this] {
        auto percent = readInt(m_deathPercent);
        auto amount = readInt(m_deathAmount);
        if (!percent || !amount) return showMessage("Enter a percent and the wanted count", false);
        dted::Scope scope;
        if (!resolveScope(scope)) return;
        runOperation([=](dted::Level& lv, dted::Options const& o) { return dted::setDeaths(lv, o, scope, *percent, *amount); }, "Deaths set");
    }, "GJ_button_02.png");

    // runs
    c = {8, top - 88};
    addLabel(c, "Run");
    m_runStart = addInput(c, 40, "S%");
    addLabel(c, "-");
    m_runEnd = addInput(c, 40, "E%");
    addLabel(c, "x");
    m_runAmount = addInput(c, 52, "n");
    addButton(c, "Add", [this] {
        auto start = readInt(m_runStart);
        auto end = readInt(m_runEnd);
        auto amount = readInt(m_runAmount);
        if (!start || !end || !amount) return showMessage("Enter a start, an end and an amount", false);
        dted::Scope scope;
        if (!resolveScope(scope)) return;
        runOperation([=](dted::Level& lv, dted::Options const& o) { return dted::addRuns(lv, o, scope, *start, *end, *amount); }, "Run added");
    }, "GJ_button_01.png");
    addButton(c, "Remove", [this] {
        auto start = readInt(m_runStart);
        auto end = readInt(m_runEnd);
        auto amount = readInt(m_runAmount);
        if (!start || !end || !amount) return showMessage("Enter a start, an end and an amount", false);
        dted::Scope scope;
        if (!resolveScope(scope)) return;
        runOperation([=](dted::Level& lv, dted::Options const& o) { return dted::removeRuns(lv, o, scope, *start, *end, *amount); }, "Runs removed");
    }, "GJ_button_06.png");
    addButton(c, "Set", [this] {
        auto start = readInt(m_runStart);
        auto end = readInt(m_runEnd);
        auto amount = readInt(m_runAmount);
        if (!start || !end || !amount) return showMessage("Enter a start, an end and the wanted count", false);
        dted::Scope scope;
        if (!resolveScope(scope)) return;
        runOperation([=](dted::Level& lv, dted::Options const& o) { return dted::setRuns(lv, o, scope, *start, *end, *amount); }, "Runs set");
    }, "GJ_button_02.png");

    // new bests
    c = {8, top - 122};
    addLabel(c, "New best at");
    m_bestPercent = addInput(c, 42, "%");
    addButton(c, "Add", [this] {
        auto percent = readInt(m_bestPercent);
        if (!percent) return showMessage("Enter a percent", false);
        dted::Scope scope;
        if (!resolveScope(scope)) return;
        runOperation([=](dted::Level& lv, dted::Options const&) { return dted::addNewBest(lv, scope, *percent); }, "New best added");
    }, "GJ_button_01.png");
    addButton(c, "Remove", [this] {
        auto percent = readInt(m_bestPercent);
        if (!percent) return showMessage("Enter a percent", false);
        dted::Scope scope;
        if (!resolveScope(scope)) return;
        runOperation([=](dted::Level& lv, dted::Options const&) { return dted::removeNewBest(lv, scope, *percent); }, "New best removed");
    }, "GJ_button_06.png");
    addNote(c, "Adding a death marks new bests by itself", .5f);

    // readout
    c = {10, top - 150};
    m_readout = addNote(c, "", .6f);

    buildResultArea(52);
    showMessage("Pick lifetime or a session, then add, remove or set deaths and runs.\nGD attempts and best % follow along (see the mod settings).", true);

    setPageActive(false);
    return true;
}

void EditorEditPage::refreshPage() {
    auto lvl = level();
    if (!lvl) return;

    updateSessionPicker(m_sessionSelector, m_dateLabel);
    if (m_scopeSwitch->getCurrentOptionIndex() == 0) m_dateLabel->setString("lifetime totals");

    auto state = EditorManager::load(lvl);
    if (state.isErr()) {
        m_readout->setString(state.unwrapErr().c_str());
        return;
    }
    auto const& lv = state.unwrap().level;

    dted::Data const* data = &lv.general;
    if (m_scopeSwitch->getCurrentOptionIndex() == 1) {
        auto dates = EditorManager::sessionDatesNewestFirst(state.unwrap());
        int index = m_sessionSelector->getCurrentCount();
        if (index >= 1 && index <= static_cast<int>(dates.size())) data = &lv.sessions.at(dates[index - 1]);
        else data = nullptr;
    }

    if (!data) {
        m_readout->setString("No session selected");
        return;
    }

    m_readout->setString(fmt::format("Selected: {} deaths | {} runs | {} new bests      GD: {} attempts, best {}%",
        dted::totalOf(data->deaths), dted::totalOf(data->runs), data->newBests.size(), lv.gdAttempts, lv.gdBest).c_str());
}

/* GD
====== */
EditorGdPage* EditorGdPage::create(const CCSize& size) {
    auto ret = new EditorGdPage();
    if (ret && ret->initWithSetup(size)) {
        ret->autorelease();
    } else {
        delete ret;
        ret = nullptr;
    }
    return ret;
}

bool EditorGdPage::setup() {
    const float top = size.height;

    Cursor c{8, top - 18};
    addNote(c, "These values are written to the real level, so they also change without the mod.", .6f);

    c = {8, top - 56};
    addLabel(c, "GD attempts");
    m_attemptsInput = addInput(c, 100, "attempts");
    addButton(c, "Set", [this] {
        auto text = std::string(m_attemptsInput->getString());
        auto parsed = geode::utils::numFromString<long long>(text);
        if (text.empty() || parsed.isErr()) return showMessage("Enter the attempts as a whole number", false);
        long long value = parsed.unwrap();
        bool force = m_forceToggler->isToggled();
        runOperation([=](dted::Level& lv, dted::Options const&) { return dted::setGdAttempts(lv, value, force); }, "GD attempts set");
    }, "GJ_button_01.png");
    m_trackedLabel = addNote(c, "", .55f);

    c = {8, top - 94};
    addLabel(c, "Best %");
    m_bestInput = addInput(c, 52, "%");
    addButton(c, "Set", [this] {
        auto value = readInt(m_bestInput);
        if (!value) return showMessage("Enter a percent", false);
        bool force = m_forceToggler->isToggled();
        runOperation([=](dted::Level& lv, dted::Options const&) { return dted::setGdBest(lv, *value, force); }, "GD best set");
    }, "GJ_button_01.png");

    addLabel(c, "Practice %");
    m_practiceInput = addInput(c, 52, "%");
    addButton(c, "Set", [this] {
        auto value = readInt(m_practiceInput);
        if (!value) return showMessage("Enter a percent", false);
        runOperation([=](dted::Level& lv, dted::Options const&) { return dted::setGdPracticeBest(lv, *value); }, "GD practice best set");
    }, "GJ_button_01.png");

    c = {8, top - 132};
    m_forceToggler = SimpleToggler::create(
        CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png"),
        CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png"),
        .6f,
        false
    );
    m_forceToggler->setPosition({c.x + m_forceToggler->getScaledContentWidth() / 2, c.y});
    this->addChild(m_forceToggler);
    c.x += m_forceToggler->getScaledContentWidth() + m_gap;
    addLabel(c, "Force (skip the safety checks)", .4f);

    c = {10, top - 160};
    m_linkLabel = addNote(c, "", .55f);

    buildResultArea(46);
    showMessage("Safety checks: GD attempts can't go below the deaths Death Tracker has, and GD best can't go below a tracked death.", true);

    setPageActive(false);
    return true;
}

void EditorGdPage::refreshPage() {
    auto lvl = level();
    if (!lvl) return;

    auto state = EditorManager::load(lvl);
    if (state.isErr()) {
        m_trackedLabel->setString(state.unwrapErr().c_str());
        return;
    }
    auto const& lv = state.unwrap().level;

    m_attemptsInput->setString(std::to_string(lv.gdAttempts).c_str());
    m_trackedLabel->setString(fmt::format("Death Tracker has {} from-0 deaths, {} runs", dted::totalOf(lv.general.deaths), dted::totalOf(lv.general.runs)).c_str());

    if (lv.platformer) {
        m_bestInput->setString("");
        m_practiceInput->setString("");
    }
    else {
        m_bestInput->setString(std::to_string(lv.gdBest).c_str());
        m_practiceInput->setString(std::to_string(lv.gdPracticeBest).c_str());
    }

    m_linkLabel->setString(fmt::format("Linking (mod settings): deaths->GD attempts {} | deaths->GD best {} | runs->GD attempts {}{}",
        Settings::getEditorLinkGdAttempts() ? "ON" : "off",
        Settings::getEditorSyncGdBest() ? "ON" : "off",
        Settings::getEditorRunsCountAsAttempts() ? "ON" : "off",
        lv.platformer ? " | platformer: no best %" : "").c_str());
}

/* Sessions
============ */
EditorSessionsPage* EditorSessionsPage::create(const CCSize& size) {
    auto ret = new EditorSessionsPage();
    if (ret && ret->initWithSetup(size)) {
        ret->autorelease();
    } else {
        delete ret;
        ret = nullptr;
    }
    return ret;
}

bool EditorSessionsPage::setup() {
    const float top = size.height;

    Cursor c{8, top - 20};
    addLabel(c, "Session");
    m_sessionSelector = addSessionSelector(c, [this](int) {
        updateSessionPicker(m_sessionSelector, m_dateLabel);
        updatePlaytimeReadout();
    });
    m_dateLabel = addNote(c, "", .6f);
    addNote(c, "(1 = newest)", .5f);

    // create / move
    auto now = static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    c = {8, top - 56};
    addLabel(c, "Date");
    m_dateInput = addInput(c, 150, "YYYY-MM-DD HH:MM", CommonFilter::Any, EditorManager::formatDate(now));
    addButton(c, "Create", [this] {
        auto date = EditorManager::parseDate(std::string(m_dateInput->getString()));
        if (!date) return showMessage("Use the format YYYY-MM-DD HH:MM", false);
        long long value = *date;
        runOperation([=](dted::Level& lv, dted::Options const&) { return dted::createSession(lv, value); }, "Session created");
    }, "GJ_button_01.png");
    addButton(c, "Move selected here", [this] {
        auto date = EditorManager::parseDate(std::string(m_dateInput->getString()));
        if (!date) return showMessage("Use the format YYYY-MM-DD HH:MM", false);
        long long from = 0;
        if (!resolveSession(m_sessionSelector, from)) return;
        long long to = *date;
        runOperation([=](dted::Level& lv, dted::Options const&) { return dted::moveSession(lv, from, to); }, "Session moved");
    }, "GJ_button_02.png");

    // delete
    c = {8, top - 92};
    addLabel(c, "Delete selected");
    addButton(c, "Keep totals", [this] { onDelete(false); }, "GJ_button_06.png");
    addButton(c, "Remove from totals too", [this] { onDelete(true); }, "GJ_button_06.png");

    // playtime
    c = {8, top - 128};
    addLabel(c, "Playtime", .36f);
    m_ptScope = addSwitcher(c, {64, 20}, {"Lifetime", "Session"}, [this](int) { updatePlaytimeReadout(); });
    m_ptKind = addSwitcher(c, {58, 20}, {"Active", "Paused", "Dead"}, [this](int) { updatePlaytimeReadout(); });
    m_ptMode = addSwitcher(c, {56, 20}, {"From 0", "Runs"}, [this](int) { updatePlaytimeReadout(); });
    m_ptHours = addInput(c, 40, "h");
    m_ptMinutes = addInput(c, 34, "m");
    m_ptSeconds = addInput(c, 34, "s");
    addButton(c, "Set", [this] {
        auto hours = readInt(m_ptHours);
        auto minutes = readInt(m_ptMinutes);
        auto seconds = readInt(m_ptSeconds);
        if (!hours && !minutes && !seconds) return showMessage("Enter hours, minutes and/or seconds", false);
        // keeps the nanosecond maths far away from overflowing (the engine enforces the real limit)
        if (hours.value_or(0) < 0 || minutes.value_or(0) < 0 || seconds.value_or(0) < 0
            || hours.value_or(0) > 1000000 || minutes.value_or(0) > 100000000 || seconds.value_or(0) > 1000000000)
            return showMessage("Playtime is limited to 100000 hours", false);

        dted::Scope scope;
        if (m_ptScope->getCurrentOptionIndex() == 1) {
            long long date = 0;
            if (!resolveSession(m_sessionSelector, date)) return;
            scope = date;
        }

        uint64_t totalSeconds = static_cast<uint64_t>(hours.value_or(0)) * 3600ull
            + static_cast<uint64_t>(minutes.value_or(0)) * 60ull
            + static_cast<uint64_t>(seconds.value_or(0));
        uint64_t nanoseconds = totalSeconds * 1000000000ull;
        auto kind = static_cast<dted::PtKind>(m_ptKind->getCurrentOptionIndex());
        auto mode = static_cast<dted::PtMode>(m_ptMode->getCurrentOptionIndex());
        runOperation([=](dted::Level& lv, dted::Options const&) { return dted::setPlaytime(lv, scope, kind, mode, nanoseconds); }, "Playtime set");
    }, "GJ_button_01.png");

    c = {10, top - 158};
    m_ptReadout = addNote(c, "", .6f);

    buildResultArea(46);
    showMessage("Sessions are part of the lifetime totals: deleting one can also take its deaths out of the totals.", true);

    setPageActive(false);
    return true;
}

void EditorSessionsPage::onDelete(bool alsoRemoveFromTotals) {
    long long date = 0;
    if (!resolveSession(m_sessionSelector, date)) return;

    std::string body = alsoRemoveFromTotals
        ? "Delete session " + EditorManager::formatDate(date) + " <cr>and take its deaths, runs and playtime out of the lifetime totals</c>?\nA backup is made first."
        : "Delete session " + EditorManager::formatDate(date) + "? The lifetime totals keep its deaths.\nA backup is made first.";

    geode::createQuickPopup("Delete session", body, "Cancel", "Delete", [this, date, alsoRemoveFromTotals](FLAlertLayer*, bool confirmed) {
        if (!confirmed) return;
        runOperation([=](dted::Level& lv, dted::Options const& o) { return dted::deleteSession(lv, o, date, alsoRemoveFromTotals); }, "Session deleted");
    });
}

void EditorSessionsPage::updatePlaytimeReadout() {
    auto lvl = level();
    if (!lvl) return;

    auto state = EditorManager::load(lvl);
    if (state.isErr()) {
        m_ptReadout->setString(state.unwrapErr().c_str());
        return;
    }
    auto const& lv = state.unwrap().level;

    dted::Data const* data = &lv.general;
    if (m_ptScope->getCurrentOptionIndex() == 1) {
        auto dates = EditorManager::sessionDatesNewestFirst(state.unwrap());
        int index = m_sessionSelector->getCurrentCount();
        if (index >= 1 && index <= static_cast<int>(dates.size())) data = &lv.sessions.at(dates[index - 1]);
        else data = nullptr;
    }

    if (!data) {
        m_ptReadout->setString("Current: no session selected");
        return;
    }

    uint64_t nanoseconds = data->pt[m_ptKind->getCurrentOptionIndex()][m_ptMode->getCurrentOptionIndex()];
    m_ptReadout->setString(("Current: " + StatsManager::workingTime(nanoseconds)).c_str());
}

void EditorSessionsPage::refreshPage() {
    updateSessionPicker(m_sessionSelector, m_dateLabel);
    updatePlaytimeReadout();
}

/* Check
========= */
EditorCheckPage* EditorCheckPage::create(const CCSize& size) {
    auto ret = new EditorCheckPage();
    if (ret && ret->initWithSetup(size)) {
        ret->autorelease();
    } else {
        delete ret;
        ret = nullptr;
    }
    return ret;
}

bool EditorCheckPage::setup() {
    const float top = size.height;

    Cursor c{8, top - 20};
    addButton(c, "Verify", [this] {
        auto lvl = level();
        if (!lvl) return showMessage("No level is loaded", false);

        auto state = EditorManager::load(lvl);
        if (state.isErr()) return showMessage(state.unwrapErr(), false);

        auto issues = dted::verify(state.unwrap().level);
        if (issues.empty()) return showMessage("No problems found. Everything adds up.", true);

        std::string text = std::to_string(issues.size()) + " problem(s)  (* = Fix problems repairs it):";
        int shown = 0;
        for (auto const& issue : issues) {
            if (shown >= 8) {
                text += "\n... and " + std::to_string(issues.size() - shown) + " more";
                break;
            }
            text += "\n- " + issue.text + (issue.fixable ? " *" : "");
            shown++;
        }
        showMessage(text, false);
    }, "GJ_button_01.png");
    addButton(c, "Fix problems", [this] {
        runOperation([](dted::Level& lv, dted::Options const&) { return dted::autoFix(lv); }, "Fixed");
    }, "GJ_button_02.png");
    addButton(c, "Remove invalid entries", [this] {
        geode::createQuickPopup("Remove invalid entries", "Delete death/run entries whose percent is out of range or unreadable?\nA backup is made first.", "Cancel", "Delete", [this](FLAlertLayer*, bool confirmed) {
            if (!confirmed) return;
            runOperation([](dted::Level& lv, dted::Options const&) { return dted::removeInvalidEntries(lv); }, "Done");
        });
    }, "GJ_button_06.png");

    c = {8, top - 56};
    addButton(c, "Undo last edit", [this] {
        auto lvl = level();
        if (!lvl) return showMessage("No level is loaded", false);

        auto result = EditorManager::undo(lvl);
        showResult(result, "Undone");
        refreshPage();
    }, "GJ_button_04.png");
    addButton(c, "Backup now", [this] {
        auto lvl = level();
        if (!lvl) return showMessage("No level is loaded", false);

        showResult(EditorManager::backupNow(lvl), "Backup created");
    }, "GJ_button_01.png");
    addButton(c, "Mod settings", [] {
        geode::openSettingsPopup(Mod::get());
    }, "GJ_button_01.png");

    c = {10, top - 88};
    m_infoLabel = addNote(c, "", .6f);

    buildResultArea(top - 112);
    showMessage("Verify checks that sessions, lifetime totals, DT attempts and GD values agree with each other.", true);

    setPageActive(false);
    return true;
}

void EditorCheckPage::refreshPage() {
    m_infoLabel->setString(fmt::format("Undo history: {} edit(s)   |   Backup before editing: {}",
        EditorManager::undoDepth(),
        Settings::getEditorAutoBackup() ? "on" : "off").c_str());
}
