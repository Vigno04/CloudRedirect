package com.cloudredirect.nativecore

import okhttp3.MediaType.Companion.toMediaTypeOrNull
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.security.SecureRandom
import java.security.cert.X509Certificate
import java.util.concurrent.TimeUnit
import javax.net.ssl.SSLContext
import javax.net.ssl.TrustManager
import javax.net.ssl.X509TrustManager

/**
 * Response object returned from [CloudRedirectHttpBridge.execute] to C++ via JNI.
 */
class CloudRedirectHttpResponse(
    @JvmField val status: Int,
    @JvmField val body: ByteArray,
    @JvmField val location: String,
    @JvmField val headers: Array<String>
)

/**
 * Transport interface called from libcloud_redirect_android.so.
 */
interface CloudRedirectHttpBridge {
    fun execute(
        method: String,
        url: String,
        body: ByteArray?,
        headers: Array<String>,
        followRedirects: Boolean,
        allowInsecureTls: Boolean,
        timeoutSec: Int
    ): CloudRedirectHttpResponse
}

/**
 * Default OkHttp-backed implementation of [CloudRedirectHttpBridge].
 */
class OkHttpCloudRedirectBridge(
    private val baseClient: OkHttpClient = OkHttpClient()
) : CloudRedirectHttpBridge {

    override fun execute(
        method: String,
        url: String,
        body: ByteArray?,
        headers: Array<String>,
        followRedirects: Boolean,
        allowInsecureTls: Boolean,
        timeoutSec: Int
    ): CloudRedirectHttpResponse {
        val builder = baseClient.newBuilder()
            .followRedirects(followRedirects)
            .followSslRedirects(followRedirects)
            .connectTimeout(timeoutSec.toLong(), TimeUnit.SECONDS)
            .readTimeout(timeoutSec.toLong(), TimeUnit.SECONDS)
            .writeTimeout(timeoutSec.toLong(), TimeUnit.SECONDS)

        if (allowInsecureTls) {
            val trustAll = object : X509TrustManager {
                override fun checkClientTrusted(chain: Array<X509Certificate>, authType: String) {}
                override fun checkServerTrusted(chain: Array<X509Certificate>, authType: String) {}
                override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()
            }
            val ssl = SSLContext.getInstance("TLS").apply {
                init(null, arrayOf<TrustManager>(trustAll), SecureRandom())
            }
            builder.sslSocketFactory(ssl.socketFactory, trustAll)
            builder.hostnameVerifier { _, _ -> true }
        }

        val reqBuilder = Request.Builder().url(url)
        var contentType: String? = null
        for (line in headers) {
            val idx = line.indexOf(':')
            if (idx <= 0) continue
            val name = line.substring(0, idx).trim()
            val value = line.substring(idx + 1).trim()
            if (name.equals("Content-Type", ignoreCase = true)) {
                contentType = value
            }
            reqBuilder.addHeader(name, value)
        }

        val upperMethod = method.uppercase()
        val reqBody = when {
            upperMethod == "GET" || upperMethod == "HEAD" -> null
            body != null -> body.toRequestBody(contentType?.toMediaTypeOrNull())
            upperMethod == "POST" || upperMethod == "PUT" || upperMethod == "PATCH" ->
                ByteArray(0).toRequestBody(contentType?.toMediaTypeOrNull())
            else -> null
        }
        reqBuilder.method(upperMethod, reqBody)

        return try {
            builder.build().newCall(reqBuilder.build()).execute().use { res ->
                val respBytes = res.body?.bytes() ?: ByteArray(0)
                val loc = res.header("Location") ?: ""
                val flatHeaders = ArrayList<String>(res.headers.size * 2)
                for (i in 0 until res.headers.size) {
                    flatHeaders.add(res.headers.name(i))
                    flatHeaders.add(res.headers.value(i))
                }
                CloudRedirectHttpResponse(
                    status = res.code,
                    body = respBytes,
                    location = loc,
                    headers = flatHeaders.toTypedArray()
                )
            }
        } catch (_: Exception) {
            CloudRedirectHttpResponse(0, ByteArray(0), "", emptyArray())
        }
    }
}

/**
 * JNI entry point for libcloud_redirect_android.so.
 */
object CloudRedirectNative {
    init {
        System.loadLibrary("cloud_redirect_android")
    }

    @JvmStatic
    external fun nativeInit(configDir: String, httpBridge: CloudRedirectHttpBridge): Boolean

    @JvmStatic
    external fun nativeGetVersion(): String

    @JvmStatic
    external fun nativeRunCommand(command: String, args: Array<String>): String
}
