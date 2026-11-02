#include "DecisionCapture.h"

#include "../ai/playerbot/PlayerbotAIConfig.h"
#include "../host/ModuleVersion.h"

#include <filesystem>

namespace TortoiseBots
{

// RunTraceService::DirPath's rule: an absolute AiPlayerbot.DecisionCapture.Dir
// stands as written, a relative one is under LogsDir.
static std::string ResolvedDir(std::string const& dir)
{
    std::string const wanted = dir.empty() ? std::string("decisions") : dir;
    std::filesystem::path const path(wanted);
    if (path.is_absolute())
        return wanted;
    return sPlayerbotAIConfig.logsDirPath() + wanted;
}

DecisionCaptureSettings DecisionCaptureSettingsFromConfig()
{
    DecisionCaptureSettings settings;
    settings.enabled = sPlayerbotAIConfig.decisionCapture;
    settings.dir = ResolvedDir(sPlayerbotAIConfig.decisionCaptureDir);
    settings.module = BuildVersion();
    settings.segmentMiB = sPlayerbotAIConfig.decisionCaptureSegmentMiB;
    settings.captureMiB = sPlayerbotAIConfig.decisionCaptureCaptureMiB;
    settings.totalMiB = sPlayerbotAIConfig.decisionCaptureTotalMiB;
    settings.recordKiB = sPlayerbotAIConfig.decisionCaptureRecordKiB;
    settings.keyframeEvery = sPlayerbotAIConfig.decisionCaptureKeyframeEvery;
    return settings;
}

DecisionCaptureService::DecisionCaptureService() : registry_(DecisionCaptureSettingsFromConfig())
{
}

DecisionCaptureService& DecisionCaptureService::Instance()
{
    static DecisionCaptureService service;
    return service;
}

} // namespace TortoiseBots
