#pragma once

// Pure-STL rules engine for Editor Mode.
//
// It knows nothing about Geode or the save files. The glue layer (EditorManager) loads the
// real data into a dted::Level, calls one of the operations below and, if it succeeded,
// writes the changed pieces back. Every operation is transactional: on failure the Level
// is left exactly as it was, on success `Result::notes` lists every side effect so the UI
// can show the user what the "smart logic" did on top of the requested edit.

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace dted {

using Counts = std::map<std::string, int>; // "37" -> deaths, "10-30" -> runs
using Bests = std::set<int>;

enum PtKind { PtPlay = 0, PtPaused = 1, PtDead = 2 };
enum PtMode { PtFrom0 = 0, PtRuns = 1 };

constexpr int kMaxCount = 100000000;          // per percent / per run entry
constexpr long long kMaxAttempts = 2000000000; // GD stores attempts in an int
constexpr uint64_t kMaxPlaytimeNs = 360000000000000000ull; // 100 000 hours

struct Data {
    Counts deaths;
    Counts runs;
    Bests newBests;
    uint64_t pt[3][2] = {}; // [PtKind][PtMode], nanoseconds
};

bool operator==(Data const& a, Data const& b);
inline bool operator!=(Data const& a, Data const& b) { return !(a == b); }

struct Level {
    Data general;                       // lifetime totals of the level
    std::map<long long, Data> sessions; // key = session start date
    int maxPercent = 100;               // highest percent a death/run/new best may have (platformers count checkpoints)
    bool platformer = false;            // platformers have no GD best %: GD best is never touched
    int gdAttempts = 0;                 // GJGameLevel::m_attempts
    int gdBest = 0;                     // best normal mode %
    int gdPracticeBest = 0;             // best practice mode %
    std::vector<int> gdBestChain;       // GJGameLevel::m_personalBests, decoded
};

struct Options {
    bool linkGdAttempts = true;     // from-0 deaths added/removed also move GD attempts
    bool syncGdBest = true;         // a death above the GD best raises the GD best
    bool runsCountAsAttempts = false; // runs (practice / mid-level) also move GD attempts
};

struct Result {
    bool ok = false;
    std::string error;
    std::vector<std::string> notes;
};

struct Issue {
    std::string text;
    bool fixable = false;
};

// Scope of an edit: nullopt = lifetime totals, otherwise the session with this start date.
using Scope = std::optional<long long>;

/* helpers */
std::string percentKey(int percent);
std::string runKey(int start, int end);
bool parsePercentKey(std::string const& key, int& out);
bool parseRunKey(std::string const& key, int& start, int& end);

long long totalOf(Counts const& counts);
// highest percent that has a positive death count and is strictly below `exclusiveMax`, or 0
int highestDeathPercent(Data const& data, int exclusiveMax = 101);

std::optional<std::vector<int>> decodeBests(std::string const& personalBests);
std::string encodeBests(std::vector<int> const& chain);

/* deaths (from 0) and runs: "add", "remove" and absolute "set" in a scope */
Result addDeaths(Level& lv, Options const& opt, Scope scope, int percent, int amount);
Result removeDeaths(Level& lv, Options const& opt, Scope scope, int percent, int amount);
Result setDeaths(Level& lv, Options const& opt, Scope scope, int percent, int target);

Result addRuns(Level& lv, Options const& opt, Scope scope, int start, int end, int amount);
Result removeRuns(Level& lv, Options const& opt, Scope scope, int start, int end, int amount);
Result setRuns(Level& lv, Options const& opt, Scope scope, int start, int end, int target);

/* new bests */
Result addNewBest(Level& lv, Scope scope, int percent);
Result removeNewBest(Level& lv, Scope scope, int percent);

/* geometry dash side. `force` skips the consistency checks (the result still lists warnings) */
Result setGdAttempts(Level& lv, long long value, bool force);
Result setGdBest(Level& lv, int value, bool force);
Result setGdPracticeBest(Level& lv, int value);

/* sessions */
Result createSession(Level& lv, long long startDate);
Result deleteSession(Level& lv, Options const& opt, long long startDate, bool alsoRemoveFromTotals);
Result moveSession(Level& lv, long long fromDate, long long toDate);

/* playtime */
Result setPlaytime(Level& lv, Scope scope, PtKind kind, PtMode mode, uint64_t nanoseconds);

/* consistency */
std::vector<Issue> verify(Level const& lv);
Result autoFix(Level& lv);
// deletes entries that can never be valid (unparsable keys, percents out of range, inverted runs).
// Only ever called on explicit request: verify() reports them but autoFix() leaves them alone.
Result removeInvalidEntries(Level& lv);

} // namespace dted
