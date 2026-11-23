// JNI bridge for CloudRedirect Android native core.
// Package: com.cloudredirect.nativecore.CloudRedirectNative

#include "cli.h"
#include "cloud_metadata_paths.h"
#include "cloud_provider.h"
#include "cloud_provider_base.h"
#include "cloud_storage.h"
#include "log.h"
#include "xdg.h"

#include <jni.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace AndroidHttpBridge {
bool Register(JavaVM* vm, JNIEnv* env, jobject bridgeObj);
}

#ifndef CR_RELEASE_VERSION
#define CR_RELEASE_VERSION "0.0.0"
#endif

static JavaVM* g_javaVm = nullptr;

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    g_javaVm = vm;
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_cloudredirect_nativecore_CloudRedirectNative_nativeInit(
    JNIEnv* env,
    jclass /*clazz*/,
    jstring jConfigDir,
    jobject jHttpBridge) {

    if (jConfigDir) {
        const char* chars = env->GetStringUTFChars(jConfigDir, nullptr);
        if (chars) {
            AndroidConfig::SetConfigDir(chars);
            env->ReleaseStringUTFChars(jConfigDir, chars);
        }
    }

    Log::Init();

    if (!g_javaVm) {
        env->GetJavaVM(&g_javaVm);
    }

    bool ok = AndroidHttpBridge::Register(g_javaVm, env, jHttpBridge);
    if (!ok) {
        Log::Error("[JNI] Failed to register CloudRedirectHttpBridge");
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_cloudredirect_nativecore_CloudRedirectNative_nativeGetVersion(
    JNIEnv* env,
    jclass /*clazz*/) {
    return env->NewStringUTF(CR_RELEASE_VERSION);
}

static std::string DiscoverCloudGamesJson(const std::string& provider) {
    std::string configDir = AndroidConfig::GetConfigDir() + "/CloudRedirect/";
    std::string configJson;
    {
        std::ifstream ifs(configDir + "config.json", std::ios::binary);
        if (ifs.is_open()) {
            std::ostringstream ss;
            ss << ifs.rdbuf();
            configJson = ss.str();
        }
    }

    std::string tokenPath = ResolveProviderTokenPath(configDir, configJson, provider);
    auto prov = CreateCloudProvider(provider);
    if (!prov) {
        return "{\"success\":false,\"error\":\"Unknown provider\"}";
    }
    if (!prov->Init(tokenPath) || !prov->IsAuthenticated()) {
        prov->Shutdown();
        return "{\"success\":false,\"error\":\"Not authenticated\"}";
    }

    std::set<std::pair<uint32_t, uint32_t>> discovered;
    auto recordPath = [&](const std::string& path) {
        uint32_t acctId = 0;
        uint32_t appId = 0;
        std::string rel;
        if (CloudProviderBase::ParsePath(path, acctId, appId, rel) &&
            acctId > 0 &&
            appId > 0 &&
            appId != CloudProviderBase::kNoAppId) {
            discovered.emplace(acctId, appId);
        }
    };

    bool supported = false;
    auto hits = prov->SearchByName(CloudIntercept::kManifestFilename, &supported);
    if (supported) {
        for (const auto& h : hits) recordPath(h.path);
        auto legacyHits = prov->SearchByName(CloudIntercept::kLegacyManifestFilename, nullptr);
        for (const auto& h : legacyHits) recordPath(h.path);
        auto cnHits = prov->SearchByName(CloudIntercept::kCNFilename, nullptr);
        for (const auto& h : cnHits) recordPath(h.path);
    } else {
        for (const auto& f : prov->List("")) {
            recordPath(f.path);
        }
    }

    std::ostringstream out;
    out << "{\"success\":true,\"games\":[";
    bool first = true;
    for (const auto& [acctId, appId] : discovered) {
        if (!first) out << ",";
        first = false;
        out << "{\"account_id\":\"" << acctId << "\",\"app_id\":\"" << appId << "\"}";
    }
    out << "]}";
    prov->Shutdown();
    return out.str();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_cloudredirect_nativecore_CloudRedirectNative_nativeRunCommand(
    JNIEnv* env,
    jclass /*clazz*/,
    jstring jCommand,
    jobjectArray jArgs) {

    if (!jCommand) {
        return env->NewStringUTF("{\"success\":false,\"error\":\"null command\"}");
    }

    const char* cmdChars = env->GetStringUTFChars(jCommand, nullptr);
    std::string cmd = cmdChars ? cmdChars : "";
    if (cmdChars) env->ReleaseStringUTFChars(jCommand, cmdChars);

    std::vector<std::string> args;
    if (jArgs) {
        jsize n = env->GetArrayLength(jArgs);
        args.reserve(static_cast<size_t>(n));
        for (jsize i = 0; i < n; ++i) {
            auto js = static_cast<jstring>(env->GetObjectArrayElement(jArgs, i));
            if (js) {
                const char* s = env->GetStringUTFChars(js, nullptr);
                args.emplace_back(s ? s : "");
                if (s) env->ReleaseStringUTFChars(js, s);
                env->DeleteLocalRef(js);
            } else {
                args.emplace_back();
            }
        }
    }

    std::string result;
    if (cmd == "auth-status" && args.size() >= 1) {
        result = CloudRedirectCli::CmdAuthStatus(args[0]);
    } else if (cmd == "list-remote-apps" && args.size() >= 2) {
        result = CloudRedirectCli::CmdListRemoteApps(args[0], args[1]);
    } else if (cmd == "list-remote-app-ids" && args.size() >= 2) {
        result = CloudRedirectCli::CmdListRemoteAppIds(args[0], args[1]);
    } else if (cmd == "list-remote-app-files" && args.size() >= 3) {
        result = CloudRedirectCli::CmdListRemoteAppFiles(args[0], args[1], args[2]);
    } else if (cmd == "delete-remote-app" && args.size() >= 3) {
        result = CloudRedirectCli::CmdDeleteRemoteApp(args[0], args[1], args[2]);
    } else if (cmd == "list-blobs" && args.size() >= 3) {
        result = CloudRedirectCli::CmdListBlobs(args[0], args[1], args[2]);
    } else if (cmd == "download-blob" && args.size() >= 4) {
        result = CloudRedirectCli::CmdDownloadBlob(args[0], args[1], args[2], args[3]);
    } else if (cmd == "list-all-stats" && args.size() >= 1) {
        result = CloudRedirectCli::CmdListAllStats(args[0]);
    } else if (cmd == "delete-blobs" && args.size() >= 4) {
        std::vector<std::string> blobs(args.begin() + 3, args.end());
        result = CloudRedirectCli::CmdDeleteBlobs(args[0], args[1], args[2], blobs);
    } else if (cmd == "sync-remote-app" && args.size() >= 4) {
        result = CloudRedirectCli::CmdSyncRemoteApp(args[0], args[1], args[2], args[3]);
    } else if (cmd == "sync-all-remote-apps" && args.size() >= 3) {
        result = CloudRedirectCli::CmdSyncAllRemoteApps(args[0], args[1], args[2]);
    } else if (cmd == "prune-local-legacy-metadata" && args.size() >= 1) {
        result = CloudRedirectCli::CmdPruneLocalLegacyMetadata(args[0]);
    } else if (cmd == "publish-full-manifest" && args.size() >= 4) {
        result = CloudRedirectCli::CmdPublishFullManifest(args[0], args[1], args[2], args[3]);
    } else if (cmd == "discover-cloud-games" && args.size() >= 1) {
        result = DiscoverCloudGamesJson(args[0]);
    } else {
        result = "{\"success\":false,\"error\":\"unknown command or missing args: " + cmd + "\"}";
    }

    return env->NewStringUTF(result.c_str());
}
