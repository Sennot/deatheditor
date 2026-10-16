#include <utils/Settings.hpp>

int64_t Settings::getMaxSessionLength() {
    auto sessionMethod = Mod::get()->getSettingValue<std::string>("session-method");
    if (sessionMethod == "Seconds"){
        sessionMethod = "Exit game";
        Mod::get()->setSettingValue("session-method", sessionMethod);
    }

    if (sessionMethod == "Exit level")
        return -2;
    else if (sessionMethod == "Exit game")
        return -1;
    else
        return Mod::get()->getSettingValue<int64_t>("session-length");
}

bool Settings::isCompletedLevelTrackingDisabled() {
    return Mod::get()->getSettingValue<bool>("disable-tracking-completed-levels");
}

bool Settings::getPauseMenuEnabled(){
    return Mod::get()->getSettingValue<bool>("pause-menu");
}

bool Settings::getCompleteMenuEnabled(){
    return Mod::get()->getSettingValue<bool>("complete-menu");
}

bool Settings::getQuickSaveEnabled(){
    return Mod::get()->getSettingValue<bool>("quick-save");
}

float Settings::getGraphPointSize(){
    return static_cast<float>(Mod::get()->getSettingValue<double>("graph-point-size"));
}

std::filesystem::path Settings::getSavePath(){
    auto path = Mod::get()->getSettingValue<std::filesystem::path>("save-path-new");

    if (!Mod::get()->getSettingValue<bool>("editor-share-original-saves")) return path;

    // called for every file operation, so the folder check is cached per setting value
    static std::filesystem::path lastInput;
    static std::filesystem::path lastOutput;
    if (!lastOutput.empty() && lastInput == path) return lastOutput;

    auto result = path;
    if (path.lexically_normal() == (Mod::get()->getSaveDir() / "levels").lexically_normal()) {
        // the original Death Tracker keeps its data in a sibling folder named after its id
        auto original = Mod::get()->getSaveDir().parent_path() / "elohmrow.death_tracker" / "levels";
        std::error_code ec;
        if (std::filesystem::is_directory(original, ec)) result = original;
    }

    lastInput = path;
    lastOutput = result;
    return result;
}

bool Settings::getEditorMode(){
    return Mod::get()->getSettingValue<bool>("editor-mode");
}

bool Settings::getEditorLinkGdAttempts(){
    return Mod::get()->getSettingValue<bool>("editor-link-gd-attempts");
}

bool Settings::getEditorSyncGdBest(){
    return Mod::get()->getSettingValue<bool>("editor-sync-gd-best");
}

bool Settings::getEditorRunsCountAsAttempts(){
    return Mod::get()->getSettingValue<bool>("editor-runs-count-as-attempts");
}

bool Settings::getEditorAutoBackup(){
    return Mod::get()->getSettingValue<bool>("editor-auto-backup");
}

std::optional<int> Settings::getMaxBackupAmount(){
    if (Mod::get()->getSettingValue<bool>("backups-unlimited")) return std::nullopt;

    return Mod::get()->getSettingValue<int>("backups-max-amount");
}

bool Settings::getAutoBackupEnabled(){
    return Mod::get()->getSettingValue<bool>("auto-backups-enabled");
}
bool Settings::getAutoBackupGeneral(){
    return Mod::get()->getSettingValue<bool>("auto-backups-general");
}

std::optional<int> Settings::getAutoBackupSessionAmount(){
    if (Mod::get()->getSettingValue<bool>("auto-backups-sessions-all")) return -1;

    return Mod::get()->getSettingValue<int>("auto-backups-sessions-amount");
}

bool Settings::getAutoBackupAtLvlExit(){
    return Mod::get()->getSettingValue<bool>("auto-backup-at-lvl-exit");
}
bool Settings::getAutoBackupAtDTExit(){
    return Mod::get()->getSettingValue<bool>("auto-backup-at-dt-exit");
}

bool Settings::getSafeMode(){
    return Mod::get()->getSettingValue<bool>("safe-mode-v3");
}

bool Settings::getCheatDetect(){
    return Mod::get()->getSettingValue<bool>("cheat-detect");
}

bool Settings::getDisablePractice(){
    return Mod::get()->getSettingValue<bool>("disable-practice-tracking");
}

bool Settings::is12HourClockFormat(){
    auto clockFormat = Mod::get()->getSettingValue<std::string>("clock-format");

    return clockFormat == "12-hour";
}
