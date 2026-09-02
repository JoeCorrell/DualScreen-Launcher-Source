package com.dualscreen.launcher.access

import org.json.JSONObject
import java.nio.charset.StandardCharsets
import java.security.KeyFactory
import java.security.Signature
import java.security.spec.X509EncodedKeySpec
import java.util.Base64
import kotlin.math.abs

/** Independently authenticates every allow decision returned by the service. */
internal class AccessLeaseVerifier(
    private val configuration: AccessConfiguration,
    private val nowEpochSeconds: () -> Long = { System.currentTimeMillis() / 1000L },
) {
    private val publicKey by lazy {
        val cleaned = configuration.leasePublicKey
            .replace("-----BEGIN PUBLIC KEY-----", "")
            .replace("-----END PUBLIC KEY-----", "")
            .filterNot(Char::isWhitespace)
        val decoded = decodeBase64(cleaned)
        KeyFactory.getInstance("EC").generatePublic(X509EncodedKeySpec(decoded))
    }

    fun verify(
        compactLease: String,
        expectedInstallationId: String,
        responseExpiresAt: Long,
    ): VerifiedLease {
        val pieces = compactLease.split('.')
        require(pieces.size == 3) { "Malformed access lease." }
        val header = JSONObject(String(decodeBase64(pieces[0]), StandardCharsets.UTF_8))
        require(header.optString("alg") == "ES256") { "Unexpected access lease algorithm." }
        require(header.optString("typ") == "JWT") { "Unexpected access lease type." }

        val rawSignature = decodeBase64(pieces[2])
        require(rawSignature.size == 64) { "Malformed ES256 signature." }
        val verifier = Signature.getInstance("SHA256withECDSA")
        verifier.initVerify(publicKey)
        verifier.update("${pieces[0]}.${pieces[1]}".toByteArray(StandardCharsets.US_ASCII))
        require(verifier.verify(rawSignatureToDer(rawSignature))) { "Invalid access lease signature." }

        val claims = JSONObject(String(decodeBase64(pieces[1]), StandardCharsets.UTF_8))
        val issuer = configuration.apiBaseUrl.trimEnd('/')
        val issuedAt = claims.getLong("iat")
        val expiresAt = claims.getLong("exp")
        val now = nowEpochSeconds()
        require(claims.getString("iss") == issuer) { "Access lease issuer does not match." }
        require(claims.getString("aud") == AUDIENCE) { "Access lease audience does not match." }
        require(claims.getString("sub").isNotBlank()) { "Access lease subject is missing." }
        require(claims.getString("installation_id") == expectedInstallationId) {
            "Access lease belongs to a different installation."
        }
        require(abs(expiresAt - responseExpiresAt) <= CLOCK_SKEW_SECONDS) { "Access lease expiry does not match its response." }
        require(issuedAt <= now + CLOCK_SKEW_SECONDS) { "Access lease was issued in the future." }
        require(expiresAt > now - CLOCK_SKEW_SECONDS) { "Access lease has expired." }
        require(expiresAt - issuedAt in MIN_LEASE_SECONDS..MAX_LEASE_SECONDS) { "Access lease lifetime is invalid." }
        val source = when (claims.getString("entitlement")) {
            "patreon" -> AccessSource.PATREON
            "code" -> AccessSource.CODE
            else -> error("Unknown access entitlement.")
        }
        require(claims.getString("entitlement_id").isNotBlank()) { "Access entitlement is missing." }
        require(claims.getString("jti").isNotBlank()) { "Access lease identifier is missing." }
        return VerifiedLease(source, expiresAt)
    }

    private fun decodeBase64(value: String): ByteArray =
        runCatching { Base64.getUrlDecoder().decode(value) }
            .getOrElse { Base64.getDecoder().decode(value) }

    data class VerifiedLease(val source: AccessSource, val expiresAtEpochSeconds: Long)

    private companion object {
        const val AUDIENCE = "dualscreen-launcher"
        const val CLOCK_SKEW_SECONDS = 30L
        const val MIN_LEASE_SECONDS = 300L
        const val MAX_LEASE_SECONDS = 86_400L
    }
}

internal fun rawSignatureToDer(raw: ByteArray): ByteArray {
    require(raw.size == 64)
    fun integer(component: ByteArray): ByteArray {
        var first = 0
        while (first < component.lastIndex && component[first] == 0.toByte()) first += 1
        val trimmed = component.copyOfRange(first, component.size)
        return if (trimmed[0].toInt() and 0x80 != 0) byteArrayOf(0) + trimmed else trimmed
    }
    val r = integer(raw.copyOfRange(0, 32))
    val s = integer(raw.copyOfRange(32, 64))
    val length = 2 + r.size + 2 + s.size
    require(length < 128)
    return byteArrayOf(
        0x30,
        length.toByte(),
        0x02,
        r.size.toByte(),
        *r,
        0x02,
        s.size.toByte(),
        *s,
    )
}
