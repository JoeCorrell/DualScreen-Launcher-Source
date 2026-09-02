package com.dualscreen.launcher.access

import org.json.JSONObject
import java.io.InputStream
import java.net.URI
import java.nio.charset.StandardCharsets
import java.util.UUID
import javax.net.ssl.HttpsURLConnection

internal class AccessApiClient(private val configuration: AccessConfiguration) {
    fun registerInstallation(): InstallationSession {
        val installationId = UUID.randomUUID().toString()
        val response = post(
            path = "/v1/installations/register",
            token = null,
            body = JSONObject().put("installation_id", installationId),
        )
        val returnedId = response.body.optString("installation_id")
        val token = response.body.optString("session_token")
        if (response.statusCode !in 200..299 || returnedId != installationId ||
            !token.startsWith("dsl_session_") || token.length > 128
        ) {
            throw serviceError(response)
        }
        return InstallationSession(installationId, token)
    }

    fun beginPatreonOAuth(token: String): URI {
        val response = post("/v1/oauth/start", token, JSONObject())
        if (response.statusCode !in 200..299) throw serviceError(response)
        val uri = runCatching { URI(response.body.getString("authorization_url")) }
            .getOrElse { throw AccessServiceException(502, "invalid_response", "The access service returned an invalid Patreon sign-in URL.") }
        if (!uri.scheme.equals("https", true) || !uri.host.equals("www.patreon.com", true) ||
            uri.path != "/oauth2/authorize"
        ) {
            throw AccessServiceException(502, "invalid_response", "The access service returned an untrusted Patreon sign-in URL.")
        }
        return uri
    }

    fun entitlementStatus(token: String): AccessDecision =
        decision(post("/v1/entitlement/status", token, JSONObject()))

    fun completePatreonOAuth(token: String, completionToken: String) {
        val response = post(
            "/v1/oauth/complete",
            token,
            JSONObject().put("completion_token", completionToken),
        )
        if (response.statusCode !in 200..299 || !response.body.optBoolean("completed", false)) {
            throw serviceError(response)
        }
    }

    fun redeemCode(token: String, code: String): AccessDecision =
        decision(
            post(
                "/v1/access-codes/redeem",
                token,
                JSONObject().put("code", code),
            ),
        )

    fun signOut(token: String) {
        val response = post("/v1/session/logout", token, JSONObject())
        if (response.statusCode !in 200..299) throw serviceError(response)
    }

    private fun decision(response: ApiResponse): AccessDecision {
        if (!response.body.has("allowed")) throw serviceError(response)
        val allowed = response.body.optBoolean("allowed", false)
        val message = response.body.optString("message").ifBlank {
            if (allowed) "Access verified." else "Access is not available."
        }
        return AccessDecision(
            allowed = allowed,
            source = response.body.stringOrNull("source"),
            reason = response.body.stringOrNull("reason"),
            message = message,
            lease = response.body.stringOrNull("lease"),
            expiresAtEpochSeconds = response.body.longOrNull("expires_at"),
            accountLinked = response.body.optBoolean("account_linked", false),
        )
    }

    private fun post(path: String, token: String?, body: JSONObject): ApiResponse {
        val endpoint = URI(configuration.apiBaseUrl + path).toURL()
        val connection = (endpoint.openConnection() as? HttpsURLConnection)
            ?: throw AccessServiceException(0, "insecure_transport", "The access service must use HTTPS.")
        return try {
            connection.requestMethod = "POST"
            connection.connectTimeout = CONNECT_TIMEOUT_MS
            connection.readTimeout = READ_TIMEOUT_MS
            connection.doOutput = true
            connection.useCaches = false
            connection.instanceFollowRedirects = false
            connection.setRequestProperty("Accept", "application/json")
            connection.setRequestProperty("Content-Type", "application/json; charset=utf-8")
            token?.let { connection.setRequestProperty("Authorization", "Bearer $it") }
            val bytes = body.toString().toByteArray(StandardCharsets.UTF_8)
            connection.setFixedLengthStreamingMode(bytes.size)
            connection.outputStream.use { it.write(bytes) }
            val status = connection.responseCode
            val input = if (status in 200..399) connection.inputStream else connection.errorStream
            val responseText = input?.use(::readLimited).orEmpty()
            val responseBody = runCatching { JSONObject(responseText) }.getOrElse {
                throw AccessServiceException(status, "invalid_response", "The access service returned an invalid response.")
            }
            ApiResponse(status, responseBody)
        } finally {
            connection.disconnect()
        }
    }

    private fun readLimited(stream: InputStream): String {
        val reader = stream.bufferedReader(StandardCharsets.UTF_8)
        val output = StringBuilder()
        val buffer = CharArray(4096)
        while (true) {
            val count = reader.read(buffer)
            if (count < 0) break
            if (output.length + count > MAX_RESPONSE_CHARS) {
                throw AccessServiceException(0, "response_too_large", "The access service returned an invalid response.")
            }
            output.append(buffer, 0, count)
        }
        return output.toString()
    }

    private fun serviceError(response: ApiResponse): AccessServiceException = AccessServiceException(
        statusCode = response.statusCode,
        errorCode = response.body.stringOrNull("error"),
        message = response.body.optString("message").ifBlank { "The access service request failed." },
    )

    private data class ApiResponse(val statusCode: Int, val body: JSONObject)

    private companion object {
        const val CONNECT_TIMEOUT_MS = 15_000
        const val READ_TIMEOUT_MS = 30_000
        const val MAX_RESPONSE_CHARS = 64 * 1024
    }
}

private fun JSONObject.stringOrNull(name: String): String? =
    if (has(name) && !isNull(name)) optString(name).takeIf(String::isNotBlank) else null

private fun JSONObject.longOrNull(name: String): Long? =
    if (has(name) && !isNull(name)) optLong(name) else null
