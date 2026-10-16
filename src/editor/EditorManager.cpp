#include <editor/EditorManager.hpp>

#include <managers/StatsManager.hpp>
#include <nodes/layers/DTLayer.hpp>
#include <utils/Settings.hpp>

#include <algorithm>
#include <cstdio>
#include <ctime>

std::vector<EditorState> EditorManager::s_undo{};
std::string EditorManager::s_backupDoneFor{};

namespace {

constexpr size_t kMaxUndo = 30;

dted::Result failure(std::string message) {
    dted::Result r;
    r.ok = false;
    r.error = std::move(message);
    return r;
}

GeneralData toGeneral(dted::Data const& d) {
    GeneralData g;
    g.deaths = d.deaths;
    g.runs = d.runs;
    g.newBests = d.newBests;
    g.playtimeGeneral.playtimeF0 = d.pt[dted::PtPlay][dted::PtFrom0];
    g.playtimeGeneral.playtimeRuns = d.pt[dted::PtPlay][dted::PtRuns];
    g.playtimePaused.playtimeF0 = d.pt[dted::PtPaused][dted::PtFrom0];
    g.playtimePaused.playtimeRuns = d.pt[dted::PtPaused][dted::PtRuns];
    g.playtimeDead.playtimeF0 = d.pt[dted::PtDead][dted::PtFrom0];
    g.playtimeDead.playtimeRuns = d.pt[dted::PtDead][dted::PtRuns];
    return g;
}

dted::Data fromGeneral(GeneralData const& g) {
    dted::Data d;
    d.deaths = g.deaths;
    d.runs = g.runs;
    d.newBests = g.newBests;
    d.pt[dted::PtPlay][dted::PtFrom0] = g.playtimeGeneral.playtimeF0;
    d.pt[dted::PtPlay][dted::PtRuns] = g.playtimeGeneral.playtimeRuns;
    d.pt[dted::PtPaused][dted::PtFrom0] = g.playtimePaused.playtimeF0;
    d.pt[dted::PtPaused][dted::PtRuns] = g.playtimePaused.playtimeRuns;
    d.pt[dted::PtDead][dted::PtFrom0] = g.playtimeDead.playtimeF0;
    d.pt[dted::PtDead][dted::PtRuns] = g.playtimeDead.playtimeRuns;
    return d;
}

bool sameLevel(dted::Level const& a, dted::Level const& b) {
    return a.general == b.general
        && a.sessions == b.sessions
        && a.gdAttempts == b.gdAttempts
        && a.gdBest == b.gdBest
        && a.gdPracticeBest == b.gdPracticeBest
        && a.gdBestChain == b.gdBestChain;
}

} // namespace

/* loading
=========== */
Result<EditorState> EditorManager::load(GJGameLevel* level) {
    if (!level) return Err("No level");

    EditorState state;
    GEODE_UNWRAP_INTO(state.levelKey, StatsManager::getLevelKey(level));

    auto generalRes = StatsManager::getGeneral(state.levelKey);
    if (generalRes.isOk()) {
        state.level.general = fromGeneral(generalRes.unwrap());
    }
    else if (generalRes.unwrapErr().code != 1) {
        return Err("The lifetime data of this level can't be read (" + generalRes.unwrapErr().error + "). Nothing was changed.");
    }

    for (auto sessionTime : StatsManager::getAllSessionTimesForLevel(state.levelKey)) {
        auto sessionRes = StatsManager::getSession(state.levelKey, sessionTime);
        if (sessionRes.isErr()) {
            if (sessionRes.unwrapErr().code == 1) continue;
            return Err(fmt::format("Session {} can't be read ({}). Nothing was changed.", sessionTime, sessionRes.unwrapErr().error));
        }

        auto session = sessionRes.unwrap();
        state.level.sessions[sessionTime] = fromGeneral(session.data);
        state.lastPlayed[sessionTime] = session.lastPlayed;
    }

    state.level.platformer = level->isPlatformer();
    state.level.maxPercent = state.level.platformer ? 10000 : 100;
    state.level.gdAttempts = level->m_attempts.value();
    state.level.gdBest = level->m_newNormalPercent2.value();
    state.level.gdPracticeBest = level->m_practicePercent;

    auto chain = dted::decodeBests(std::string(level->m_personalBests));
    if (chain.has_value()) state.level.gdBestChain = std::move(chain.value());

    return Ok(std::move(state));
}

dted::Options EditorManager::options() {
    dted::Options o;
    o.linkGdAttempts = Settings::getEditorLinkGdAttempts();
    o.syncGdBest = Settings::getEditorSyncGdBest();
    o.runsCountAsAttempts = Settings::getEditorRunsCountAsAttempts();
    return o;
}

std::vector<long long> EditorManager::sessionDatesNewestFirst(EditorState const& state) {
    std::vector<long long> dates;
    dates.reserve(state.level.sessions.size());
    for (auto it = state.level.sessions.rbegin(); it != state.level.sessions.rend(); ++it) dates.push_back(it->first);
    return dates;
}

std::string EditorManager::formatDate(long long unixSeconds) {
    std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm tm{};
    if (localtime_s(&tm, &t) != 0) return "?";
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &tm);
    return buffer;
}

std::optional<long long> EditorManager::parseDate(std::string const& text) {
    int y, mo, d, h, mi;
    char extra;
    if (std::sscanf(text.c_str(), "%d-%d-%d %d:%d%c", &y, &mo, &d, &h, &mi, &extra) != 5) return std::nullopt;
    if (y < 2000 || y > 2100 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59) return std::nullopt;

    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_isdst = -1;
    std::time_t t = std::mktime(&tm);
    if (t == static_cast<std::time_t>(-1)) return std::nullopt;
    // mktime normalises things like 31 February; reject those instead of silently moving the date
    if (tm.tm_mday != d || tm.tm_mon != mo - 1) return std::nullopt;
    return static_cast<long long>(t);
}

/* writing
=========== */
void EditorManager::persistGd(GJGameLevel* level) {
    // online levels live in the saved levels list, everything else is saved with the game itself
    if (level->m_levelType == GJLevelType::Saved) GameLevelManager::get()->saveLevel(level);
    if (auto app = AppDelegate::get()) app->trySaveGame(true);
}

Result<> EditorManager::commit(GJGameLevel* level, EditorState const& before, EditorState const& after) {
    auto const& key = after.levelKey;

    if (after.level.general != before.level.general) {
        GEODE_UNWRAP(StatsManager::setGeneral(toGeneral(after.level.general), key));
    }

    for (auto const& [date, data] : after.level.sessions) {
        auto it = before.level.sessions.find(date);
        if (it != before.level.sessions.end() && it->second == data) continue;

        Session session{};
        session.ownerLevelKey = key;
        session.sessionStartDate = date;
        session.groupID = 0;
        session.data = toGeneral(data);
        auto played = after.lastPlayed.find(date);
        session.lastPlayed = played != after.lastPlayed.end() ? played->second : date;

        GEODE_UNWRAP(StatsManager::setSession(session, key, date, false));
    }

    for (auto const& [date, _] : before.level.sessions) {
        if (after.level.sessions.contains(date)) continue;
        GEODE_UNWRAP(StatsManager::deleteSession(key, date));
    }

    bool gdChanged = false;
    if (after.level.gdAttempts != before.level.gdAttempts) {
        level->m_attempts = after.level.gdAttempts;
        gdChanged = true;

        // a running PlayLayer counts attempts itself; move it by the same amount so the next
        // death doesn't write the old number back (the in-game "Attempt N" follows too)
        if (auto playLayer = PlayLayer::get(); playLayer && playLayer->m_level == level) {
            long long moved = static_cast<long long>(playLayer->m_attempts) + (after.level.gdAttempts - before.level.gdAttempts);
            playLayer->m_attempts = static_cast<int>(std::max<long long>(0, moved));
        }
    }
    if (after.level.gdBest != before.level.gdBest) {
        level->m_newNormalPercent2 = after.level.gdBest;
        level->m_normalPercent = after.level.gdBest;
        gdChanged = true;
    }
    if (after.level.gdPracticeBest != before.level.gdPracticeBest) {
        level->m_practicePercent = after.level.gdPracticeBest;
        gdChanged = true;
    }
    if (after.level.gdBestChain != before.level.gdBestChain) {
        level->m_personalBests = dted::encodeBests(after.level.gdBestChain);
        gdChanged = true;
    }
    if (gdChanged) persistGd(level);

    return Ok();
}

void EditorManager::refreshCaches(GJGameLevel* level, EditorState const& after) {
    // PlayLayer::onExit rewrites the save files from these caches, so they must match what we wrote
    // or the edit would be overwritten the moment the player leaves the level
    auto currentKey = StatsManager::getLevelKey(StatsManager::currentLevel);
    if (StatsManager::currentLevel != nullptr && currentKey.isOk() && currentKey.unwrap() == after.levelKey) {
        StatsManager::currentFrom0 = toGeneral(after.level.general);

        if (StatsManager::currentSession.has_value()) {
            auto it = after.level.sessions.find(StatsManager::currentSession->sessionStartDate);
            if (it != after.level.sessions.end()) StatsManager::currentSession->data = toGeneral(it->second);
            else StatsManager::currentSession = std::nullopt; // deleted/moved: StatsManager reloads the latest one
        }
    }

    if (auto dtLayer = DTLayer::get()) {
        dtLayer->CleanGetStats();
        for (auto& [_, specialKey] : dtLayer->specialStrings) {
            if (specialKey) specialKey->updateContent();
        }
    }
    else if (auto meta = StatsManager::getMetadata(after.levelKey); meta.isOk()) {
        auto metadata = meta.unwrap();
        metadata.attempts = after.level.gdAttempts;
        (void)StatsManager::setMetadata(metadata, after.levelKey);
    }
}

/* public operations
===================== */
dted::Result EditorManager::apply(GJGameLevel* level, Operation const& op) {
    auto loaded = load(level);
    if (loaded.isErr()) return failure(loaded.unwrapErr());

    EditorState before = loaded.unwrap();
    EditorState after = before;

    auto result = op(after.level, options());
    if (!result.ok) return result;
    if (sameLevel(before.level, after.level)) return result;

    if (Settings::getEditorAutoBackup() && s_backupDoneFor != before.levelKey) {
        auto backup = StatsManager::createBackupNow(before.levelKey);
        if (backup.isOk()) {
            s_backupDoneFor = before.levelKey;
            result.notes.insert(result.notes.begin(), "Backup created");
        }
        else if (std::filesystem::exists(StatsManager::getSavesFolderPath() / before.levelKey)) {
            // there is data to protect but the backup failed: do not edit
            return failure("Backup failed, nothing was changed: " + backup.unwrapErr());
        }
        // no save folder yet: nothing to back up
    }

    auto committed = commit(level, before, after);
    if (committed.isErr()) {
        auto error = committed.unwrapErr();
        // best effort: put back whatever was already written
        (void)commit(level, after, before);
        refreshCaches(level, before);
        return failure("Saving failed, the edit was rolled back: " + error);
    }

    s_undo.push_back(std::move(before));
    if (s_undo.size() > kMaxUndo) s_undo.erase(s_undo.begin());

    refreshCaches(level, after);
    return result;
}

dted::Result EditorManager::undo(GJGameLevel* level) {
    if (s_undo.empty()) return failure("Nothing to undo");

    auto loaded = load(level);
    if (loaded.isErr()) return failure(loaded.unwrapErr());
    EditorState current = loaded.unwrap();

    if (s_undo.back().levelKey != current.levelKey) {
        s_undo.clear();
        return failure("The undo history belongs to another level");
    }

    EditorState target = s_undo.back();
    auto committed = commit(level, current, target);
    if (committed.isErr()) return failure("Undo failed: " + committed.unwrapErr());

    s_undo.pop_back();
    refreshCaches(level, target);

    dted::Result r;
    r.ok = true;
    r.notes.push_back("Undone");
    return r;
}

size_t EditorManager::undoDepth() {
    return s_undo.size();
}

void EditorManager::resetSession() {
    s_undo.clear();
    s_backupDoneFor.clear();
}

dted::Result EditorManager::backupNow(GJGameLevel* level) {
    auto key = StatsManager::getLevelKey(level);
    if (key.isErr()) return failure(key.unwrapErr());

    auto backup = StatsManager::createBackupNow(key.unwrap());
    if (backup.isErr()) return failure(backup.unwrapErr());

    s_backupDoneFor = key.unwrap();

    dted::Result r;
    r.ok = true;
    r.notes.push_back("Backup created");
    return r;
}
