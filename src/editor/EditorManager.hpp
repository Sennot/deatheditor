#pragma once

#include <Geode/Geode.hpp>
#include <editor/EditorEngine.hpp>

#include <functional>

using namespace geode::prelude;

// Everything the editor knows about one level at one moment: the save files plus the GD values.
struct EditorState {
    std::string levelKey;
    dted::Level level;
    std::map<long long, long long> lastPlayed{}; // session start date -> lastPlayed (kept so edits don't lose it)
};

// Glue between the pure rules engine (dted::*) and the real world: save files, the GJGameLevel,
// the StatsManager caches and the open Death Tracker screen.
class EditorManager {
public:
    using Operation = std::function<dted::Result(dted::Level&, dted::Options const&)>;

    // Reads the level's save files and GD values. Fails when a file is unreadable: the editor
    // never edits (and therefore never overwrites) data it could not read.
    static Result<EditorState> load(GJGameLevel* level);

    // the editor settings as engine options
    static dted::Options options();

    // Loads fresh data, runs `op` on it and, if it succeeds and changed something, backs up (once per
    // options popup), writes everything back and refreshes the caches/screens. The result carries the
    // engine's notes so the UI can show the side effects of the "smart logic".
    static dted::Result apply(GJGameLevel* level, Operation const& op);

    // reverts the last applied edit (GD values included)
    static dted::Result undo(GJGameLevel* level);
    static size_t undoDepth();

    // forget the undo history and take a new backup on the next edit (called when the options popup opens)
    static void resetSession();

    // synchronous backup right now
    static dted::Result backupNow(GJGameLevel* level);

    // session start dates, newest first (the order the session selector uses)
    static std::vector<long long> sessionDatesNewestFirst(EditorState const& state);

    // "2025-03-14 21:05" in local time
    static std::string formatDate(long long unixSeconds);
    // parses "YYYY-MM-DD HH:MM" (local time); nullopt if invalid
    static std::optional<long long> parseDate(std::string const& text);

private:
    static Result<> commit(GJGameLevel* level, EditorState const& before, EditorState const& after);
    static void persistGd(GJGameLevel* level);
    static void refreshCaches(GJGameLevel* level, EditorState const& after);

    static std::vector<EditorState> s_undo;
    static std::string s_backupDoneFor;
};
