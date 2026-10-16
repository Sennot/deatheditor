#include "EditorEngine.hpp"

#include <algorithm>
#include <cstdlib>
#include <iterator>

namespace dted {

namespace {

Result fail(std::string msg) {
    Result r;
    r.ok = false;
    r.error = std::move(msg);
    return r;
}

Result success(std::vector<std::string> notes = {}) {
    Result r;
    r.ok = true;
    r.notes = std::move(notes);
    return r;
}

std::string str(long long v) { return std::to_string(v); }

bool validPercent(Level const& lv, int p) { return p >= 0 && p <= lv.maxPercent; }
bool validGdPercent(int p) { return p >= 0 && p <= 100; }

Data* dataFor(Level& lv, Scope const& scope) {
    if (!scope.has_value()) return &lv.general;
    auto it = lv.sessions.find(*scope);
    return it == lv.sessions.end() ? nullptr : &it->second;
}

long long sessionsSum(Level const& lv, Counts Data::* field, std::string const& key) {
    long long sum = 0;
    for (auto const& [_, data] : lv.sessions) {
        auto it = (data.*field).find(key);
        if (it != (data.*field).end() && it->second > 0) sum += it->second;
    }
    return sum;
}

// mirrors StatsManager::logDeath: a death is a new best when it is above every recorded best
void applyNewBest(Data& data, int percent) {
    int best = data.newBests.empty() ? -1 : *data.newBests.rbegin();
    if (percent > best) data.newBests.insert(percent);
}

void pruneBestIfNoDeaths(Level& lv, int percent) {
    auto key = percentKey(percent);
    auto prune = [&](Data& d) {
        auto it = d.deaths.find(key);
        if (it == d.deaths.end() || it->second <= 0) d.newBests.erase(percent);
    };
    prune(lv.general);
    for (auto& [_, data] : lv.sessions) prune(data);
}

// Raises/lowers GD attempts by `delta` (the number of lifetime attempts that were really
// added/removed). Lowering never goes below the number of tracked from-0 deaths.
void moveGdAttempts(Level& lv, long long delta, std::vector<std::string>& notes) {
    if (delta == 0) return;
    long long before = lv.gdAttempts;
    long long target = before + delta;
    if (delta < 0) {
        long long floorValue = totalOf(lv.general.deaths);
        if (target < floorValue) {
            // never silently raise a value that was already below the floor
            target = before >= floorValue ? floorValue : before;
            notes.push_back("GD attempts kept at " + str(target) + " (can't go below the " + str(floorValue) + " tracked deaths)");
        }
    }
    if (target < 0) target = 0;
    if (target > kMaxAttempts) target = kMaxAttempts;
    if (target != before) {
        lv.gdAttempts = static_cast<int>(target);
        notes.push_back("GD attempts " + str(before) + " -> " + str(target));
    }
}

// Raises the GD best when a death above it is added. 100 is a completion and is never
// pushed into GD: a fake 100% would mark the level as beaten in the real game.
void raiseGdBest(Level& lv, int percent, std::vector<std::string>& notes) {
    if (lv.platformer) return;
    if (percent >= 100) {
        notes.push_back("100% is a completion: recorded in Death Tracker only, GD completion not changed");
        return;
    }
    if (percent <= lv.gdBest) return;
    notes.push_back("GD best " + str(lv.gdBest) + "% -> " + str(percent) + "%");
    lv.gdBest = percent;
    if (lv.gdBestChain.empty() || lv.gdBestChain.back() < percent) lv.gdBestChain.push_back(percent);
}

// Shared add/remove with lifetime <-> session propagation.
// `generalChange` receives the real change applied to the lifetime totals.
std::string changeCount(Level& lv, Counts Data::* field, std::string const& key, long long delta, Scope const& scope,
                        std::vector<std::string>& notes, long long& generalChange) {
    generalChange = 0;
    Data* target = dataFor(lv, scope);
    if (!target) return "Session not found";

    if (delta > 0) {
        int& total = (lv.general.*field)[key];
        if (static_cast<long long>(total) + delta > kMaxCount) return "Too many entries (limit " + str(kMaxCount) + " per percent)";
        total += static_cast<int>(delta);
        if (scope.has_value()) {
            int& inSession = (target->*field)[key];
            if (static_cast<long long>(inSession) + delta > kMaxCount) return "Too many entries in the session (limit " + str(kMaxCount) + ")";
            inSession += static_cast<int>(delta);
        }
        generalChange = delta;
        return {};
    }

    long long n = -delta;
    if (scope.has_value()) {
        auto it = (target->*field).find(key);
        long long have = it == (target->*field).end() ? 0 : it->second;
        if (have < n) return "The session only has " + str(have) + " of those (asked to remove " + str(n) + ")";
        it->second -= static_cast<int>(n);
        if (it->second <= 0) (target->*field).erase(it);

        auto git = (lv.general.*field).find(key);
        long long gHave = git == (lv.general.*field).end() ? 0 : git->second;
        long long take = std::min(gHave, n);
        if (take < n) notes.push_back("Lifetime totals had fewer entries than the session (inconsistent data), clamped");
        if (take > 0) {
            git->second -= static_cast<int>(take);
            if (git->second <= 0) (lv.general.*field).erase(git);
        }
        generalChange = -take;
        return {};
    }

    auto git = (lv.general.*field).find(key);
    long long have = git == (lv.general.*field).end() ? 0 : git->second;
    if (have < n) return "Lifetime totals only have " + str(have) + " of those (asked to remove " + str(n) + ")";
    long long remaining = have - n;

    // keep "sum of sessions <= lifetime": take the excess out of the newest sessions first
    long long sessSum = sessionsSum(lv, field, key);
    long long excess = sessSum - remaining;
    if (excess > 0) {
        for (auto it = lv.sessions.rbegin(); it != lv.sessions.rend() && excess > 0; ++it) {
            auto& counts = it->second.*field;
            auto cit = counts.find(key);
            if (cit == counts.end() || cit->second <= 0) continue;
            long long cut = std::min<long long>(cit->second, excess);
            cit->second -= static_cast<int>(cut);
            excess -= cut;
            notes.push_back("Removed " + str(cut) + " from session " + str(it->first) + " to keep sessions <= lifetime");
            if (cit->second <= 0) counts.erase(cit);
        }
    }
    if (remaining <= 0) (lv.general.*field).erase(git);
    else git->second = static_cast<int>(remaining);
    generalChange = -n;
    return {};
}

bool parseInt(std::string const& s, int& out) {
    if (s.empty() || s.size() > 9) return false;
    size_t i = 0;
    bool neg = false;
    if (s[0] == '-') {
        neg = true;
        i = 1;
        if (s.size() == 1) return false;
    }
    long long v = 0;
    for (; i < s.size(); i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    out = static_cast<int>(neg ? -v : v);
    return true;
}

Result validateAmount(int amount) {
    if (amount < 1) return fail("Amount must be at least 1");
    if (amount > kMaxCount) return fail("Amount is too large (max " + str(kMaxCount) + ")");
    return success();
}

} // namespace

bool operator==(Data const& a, Data const& b) {
    if (a.deaths != b.deaths || a.runs != b.runs || a.newBests != b.newBests) return false;
    for (int k = 0; k < 3; k++)
        for (int m = 0; m < 2; m++)
            if (a.pt[k][m] != b.pt[k][m]) return false;
    return true;
}

/* helpers
=========== */
std::string percentKey(int percent) { return std::to_string(percent); }

std::string runKey(int start, int end) { return std::to_string(start) + "-" + std::to_string(end); }

bool parsePercentKey(std::string const& key, int& out) { return parseInt(key, out); }

bool parseRunKey(std::string const& key, int& start, int& end) {
    // "a-b"; the separator is the first '-' after position 0 (a may be negative)
    size_t sep = key.find('-', 1);
    if (sep == std::string::npos) return false;
    return parseInt(key.substr(0, sep), start) && parseInt(key.substr(sep + 1), end);
}

long long totalOf(Counts const& counts) {
    long long sum = 0;
    for (auto const& [_, c] : counts)
        if (c > 0) sum += c;
    return sum;
}

int highestDeathPercent(Data const& data, int exclusiveMax) {
    int best = 0;
    for (auto const& [key, count] : data.deaths) {
        int p;
        if (count > 0 && parsePercentKey(key, p) && p < exclusiveMax && p > best) best = p;
    }
    return best;
}

std::optional<std::vector<int>> decodeBests(std::string const& personalBests) {
    std::vector<int> chain;
    long long acc = 0;
    size_t pos = 0;
    while (pos <= personalBests.size() && !personalBests.empty()) {
        size_t comma = personalBests.find(',', pos);
        std::string token = personalBests.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (!token.empty()) {
            int delta;
            if (!parseInt(token, delta) || delta < 0) return std::nullopt;
            acc += delta;
            if (acc > 100) return std::nullopt;
            chain.push_back(static_cast<int>(acc));
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return chain;
}

std::string encodeBests(std::vector<int> const& chain) {
    std::string out;
    int prev = 0;
    for (size_t i = 0; i < chain.size(); i++) {
        if (i) out += ",";
        out += std::to_string(chain[i] - prev);
        prev = chain[i];
    }
    return out;
}

/* deaths
========== */
Result addDeaths(Level& lv, Options const& opt, Scope scope, int percent, int amount) {
    if (!validPercent(lv, percent)) return fail("Percent must be 0-" + str(lv.maxPercent));
    if (auto v = validateAmount(amount); !v.ok) return v;

    Level w = lv;
    std::vector<std::string> notes;
    long long generalChange = 0;
    if (auto err = changeCount(w, &Data::deaths, percentKey(percent), amount, scope, notes, generalChange); !err.empty()) return fail(err);

    applyNewBest(w.general, percent);
    if (scope.has_value()) applyNewBest(w.sessions[*scope], percent);

    if (opt.linkGdAttempts) moveGdAttempts(w, generalChange, notes);
    if (opt.syncGdBest) raiseGdBest(w, percent, notes);

    lv = std::move(w);
    return success(std::move(notes));
}

Result removeDeaths(Level& lv, Options const& opt, Scope scope, int percent, int amount) {
    if (!validPercent(lv, percent)) return fail("Percent must be 0-" + str(lv.maxPercent));
    if (auto v = validateAmount(amount); !v.ok) return v;

    Level w = lv;
    std::vector<std::string> notes;
    long long generalChange = 0;
    if (auto err = changeCount(w, &Data::deaths, percentKey(percent), -static_cast<long long>(amount), scope, notes, generalChange); !err.empty()) return fail(err);

    pruneBestIfNoDeaths(w, percent);

    if (opt.linkGdAttempts) moveGdAttempts(w, generalChange, notes);
    if (!w.platformer && percent >= w.gdBest && percent > 0 && percent < 100 && opt.syncGdBest)
        notes.push_back("GD best is still " + str(w.gdBest) + "% - lower it in the GD tab if needed");

    lv = std::move(w);
    return success(std::move(notes));
}

Result setDeaths(Level& lv, Options const& opt, Scope scope, int percent, int target) {
    if (!validPercent(lv, percent)) return fail("Percent must be 0-" + str(lv.maxPercent));
    if (target < 0) return fail("Count can't be negative");
    Data* d = dataFor(lv, scope);
    if (!d) return fail("Session not found");
    auto it = d->deaths.find(percentKey(percent));
    long long cur = it == d->deaths.end() ? 0 : it->second;
    long long diff = static_cast<long long>(target) - cur;
    if (diff == 0) return success({"Already " + str(cur)});
    if (std::llabs(diff) > kMaxCount) return fail("Change is too large");
    return diff > 0 ? addDeaths(lv, opt, scope, percent, static_cast<int>(diff))
                    : removeDeaths(lv, opt, scope, percent, static_cast<int>(-diff));
}

/* runs
======== */
static std::string validateRun(Level const& lv, int start, int end) {
    if (!validPercent(lv, start) || !validPercent(lv, end)) return "Run percents must be 0-" + str(lv.maxPercent);
    if (start > end) return "Run start can't be above run end";
    return {};
}

Result addRuns(Level& lv, Options const& opt, Scope scope, int start, int end, int amount) {
    if (auto e = validateRun(lv, start, end); !e.empty()) return fail(e);
    if (auto v = validateAmount(amount); !v.ok) return v;

    Level w = lv;
    std::vector<std::string> notes;
    long long generalChange = 0;
    if (auto err = changeCount(w, &Data::runs, runKey(start, end), amount, scope, notes, generalChange); !err.empty()) return fail(err);

    if (opt.runsCountAsAttempts) moveGdAttempts(w, generalChange, notes);

    lv = std::move(w);
    return success(std::move(notes));
}

Result removeRuns(Level& lv, Options const& opt, Scope scope, int start, int end, int amount) {
    if (auto e = validateRun(lv, start, end); !e.empty()) return fail(e);
    if (auto v = validateAmount(amount); !v.ok) return v;

    Level w = lv;
    std::vector<std::string> notes;
    long long generalChange = 0;
    if (auto err = changeCount(w, &Data::runs, runKey(start, end), -static_cast<long long>(amount), scope, notes, generalChange); !err.empty()) return fail(err);

    if (opt.runsCountAsAttempts) moveGdAttempts(w, generalChange, notes);

    lv = std::move(w);
    return success(std::move(notes));
}

Result setRuns(Level& lv, Options const& opt, Scope scope, int start, int end, int target) {
    if (auto e = validateRun(lv, start, end); !e.empty()) return fail(e);
    if (target < 0) return fail("Count can't be negative");
    Data* d = dataFor(lv, scope);
    if (!d) return fail("Session not found");
    auto it = d->runs.find(runKey(start, end));
    long long cur = it == d->runs.end() ? 0 : it->second;
    long long diff = static_cast<long long>(target) - cur;
    if (diff == 0) return success({"Already " + str(cur)});
    if (std::llabs(diff) > kMaxCount) return fail("Change is too large");
    return diff > 0 ? addRuns(lv, opt, scope, start, end, static_cast<int>(diff))
                    : removeRuns(lv, opt, scope, start, end, static_cast<int>(-diff));
}

/* new bests
============= */
Result addNewBest(Level& lv, Scope scope, int percent) {
    if (!validPercent(lv, percent)) return fail("Percent must be 0-" + str(lv.maxPercent));
    Data* d = dataFor(lv, scope);
    if (!d) return fail("Session not found");
    auto it = d->deaths.find(percentKey(percent));
    if (it == d->deaths.end() || it->second <= 0)
        return fail("A new best at " + str(percent) + "% needs a death there first - use Add death (it marks new bests by itself)");
    if (!d->newBests.insert(percent).second) return success({"Already a new best"});
    return success();
}

Result removeNewBest(Level& lv, Scope scope, int percent) {
    if (!validPercent(lv, percent)) return fail("Percent must be 0-" + str(lv.maxPercent));
    Data* d = dataFor(lv, scope);
    if (!d) return fail("Session not found");
    if (d->newBests.erase(percent) == 0) return success({"Wasn't a new best"});
    return success();
}

/* geometry dash side
====================== */
Result setGdAttempts(Level& lv, long long value, bool force) {
    if (value < 0) return fail("Attempts can't be negative");
    if (value > kMaxAttempts) return fail("Attempts are limited to " + str(kMaxAttempts));
    long long tracked = totalOf(lv.general.deaths);
    std::vector<std::string> notes;
    if (value < tracked) {
        if (!force)
            return fail("GD attempts (" + str(value) + ") can't be below the " + str(tracked) + " from-0 deaths Death Tracker has for this level. "
                        "Remove those deaths first, or force it");
        notes.push_back("Forced: GD attempts are now below the tracked deaths (" + str(tracked) + ")");
    }
    if (lv.gdAttempts != value) notes.push_back("GD attempts " + str(lv.gdAttempts) + " -> " + str(value));
    lv.gdAttempts = static_cast<int>(value);
    return success(std::move(notes));
}

Result setGdBest(Level& lv, int value, bool force) {
    if (lv.platformer) return fail("Platformer levels have no best percent");
    if (!validGdPercent(value)) return fail("Percent must be 0-100");
    int highest = highestDeathPercent(lv.general, 100);
    std::vector<std::string> notes;
    if (value < highest) {
        if (!force) return fail("GD best (" + str(value) + "%) can't be below a tracked death at " + str(highest) + "%. Remove that death first, or force it");
        notes.push_back("Forced: GD best is below a tracked death at " + str(highest) + "%");
    }
    // personal bests chain: drop what is above the new best, make sure it ends on the new best
    std::vector<int> chain;
    for (int p : lv.gdBestChain)
        if (p <= value) chain.push_back(p);
    if (value > 0 && (chain.empty() || chain.back() < value)) chain.push_back(value);
    if (lv.gdBest != value) notes.push_back("GD best " + str(lv.gdBest) + "% -> " + str(value) + "%");
    if (value == 100) notes.push_back("100% marks the level as completed in normal mode");
    lv.gdBestChain = std::move(chain);
    lv.gdBest = value;
    return success(std::move(notes));
}

Result setGdPracticeBest(Level& lv, int value) {
    if (lv.platformer) return fail("Platformer levels have no best percent");
    if (!validGdPercent(value)) return fail("Percent must be 0-100");
    lv.gdPracticeBest = value;
    return success();
}

/* sessions
============ */
Result createSession(Level& lv, long long startDate) {
    if (startDate <= 0) return fail("Invalid session date");
    if (lv.sessions.count(startDate)) return fail("A session with that date already exists");
    lv.sessions[startDate] = Data{};
    return success();
}

Result deleteSession(Level& lv, Options const& opt, long long startDate, bool alsoRemoveFromTotals) {
    auto it = lv.sessions.find(startDate);
    if (it == lv.sessions.end()) return fail("Session not found");

    Level w = lv;
    std::vector<std::string> notes;
    Data removed = std::move(w.sessions[startDate]);
    w.sessions.erase(startDate);

    if (alsoRemoveFromTotals) {
        long long deathsRemoved = 0;
        for (auto const& [key, count] : removed.deaths) {
            if (count <= 0) continue;
            auto git = w.general.deaths.find(key);
            if (git == w.general.deaths.end()) continue;
            int cut = std::min(git->second, count);
            git->second -= cut;
            deathsRemoved += cut;
            if (git->second <= 0) {
                w.general.deaths.erase(git);
                int p;
                if (parsePercentKey(key, p)) pruneBestIfNoDeaths(w, p);
            }
        }
        for (auto const& [key, count] : removed.runs) {
            if (count <= 0) continue;
            auto git = w.general.runs.find(key);
            if (git == w.general.runs.end()) continue;
            git->second -= std::min(git->second, count);
            if (git->second <= 0) w.general.runs.erase(git);
        }
        for (int k = 0; k < 3; k++)
            for (int m = 0; m < 2; m++)
                w.general.pt[k][m] -= std::min(w.general.pt[k][m], removed.pt[k][m]);

        notes.push_back("Removed the session's deaths, runs and playtime from the lifetime totals");
        if (opt.linkGdAttempts) moveGdAttempts(w, -deathsRemoved, notes);
    } else {
        notes.push_back("Lifetime totals keep this session's deaths");
    }

    lv = std::move(w);
    return success(std::move(notes));
}

Result moveSession(Level& lv, long long fromDate, long long toDate) {
    if (toDate <= 0) return fail("Invalid session date");
    auto it = lv.sessions.find(fromDate);
    if (it == lv.sessions.end()) return fail("Session not found");
    if (fromDate == toDate) return success({"Same date"});
    if (lv.sessions.count(toDate)) return fail("A session with that date already exists");
    lv.sessions[toDate] = std::move(it->second);
    lv.sessions.erase(fromDate);
    return success();
}

/* playtime
============ */
Result setPlaytime(Level& lv, Scope scope, PtKind kind, PtMode mode, uint64_t nanoseconds) {
    if (nanoseconds > kMaxPlaytimeNs) return fail("Playtime is limited to 100000 hours");
    Data* d = dataFor(lv, scope);
    if (!d) return fail("Session not found");

    std::vector<std::string> notes;
    if (!scope.has_value()) {
        uint64_t sessSum = 0;
        for (auto const& [_, s] : lv.sessions) sessSum += s.pt[kind][mode];
        if (nanoseconds < sessSum) return fail("Lifetime playtime can't be below the sum of its sessions - edit the sessions first");
        d->pt[kind][mode] = nanoseconds;
        return success();
    }

    uint64_t old = d->pt[kind][mode];
    d->pt[kind][mode] = nanoseconds;
    uint64_t& g = lv.general.pt[kind][mode];
    if (nanoseconds >= old) {
        g += nanoseconds - old;
    } else {
        uint64_t cut = old - nanoseconds;
        if (g < cut) {
            notes.push_back("Lifetime playtime was lower than the session's (inconsistent data), clamped to 0");
            g = 0;
        } else {
            g -= cut;
        }
    }
    return success(std::move(notes));
}

/* consistency
=============== */
static void checkData(Level const& lv, Data const& d, std::string const& label, std::vector<Issue>& out) {
    for (auto const& [key, count] : d.deaths) {
        int p;
        if (!parsePercentKey(key, p) || !validPercent(lv, p)) out.push_back({label + ": invalid death entry '" + key + "'", false});
        else if (count <= 0) out.push_back({label + ": empty death entry at " + key + "%", true});
    }
    for (auto const& [key, count] : d.runs) {
        int s, e;
        if (!parseRunKey(key, s, e) || !validPercent(lv, s) || !validPercent(lv, e) || s > e) out.push_back({label + ": invalid run entry '" + key + "'", false});
        else if (count <= 0) out.push_back({label + ": empty run entry " + key, true});
    }
    for (int p : d.newBests) {
        if (!validPercent(lv, p)) {
            out.push_back({label + ": new best out of range (" + str(p) + ")", true});
            continue;
        }
        auto it = d.deaths.find(percentKey(p));
        if (it == d.deaths.end() || it->second <= 0) out.push_back({label + ": new best at " + str(p) + "% has no death there", true});
    }
}

std::vector<Issue> verify(Level const& lv) {
    std::vector<Issue> out;
    checkData(lv, lv.general, "Lifetime", out);
    for (auto const& [date, data] : lv.sessions) checkData(lv, data, "Session " + str(date), out);

    // sessions are part of the lifetime totals
    std::set<std::string> deathKeys, runKeys;
    for (auto const& [_, s] : lv.sessions) {
        for (auto const& [k, c] : s.deaths) if (c > 0) deathKeys.insert(k);
        for (auto const& [k, c] : s.runs) if (c > 0) runKeys.insert(k);
    }
    for (auto const& k : deathKeys) {
        long long sum = sessionsSum(lv, &Data::deaths, k);
        auto it = lv.general.deaths.find(k);
        long long have = it == lv.general.deaths.end() ? 0 : it->second;
        if (sum > have) out.push_back({"Sessions have " + str(sum) + " deaths at " + k + "% but lifetime only " + str(have), true});
    }
    for (auto const& k : runKeys) {
        long long sum = sessionsSum(lv, &Data::runs, k);
        auto it = lv.general.runs.find(k);
        long long have = it == lv.general.runs.end() ? 0 : it->second;
        if (sum > have) out.push_back({"Sessions have " + str(sum) + " runs " + k + " but lifetime only " + str(have), true});
    }
    for (int k = 0; k < 3; k++) {
        for (int m = 0; m < 2; m++) {
            uint64_t sum = 0;
            for (auto const& [_, s] : lv.sessions) sum += s.pt[k][m];
            if (sum > lv.general.pt[k][m]) out.push_back({"Sessions have more playtime than the lifetime totals (kind " + str(k) + ", mode " + str(m) + ")", true});
        }
    }

    long long tracked = totalOf(lv.general.deaths);
    if (lv.gdAttempts < tracked) out.push_back({"GD attempts (" + str(lv.gdAttempts) + ") are below the tracked from-0 deaths (" + str(tracked) + ")", true});

    if (lv.platformer) return out;

    int highest = highestDeathPercent(lv.general, 100);
    if (lv.gdBest < highest) out.push_back({"GD best (" + str(lv.gdBest) + "%) is below a tracked death at " + str(highest) + "%", true});
    if (!validGdPercent(lv.gdBest)) out.push_back({"GD best is out of range", true});
    if (!validGdPercent(lv.gdPracticeBest)) out.push_back({"GD practice best is out of range", true});

    bool chainBad = false;
    int prev = 0;
    for (int p : lv.gdBestChain) {
        if (p <= prev || p > lv.gdBest) chainBad = true;
        prev = p;
    }
    if (chainBad) out.push_back({"GD personal bests list is out of order or above the GD best", true});
    return out;
}

static void fixData(Level const& lv, Data& d, std::vector<std::string>& notes, std::string const& label) {
    size_t before = d.deaths.size() + d.runs.size();
    for (auto it = d.deaths.begin(); it != d.deaths.end();) it = it->second <= 0 ? d.deaths.erase(it) : std::next(it);
    for (auto it = d.runs.begin(); it != d.runs.end();) it = it->second <= 0 ? d.runs.erase(it) : std::next(it);
    if (d.deaths.size() + d.runs.size() != before) notes.push_back(label + ": removed empty entries");

    size_t bests = d.newBests.size();
    for (auto it = d.newBests.begin(); it != d.newBests.end();) {
        auto dit = d.deaths.find(percentKey(*it));
        bool bad = !validPercent(lv, *it) || dit == d.deaths.end() || dit->second <= 0;
        it = bad ? d.newBests.erase(it) : std::next(it);
    }
    if (d.newBests.size() != bests) notes.push_back(label + ": removed new bests without a death");
}

Result autoFix(Level& lv) {
    Level w = lv;
    std::vector<std::string> notes;

    fixData(w, w.general, notes, "Lifetime");
    for (auto& [date, data] : w.sessions) fixData(w, data, notes, "Session " + str(date));

    auto raiseTotals = [&](Counts Data::* field, char const* what) {
        std::set<std::string> keys;
        for (auto const& [_, s] : w.sessions)
            for (auto const& [k, c] : s.*field) if (c > 0) keys.insert(k);
        for (auto const& k : keys) {
            long long sum = sessionsSum(w, field, k);
            int& have = (w.general.*field)[k];
            if (sum > have) {
                have = static_cast<int>(std::min<long long>(sum, kMaxCount));
                notes.push_back(std::string("Raised lifetime ") + what + " " + k + " to " + str(have) + " to match the sessions");
            }
        }
    };
    raiseTotals(&Data::deaths, "deaths");
    raiseTotals(&Data::runs, "runs");

    for (int k = 0; k < 3; k++) {
        for (int m = 0; m < 2; m++) {
            uint64_t sum = 0;
            for (auto const& [_, s] : w.sessions) sum += s.pt[k][m];
            if (sum > w.general.pt[k][m]) {
                w.general.pt[k][m] = sum;
                notes.push_back("Raised lifetime playtime to match the sessions");
            }
        }
    }

    long long tracked = totalOf(w.general.deaths);
    if (w.gdAttempts < tracked) {
        notes.push_back("GD attempts " + str(w.gdAttempts) + " -> " + str(tracked));
        w.gdAttempts = static_cast<int>(std::min<long long>(tracked, kMaxAttempts));
    }

    if (w.platformer) {
        if (notes.empty()) notes.push_back("Nothing to fix");
        lv = std::move(w);
        return success(std::move(notes));
    }

    int highest = highestDeathPercent(w.general, 100);
    w.gdBest = std::clamp(w.gdBest, 0, 100);
    w.gdPracticeBest = std::clamp(w.gdPracticeBest, 0, 100);
    if (w.gdBest < highest) {
        notes.push_back("GD best " + str(w.gdBest) + "% -> " + str(highest) + "%");
        w.gdBest = highest;
    }

    std::vector<int> chain = w.gdBestChain;
    std::sort(chain.begin(), chain.end());
    chain.erase(std::unique(chain.begin(), chain.end()), chain.end());
    chain.erase(std::remove_if(chain.begin(), chain.end(), [&](int p) { return p <= 0 || p > w.gdBest; }), chain.end());
    if (w.gdBest > 0 && (chain.empty() || chain.back() < w.gdBest)) chain.push_back(w.gdBest);
    if (chain != w.gdBestChain) {
        notes.push_back("Rebuilt the GD personal bests list");
        w.gdBestChain = std::move(chain);
    }

    if (notes.empty()) notes.push_back("Nothing to fix");
    lv = std::move(w);
    return success(std::move(notes));
}

Result removeInvalidEntries(Level& lv) {
    Level w = lv;
    std::vector<std::string> notes;

    auto clean = [&](Data& d, std::string const& label) {
        size_t removed = 0;
        for (auto it = d.deaths.begin(); it != d.deaths.end();) {
            int p;
            bool bad = !parsePercentKey(it->first, p) || !validPercent(w, p);
            if (bad) { it = d.deaths.erase(it); removed++; }
            else ++it;
        }
        for (auto it = d.runs.begin(); it != d.runs.end();) {
            int s, e;
            bool bad = !parseRunKey(it->first, s, e) || !validPercent(w, s) || !validPercent(w, e) || s > e;
            if (bad) { it = d.runs.erase(it); removed++; }
            else ++it;
        }
        for (auto it = d.newBests.begin(); it != d.newBests.end();) {
            if (!validPercent(w, *it)) { it = d.newBests.erase(it); removed++; }
            else ++it;
        }
        if (removed) notes.push_back(label + ": removed " + str(static_cast<long long>(removed)) + " invalid entries");
    };

    clean(w.general, "Lifetime");
    for (auto& [date, data] : w.sessions) clean(data, "Session " + str(date));

    if (notes.empty()) notes.push_back("No invalid entries found");
    lv = std::move(w);
    return success(std::move(notes));
}

} // namespace dted
