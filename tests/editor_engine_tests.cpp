// Standalone tests for the Editor Mode rules engine (no Geode needed).
// Build: see tests/run_tests.bat

#include "../src/editor/EditorEngine.hpp"

#include <cstdio>
#include <cstdlib>

using namespace dted;

static int g_failed = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        g_checks++;                                                                  \
        if (!(cond)) {                                                               \
            g_failed++;                                                              \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
        }                                                                            \
    } while (0)

static int count(Counts const& c, std::string const& k) {
    auto it = c.find(k);
    return it == c.end() ? 0 : it->second;
}

// a level that looks like a real one: 2500 GD attempts, 1200 tracked, one session
static Level sample() {
    Level lv;
    lv.gdAttempts = 2500;
    lv.gdBest = 60;
    lv.gdBestChain = {10, 30, 60};
    lv.general.deaths = {{"10", 400}, {"30", 500}, {"60", 300}};
    lv.general.newBests = {10, 30, 60};
    lv.general.runs = {{"20-40", 50}};
    lv.sessions[1000].deaths = {{"10", 100}, {"30", 50}};
    lv.sessions[1000].newBests = {10, 30};
    lv.sessions[1000].runs = {{"20-40", 20}};
    lv.sessions[2000].deaths = {{"60", 30}, {"30", 10}};
    lv.sessions[2000].newBests = {60};
    return lv;
}

static void testAddDeathNewBest() {
    Level lv = sample();
    Options o;
    // above the best: new best everywhere it applies, GD attempts +1, GD best raised
    auto r = addDeaths(lv, o, std::nullopt, 75, 1);
    CHECK(r.ok);
    CHECK(count(lv.general.deaths, "75") == 1);
    CHECK(lv.general.newBests.count(75) == 1);
    CHECK(lv.gdAttempts == 2501);
    CHECK(lv.gdBest == 75);
    CHECK(lv.gdBestChain.back() == 75);
    CHECK(!r.notes.empty());

    // below the best: plain death, no new best, GD best untouched
    r = addDeaths(lv, o, std::nullopt, 20, 3);
    CHECK(r.ok);
    CHECK(count(lv.general.deaths, "20") == 3);
    CHECK(lv.general.newBests.count(20) == 0);
    CHECK(lv.gdAttempts == 2504);
    CHECK(lv.gdBest == 75);
}

static void testAddDeathInSession() {
    Level lv = sample();
    Options o;
    auto r = addDeaths(lv, o, Scope(1000), 45, 2);
    CHECK(r.ok);
    // session and lifetime both get it
    CHECK(count(lv.sessions[1000].deaths, "45") == 2);
    CHECK(count(lv.general.deaths, "45") == 2);
    // 45 beats the best of session 1000 (30) but not the lifetime best (60)
    CHECK(lv.sessions[1000].newBests.count(45) == 1);
    CHECK(lv.general.newBests.count(45) == 0);
    CHECK(lv.gdAttempts == 2502);
    CHECK(lv.gdBest == 60);
    CHECK(verify(lv).empty());

    // unknown session
    Level before = lv;
    r = addDeaths(lv, o, Scope(999), 10, 1);
    CHECK(!r.ok);
    CHECK(lv.general == before.general && lv.gdAttempts == before.gdAttempts);
}

static void testCompletionNotPushedToGd() {
    Level lv = sample();
    Options o;
    auto r = addDeaths(lv, o, std::nullopt, 100, 1);
    CHECK(r.ok);
    CHECK(lv.general.newBests.count(100) == 1);
    CHECK(lv.gdBest == 60); // GD completion must not change
    CHECK(lv.gdAttempts == 2501);
}

static void testLinkOff() {
    Level lv = sample();
    Options o;
    o.linkGdAttempts = false;
    o.syncGdBest = false;
    auto r = addDeaths(lv, o, std::nullopt, 90, 5);
    CHECK(r.ok);
    CHECK(lv.gdAttempts == 2500);
    CHECK(lv.gdBest == 60);
}

static void testRemoveCascadesToSessions() {
    // removing every death at the best percent from the lifetime totals: session 2000 holds 30 of
    // them, so they are taken out of the session too (sessions must never exceed lifetime)
    Level lv = sample();
    Options o;
    auto r = removeDeaths(lv, o, std::nullopt, 60, 300);
    CHECK(r.ok);
    CHECK(count(lv.general.deaths, "60") == 0);
    CHECK(count(lv.sessions[2000].deaths, "60") == 0);   // cascaded
    CHECK(lv.general.newBests.count(60) == 0);           // pruned
    CHECK(lv.sessions[2000].newBests.count(60) == 0);    // pruned
    CHECK(lv.gdAttempts == 2200);
    CHECK(lv.gdBest == 60);                              // never lowered silently
    CHECK(verify(lv).empty());                           // GD best above the highest tracked death is fine
}

static void testRemoveTooMany() {
    Level lv = sample();
    Options o;
    Level before = lv;
    auto r = removeDeaths(lv, o, Scope(1000), 10, 101);
    CHECK(!r.ok);
    CHECK(lv.general == before.general);
    CHECK(lv.sessions == before.sessions);
    CHECK(lv.gdAttempts == before.gdAttempts);
    r = removeDeaths(lv, o, std::nullopt, 33, 1);
    CHECK(!r.ok);
}

static void testRemoveFromSessionKeepsLifetimeConsistent() {
    Level lv = sample();
    Options o;
    auto r = removeDeaths(lv, o, Scope(1000), 10, 40);
    CHECK(r.ok);
    CHECK(count(lv.sessions[1000].deaths, "10") == 60);
    CHECK(count(lv.general.deaths, "10") == 360);
    CHECK(lv.gdAttempts == 2460);
    CHECK(verify(lv).empty());
}

static void testGdAttemptsFloor() {
    Level lv = sample();
    lv.gdAttempts = 1200; // exactly the tracked deaths (400+500+300)
    Options o;
    auto r = removeDeaths(lv, o, std::nullopt, 10, 100);
    CHECK(r.ok);
    CHECK(lv.gdAttempts == 1100);
    // slack is preserved
    lv = sample();
    lv.gdAttempts = 1250;
    r = removeDeaths(lv, o, std::nullopt, 10, 100);
    CHECK(lv.gdAttempts == 1150);
}

static void testSetDeaths() {
    Level lv = sample();
    Options o;
    auto r = setDeaths(lv, o, std::nullopt, 30, 520);
    CHECK(r.ok);
    CHECK(count(lv.general.deaths, "30") == 520);
    CHECK(lv.gdAttempts == 2520);
    r = setDeaths(lv, o, std::nullopt, 30, 520);
    CHECK(r.ok && lv.gdAttempts == 2520); // no change
    r = setDeaths(lv, o, std::nullopt, 30, -1);
    CHECK(!r.ok);
    r = setDeaths(lv, o, std::nullopt, 101, 1);
    CHECK(!r.ok);
}

static void testRuns() {
    Level lv = sample();
    Options o;
    auto r = addRuns(lv, o, Scope(1000), 10, 50, 4);
    CHECK(r.ok);
    CHECK(count(lv.sessions[1000].runs, "10-50") == 4);
    CHECK(count(lv.general.runs, "10-50") == 4);
    CHECK(lv.gdAttempts == 2500); // runs don't move GD attempts by default
    o.runsCountAsAttempts = true;
    r = addRuns(lv, o, std::nullopt, 10, 50, 2);
    CHECK(lv.gdAttempts == 2502);
    r = addRuns(lv, o, std::nullopt, 50, 10, 1);
    CHECK(!r.ok);
    r = removeRuns(lv, o, Scope(1000), 10, 50, 5);
    CHECK(!r.ok);
    r = setRuns(lv, o, std::nullopt, 10, 50, 0);
    CHECK(r.ok);
    CHECK(lv.general.runs.count("10-50") == 0);
    CHECK(lv.sessions[1000].runs.count("10-50") == 0); // cascaded, still consistent
    CHECK(verify(lv).empty());
}

static void testNewBestRules() {
    Level lv = sample();
    auto r = addNewBest(lv, std::nullopt, 45); // no death at 45
    CHECK(!r.ok);
    addDeaths(lv, Options{}, std::nullopt, 45, 1);
    r = addNewBest(lv, std::nullopt, 45);
    CHECK(r.ok); // already a best? 45 < 60 so not auto-added -> now added manually
    CHECK(lv.general.newBests.count(45) == 1);
    r = removeNewBest(lv, std::nullopt, 45);
    CHECK(r.ok && lv.general.newBests.count(45) == 0);
}

static void testGdFields() {
    Level lv = sample();
    auto r = setGdAttempts(lv, 3000, false);
    CHECK(r.ok && lv.gdAttempts == 3000);
    r = setGdAttempts(lv, 1000, false); // tracked = 1200
    CHECK(!r.ok && lv.gdAttempts == 3000);
    r = setGdAttempts(lv, 1000, true);
    CHECK(r.ok && lv.gdAttempts == 1000);
    CHECK(!verify(lv).empty());
    r = autoFix(lv);
    CHECK(r.ok && lv.gdAttempts == 1200);
    CHECK(verify(lv).empty());
    r = setGdAttempts(lv, -5, true);
    CHECK(!r.ok);
    r = setGdAttempts(lv, 3000000000LL, true);
    CHECK(!r.ok);

    r = setGdBest(lv, 40, false); // deaths tracked at 60
    CHECK(!r.ok);
    r = setGdBest(lv, 80, false);
    CHECK(r.ok && lv.gdBest == 80);
    CHECK(lv.gdBestChain.back() == 80);
    r = setGdBest(lv, 60, false);
    CHECK(r.ok && lv.gdBest == 60);
    CHECK((lv.gdBestChain == std::vector<int>{10, 30, 60}));
    r = setGdBest(lv, 101, true);
    CHECK(!r.ok);
    r = setGdPracticeBest(lv, 55);
    CHECK(r.ok && lv.gdPracticeBest == 55);
    r = setGdPracticeBest(lv, 200);
    CHECK(!r.ok);
}

static void testPersonalBestsCodec() {
    auto chain = decodeBests("10,20,30");
    CHECK(chain.has_value());
    CHECK((*chain == std::vector<int>{10, 30, 60}));
    CHECK(encodeBests({10, 30, 60}) == "10,20,30");
    CHECK(decodeBests("").has_value() && decodeBests("")->empty());
    CHECK(!decodeBests("abc").has_value());
    CHECK(!decodeBests("60,60").has_value()); // sums above 100
    CHECK(encodeBests({}) == "");
}

static void testSessions() {
    Level lv = sample();
    Options o;
    auto r = createSession(lv, 1000);
    CHECK(!r.ok);
    r = createSession(lv, 3000);
    CHECK(r.ok && lv.sessions.count(3000) == 1);
    r = moveSession(lv, 3000, 2000);
    CHECK(!r.ok);
    r = moveSession(lv, 3000, 4000);
    CHECK(r.ok && lv.sessions.count(4000) == 1 && lv.sessions.count(3000) == 0);

    // delete but keep lifetime totals
    Level keep = lv;
    r = deleteSession(keep, o, 1000, false);
    CHECK(r.ok && keep.sessions.count(1000) == 0);
    CHECK(count(keep.general.deaths, "10") == 400);
    CHECK(keep.gdAttempts == 2500);

    // delete and subtract from lifetime
    r = deleteSession(lv, o, 1000, true);
    CHECK(r.ok);
    CHECK(count(lv.general.deaths, "10") == 300);
    CHECK(count(lv.general.deaths, "30") == 450);
    CHECK(lv.gdAttempts == 2350);
    CHECK(count(lv.general.runs, "20-40") == 30);
    r = deleteSession(lv, o, 12345, true);
    CHECK(!r.ok);
}

static void testPlaytime() {
    Level lv = sample();
    lv.general.pt[PtPlay][PtFrom0] = 1000;
    lv.sessions[1000].pt[PtPlay][PtFrom0] = 400;
    lv.sessions[2000].pt[PtPlay][PtFrom0] = 300;

    auto r = setPlaytime(lv, std::nullopt, PtPlay, PtFrom0, 600); // < 700
    CHECK(!r.ok);
    r = setPlaytime(lv, std::nullopt, PtPlay, PtFrom0, 900);
    CHECK(r.ok && lv.general.pt[PtPlay][PtFrom0] == 900);
    // growing a session grows lifetime by the same amount
    r = setPlaytime(lv, Scope(1000), PtPlay, PtFrom0, 450);
    CHECK(r.ok && lv.general.pt[PtPlay][PtFrom0] == 950);
    // shrinking a session shrinks lifetime
    r = setPlaytime(lv, Scope(1000), PtPlay, PtFrom0, 100);
    CHECK(r.ok && lv.general.pt[PtPlay][PtFrom0] == 600);
    r = setPlaytime(lv, Scope(777), PtPlay, PtFrom0, 1);
    CHECK(!r.ok);
    r = setPlaytime(lv, std::nullopt, PtPlay, PtFrom0, kMaxPlaytimeNs + 1);
    CHECK(!r.ok);
}

static void testVerifyAndFix() {
    Level lv = sample();
    CHECK(verify(lv).empty());

    lv.general.deaths["10"] = 50;            // sessions have 100 at 10%
    lv.general.deaths["77"] = 0;             // empty entry
    lv.general.newBests.insert(88);          // best without death
    lv.gdBest = 20;                          // below tracked death at 60
    lv.gdBestChain = {30, 10, 60};           // unordered
    auto issues = verify(lv);
    CHECK(issues.size() >= 5);
    bool anyUnfixable = false;
    for (auto const& i : issues) anyUnfixable |= !i.fixable;
    CHECK(!anyUnfixable);

    auto r = autoFix(lv);
    CHECK(r.ok);
    CHECK(verify(lv).empty());
    CHECK(count(lv.general.deaths, "10") == 100);
    CHECK(lv.general.deaths.count("77") == 0);
    CHECK(lv.general.newBests.count(88) == 0);
    CHECK(lv.gdBest == 60);
    CHECK((lv.gdBestChain == std::vector<int>{10, 30, 60}));

    lv.general.deaths["abc"] = 3;
    lv.general.runs["50-10"] = 2;       // inverted run
    lv.sessions[1000].deaths["250"] = 1; // above 100
    issues = verify(lv);
    bool hasUnfixable = false;
    for (auto const& i : issues) hasUnfixable |= !i.fixable;
    CHECK(hasUnfixable); // garbage keys are reported, never silently deleted
    r = autoFix(lv);
    CHECK(lv.general.deaths.count("abc") == 1);

    // ...but they can be removed on explicit request
    r = removeInvalidEntries(lv);
    CHECK(r.ok);
    CHECK(lv.general.deaths.count("abc") == 0);
    CHECK(lv.general.runs.count("50-10") == 0);
    CHECK(lv.sessions[1000].deaths.count("250") == 0);
    CHECK(count(lv.general.deaths, "10") == 100); // valid data untouched
    r = removeInvalidEntries(lv);
    CHECK(r.ok && r.notes.size() == 1 && r.notes[0] == "No invalid entries found");
}

static void testKeyParsing() {
    int a, b;
    CHECK(parsePercentKey("37", a) && a == 37);
    CHECK(!parsePercentKey("3x", a));
    CHECK(!parsePercentKey("", a));
    CHECK(parseRunKey("10-30", a, b) && a == 10 && b == 30);
    CHECK(parseRunKey("-5-30", a, b) && a == -5 && b == 30);
    CHECK(!parseRunKey("10", a, b));
    CHECK(!parseRunKey("a-b", a, b));
}

static void testLimits() {
    Level lv = sample();
    Options o;
    CHECK(!addDeaths(lv, o, std::nullopt, 10, 0).ok);
    CHECK(!addDeaths(lv, o, std::nullopt, 10, -4).ok);
    CHECK(!addDeaths(lv, o, std::nullopt, -1, 1).ok);
    CHECK(!addDeaths(lv, o, std::nullopt, 101, 1).ok);
    CHECK(!addDeaths(lv, o, std::nullopt, 10, kMaxCount + 1).ok);
    // attempts clamp instead of overflowing
    lv.gdAttempts = static_cast<int>(kMaxAttempts) - 1;
    auto r = addDeaths(lv, o, std::nullopt, 10, 5);
    CHECK(r.ok && lv.gdAttempts == static_cast<int>(kMaxAttempts));
}

static void testPlatformer() {
    Level lv;
    lv.platformer = true;
    lv.maxPercent = 1000; // checkpoints, not percent
    lv.gdAttempts = 300;
    lv.gdBest = 0;
    Options o;

    auto r = addDeaths(lv, o, std::nullopt, 250, 2);
    CHECK(r.ok);
    CHECK(count(lv.general.deaths, "250") == 2);
    CHECK(lv.gdAttempts == 302);
    CHECK(lv.gdBest == 0);          // platformers never touch GD best
    CHECK(lv.gdBestChain.empty());
    CHECK(!addDeaths(lv, o, std::nullopt, 1001, 1).ok);
    CHECK(!setGdBest(lv, 50, false).ok);
    CHECK(!setGdPracticeBest(lv, 50).ok);
    CHECK(verify(lv).empty());
    CHECK(autoFix(lv).ok);
}

int main() {
    testPlatformer();
    testAddDeathNewBest();
    testAddDeathInSession();
    testCompletionNotPushedToGd();
    testLinkOff();
    testRemoveCascadesToSessions();
    testRemoveTooMany();
    testRemoveFromSessionKeepsLifetimeConsistent();
    testGdAttemptsFloor();
    testSetDeaths();
    testRuns();
    testNewBestRules();
    testGdFields();
    testPersonalBestsCodec();
    testSessions();
    testPlaytime();
    testVerifyAndFix();
    testKeyParsing();
    testLimits();

    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
