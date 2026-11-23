// Android HTTP transport: delegates HTTPS/HTTP requests via JNI to a
// Kotlin/Java CloudRedirectHttpBridge (backed by OkHttpClient).

#include "cloud_provider_base.h"
#include "log.h"

#include <jni.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace AndroidHttpBridge {

static std::mutex g_bridgeMutex;
static JavaVM* g_vm = nullptr;
static jobject g_bridgeObj = nullptr;
static jmethodID g_executeMethod = nullptr;
static jfieldID g_statusField = nullptr;
static jfieldID g_bodyField = nullptr;
static jfieldID g_locationField = nullptr;
static jfieldID g_headersField = nullptr;

bool Register(JavaVM* vm, JNIEnv* env, jobject bridgeObj) {
    std::lock_guard<std::mutex> lk(g_bridgeMutex);
    g_vm = vm;
    if (g_bridgeObj) {
        env->DeleteGlobalRef(g_bridgeObj);
        g_bridgeObj = nullptr;
    }
    g_executeMethod = nullptr;
    g_statusField = nullptr;
    g_bodyField = nullptr;
    g_locationField = nullptr;
    g_headersField = nullptr;

    if (!bridgeObj) return false;

    jclass bridgeCls = env->GetObjectClass(bridgeObj);
    if (!bridgeCls) return false;

    g_executeMethod = env->GetMethodID(
        bridgeCls,
        "execute",
        "(Ljava/lang/String;Ljava/lang/String;[B[Ljava/lang/String;ZZI)"
        "Lcom/cloudredirect/nativecore/CloudRedirectHttpResponse;"
    );
    env->DeleteLocalRef(bridgeCls);
    if (!g_executeMethod) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        Log::Error("[AndroidHttp] Failed to find CloudRedirectHttpBridge.execute");
        return false;
    }

    jclass respCls = env->FindClass("com/cloudredirect/nativecore/CloudRedirectHttpResponse");
    if (!respCls) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        Log::Error("[AndroidHttp] Failed to find CloudRedirectHttpResponse");
        return false;
    }

    g_statusField = env->GetFieldID(respCls, "status", "I");
    g_bodyField = env->GetFieldID(respCls, "body", "[B");
    g_locationField = env->GetFieldID(respCls, "location", "Ljava/lang/String;");
    g_headersField = env->GetFieldID(respCls, "headers", "[Ljava/lang/String;");
    env->DeleteLocalRef(respCls);

    if (!g_statusField || !g_bodyField || !g_locationField || !g_headersField) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        Log::Error("[AndroidHttp] Failed to resolve CloudRedirectHttpResponse fields");
        return false;
    }

    g_bridgeObj = env->NewGlobalRef(bridgeObj);
    return g_bridgeObj != nullptr;
}

bool IsRegistered() {
    std::lock_guard<std::mutex> lk(g_bridgeMutex);
    return g_vm != nullptr && g_bridgeObj != nullptr && g_executeMethod != nullptr;
}

} // namespace AndroidHttpBridge

namespace {

struct JniThreadScope {
    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    bool attached = false;

    explicit JniThreadScope(JavaVM* javaVm) : vm(javaVm) {
        if (!vm) return;
        jint rc = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (rc == JNI_EDETACHED) {
            if (vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
                attached = true;
            } else {
                env = nullptr;
            }
        } else if (rc != JNI_OK) {
            env = nullptr;
        }
    }

    ~JniThreadScope() {
        if (attached && vm) {
            vm->DetachCurrentThread();
        }
    }
};

std::string Base64Encode(const std::string& input) {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    int val = 0;
    int valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(kTable[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(kTable[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string ToLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool HasHeaderPrefix(const std::vector<std::string>& headers, const std::string& lowerPrefix) {
    for (const auto& h : headers) {
        if (h.size() >= lowerPrefix.size() &&
            ToLowerAscii(h.substr(0, lowerPrefix.size())) == lowerPrefix) {
            return true;
        }
    }
    return false;
}

std::string BuildUrl(const char* host, const std::string& path, bool allowInsecureHttp) {
    std::string h = host ? host : "";
    if (h.rfind("http://", 0) == 0 || h.rfind("https://", 0) == 0) {
        return h + path;
    }
    const char* scheme = (allowInsecureHttp && h.find(":80") != std::string::npos) ? "http://" : "https://";
    if (!path.empty() && path.front() != '/') {
        return std::string(scheme) + h + "/" + path;
    }
    return std::string(scheme) + h + path;
}

} // namespace

class AndroidHttpTransport : public IHttpTransport {
public:
    explicit AndroidHttpTransport(const char* logTag)
        : m_logTag(logTag ? logTag : "CloudRedirect") {}

    bool Init() override {
        m_ready = AndroidHttpBridge::IsRegistered();
        if (!m_ready) {
            Log::Warn("[%s] AndroidHttpTransport Init called before HTTP bridge registration", m_logTag.c_str());
        }
        return m_ready;
    }

    void Shutdown() override {
        m_ready = false;
    }

    bool IsReady() const override {
        return m_ready && AndroidHttpBridge::IsRegistered();
    }

    void SetOptions(const TransportOptions& opts) override {
        m_opts = opts;
    }

    HttpUtil::HttpResp Request(const char* method, const char* host,
                               const std::string& path,
                               const std::string& body,
                               const std::vector<std::string>& headers) override {
        std::string url = BuildUrl(host, path, m_opts.allowInsecureHttp);
        return RequestUrl(method, url, body, headers);
    }

    HttpUtil::HttpResp RequestUrl(const char* method, const std::string& fullUrl,
                                  const std::string& body,
                                  const std::vector<std::string>& headers) override {
        HttpUtil::HttpResp resp;
        if (!AndroidHttpBridge::IsRegistered()) {
            Log::Error("[%s] HTTP bridge not registered", m_logTag.c_str());
            return resp;
        }

        if (!m_opts.allowInsecureHttp && fullUrl.rfind("http://", 0) == 0) {
            Log::Error("[%s] Blocked insecure HTTP URL: %s", m_logTag.c_str(), fullUrl.c_str());
            return resp;
        }

        std::vector<std::string> effHeaders = headers;
        if (!HasHeaderPrefix(effHeaders, "authorization:")) {
            if (m_opts.authScheme == ServerAuthScheme::Basic && !m_opts.username.empty()) {
                effHeaders.push_back("Authorization: Basic " +
                                     Base64Encode(m_opts.username + ":" + m_opts.password));
            } else if (m_opts.authScheme == ServerAuthScheme::Bearer && !m_opts.bearerToken.empty()) {
                effHeaders.push_back("Authorization: Bearer " + m_opts.bearerToken);
            }
        }

        bool followRedirects = !m_opts.disableRedirects;
        int timeoutSec = m_opts.timeoutSec > 0 ? m_opts.timeoutSec : 60;

        JavaVM* vm = nullptr;
        jobject bridgeObj = nullptr;
        jmethodID executeMethod = nullptr;
        jfieldID statusField = nullptr;
        jfieldID bodyField = nullptr;
        jfieldID locationField = nullptr;
        jfieldID headersField = nullptr;
        {
            std::lock_guard<std::mutex> lk(AndroidHttpBridge::g_bridgeMutex);
            vm = AndroidHttpBridge::g_vm;
            bridgeObj = AndroidHttpBridge::g_bridgeObj;
            executeMethod = AndroidHttpBridge::g_executeMethod;
            statusField = AndroidHttpBridge::g_statusField;
            bodyField = AndroidHttpBridge::g_bodyField;
            locationField = AndroidHttpBridge::g_locationField;
            headersField = AndroidHttpBridge::g_headersField;
        }

        JniThreadScope jni(vm);
        JNIEnv* env = jni.env;
        if (!env || !bridgeObj || !executeMethod) return resp;

        jstring jMethod = env->NewStringUTF(method ? method : "GET");
        jstring jUrl = env->NewStringUTF(fullUrl.c_str());

        jbyteArray jBody = nullptr;
        if (!body.empty()) {
            jBody = env->NewByteArray(static_cast<jsize>(body.size()));
            if (jBody) {
                env->SetByteArrayRegion(jBody, 0, static_cast<jsize>(body.size()),
                                        reinterpret_cast<const jbyte*>(body.data()));
            }
        }

        jclass strCls = env->FindClass("java/lang/String");
        jobjectArray jHeaders = env->NewObjectArray(static_cast<jsize>(effHeaders.size()), strCls, nullptr);
        for (jsize i = 0; i < static_cast<jsize>(effHeaders.size()); ++i) {
            jstring s = env->NewStringUTF(effHeaders[static_cast<size_t>(i)].c_str());
            env->SetObjectArrayElement(jHeaders, i, s);
            env->DeleteLocalRef(s);
        }
        env->DeleteLocalRef(strCls);

        jobject jResp = env->CallObjectMethod(
            bridgeObj,
            executeMethod,
            jMethod,
            jUrl,
            jBody,
            jHeaders,
            static_cast<jboolean>(followRedirects),
            static_cast<jboolean>(m_opts.allowInsecureTls),
            static_cast<jint>(timeoutSec)
        );

        env->DeleteLocalRef(jMethod);
        env->DeleteLocalRef(jUrl);
        if (jBody) env->DeleteLocalRef(jBody);
        env->DeleteLocalRef(jHeaders);

        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            return resp;
        }

        if (!jResp) return resp;

        resp.status = static_cast<int>(env->GetIntField(jResp, statusField));

        auto jRespBody = static_cast<jbyteArray>(env->GetObjectField(jResp, bodyField));
        if (jRespBody) {
            jsize len = env->GetArrayLength(jRespBody);
            if (len > 0) {
                resp.body.resize(static_cast<size_t>(len));
                env->GetByteArrayRegion(jRespBody, 0, len, reinterpret_cast<jbyte*>(resp.body.data()));
            }
            env->DeleteLocalRef(jRespBody);
        }

        auto jLoc = static_cast<jstring>(env->GetObjectField(jResp, locationField));
        if (jLoc) {
            const char* locChars = env->GetStringUTFChars(jLoc, nullptr);
            if (locChars) {
                resp.location = locChars;
                env->ReleaseStringUTFChars(jLoc, locChars);
            }
            env->DeleteLocalRef(jLoc);
        }

        auto jRespHeaders = static_cast<jobjectArray>(env->GetObjectField(jResp, headersField));
        if (jRespHeaders) {
            jsize count = env->GetArrayLength(jRespHeaders);
            for (jsize i = 0; i + 1 < count; i += 2) {
                auto jk = static_cast<jstring>(env->GetObjectArrayElement(jRespHeaders, i));
                auto jv = static_cast<jstring>(env->GetObjectArrayElement(jRespHeaders, i + 1));
                if (jk && jv) {
                    const char* kChars = env->GetStringUTFChars(jk, nullptr);
                    const char* vChars = env->GetStringUTFChars(jv, nullptr);
                    if (kChars && vChars) {
                        resp.headers[ToLowerAscii(kChars)] = vChars;
                    }
                    if (kChars) env->ReleaseStringUTFChars(jk, kChars);
                    if (vChars) env->ReleaseStringUTFChars(jv, vChars);
                }
                if (jk) env->DeleteLocalRef(jk);
                if (jv) env->DeleteLocalRef(jv);
            }
            env->DeleteLocalRef(jRespHeaders);
        }

        env->DeleteLocalRef(jResp);
        return resp;
    }

    HttpUtil::HttpResp AuthenticatedGetWithRedirect(const std::string& host,
                                                    const std::string& path,
                                                    const std::string& authHeader) override {
        TransportOptions saved = m_opts;
        m_opts.disableRedirects = true;
        std::vector<std::string> headers;
        if (!authHeader.empty()) {
            headers.push_back(authHeader);
        }

        HttpUtil::HttpResp resp = Request("GET", host.c_str(), path, "", headers);
        m_opts = saved;

        if ((resp.status == 301 || resp.status == 302 || resp.status == 303 ||
             resp.status == 307 || resp.status == 308) && !resp.location.empty()) {
            // Follow redirect without Authorization header (required by OneDrive/CDN)
            resp = RequestUrl("GET", resp.location, "", {});
        }
        return resp;
    }

private:
    std::string m_logTag;
    bool m_ready = false;
    TransportOptions m_opts;
};

std::unique_ptr<IHttpTransport> CreateHttpTransport(const char* logTag) {
    return std::make_unique<AndroidHttpTransport>(logTag);
}
