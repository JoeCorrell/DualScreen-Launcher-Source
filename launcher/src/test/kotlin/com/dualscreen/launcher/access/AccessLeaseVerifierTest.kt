package com.dualscreen.launcher.access

import com.google.common.truth.Truth.assertThat
import org.json.JSONObject
import org.junit.Assert.assertThrows
import org.junit.Test
import java.nio.charset.StandardCharsets
import java.security.KeyPairGenerator
import java.security.Signature
import java.util.Base64

class AccessLeaseVerifierTest {
    private val keys = KeyPairGenerator.getInstance("EC").apply { initialize(256) }.generateKeyPair()
    private val configuration = AccessConfiguration(
        apiBaseUrl = "https://access.example.test",
        leasePublicKey = encode(keys.public.encoded),
    )

    @Test
    fun acceptsSignedLeaseBoundToInstallation() {
        val lease = lease(installationId = INSTALLATION_ID)

        val verified = AccessLeaseVerifier(configuration) { NOW }
            .verify(lease, INSTALLATION_ID, NOW + 3600)

        assertThat(verified.source).isEqualTo(AccessSource.PATREON)
        assertThat(verified.expiresAtEpochSeconds).isEqualTo(NOW + 3600)
    }

    @Test
    fun rejectsLeaseForDifferentInstallation() {
        val lease = lease(installationId = INSTALLATION_ID)

        assertThrows(IllegalArgumentException::class.java) {
            AccessLeaseVerifier(configuration) { NOW }
                .verify(lease, "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", NOW + 3600)
        }
    }

    @Test
    fun rejectsTamperedClaims() {
        val signed = lease(installationId = INSTALLATION_ID)
        val pieces = signed.split('.').toMutableList()
        val claims = JSONObject(String(Base64.getUrlDecoder().decode(pieces[1]), StandardCharsets.UTF_8))
            .put("entitlement", "code")
        pieces[1] = encode(claims.toString().toByteArray(StandardCharsets.UTF_8))

        assertThrows(IllegalArgumentException::class.java) {
            AccessLeaseVerifier(configuration) { NOW }
                .verify(pieces.joinToString("."), INSTALLATION_ID, NOW + 3600)
        }
    }

    @Test
    fun convertsRawEs256SignatureToDer() {
        val raw = ByteArray(64).also {
            it[0] = 0x80.toByte()
            it[31] = 1
            it[32] = 0
            it[63] = 2
        }

        val der = rawSignatureToDer(raw)

        assertThat(der[0]).isEqualTo(0x30.toByte())
        assertThat(der[2]).isEqualTo(0x02.toByte())
        assertThat(der[4]).isEqualTo(0.toByte())
    }

    private fun lease(installationId: String): String {
        val header = JSONObject()
            .put("alg", "ES256")
            .put("typ", "JWT")
            .put("kid", "test-key")
        val claims = JSONObject()
            .put("iss", configuration.apiBaseUrl)
            .put("aud", "dualscreen-launcher")
            .put("sub", "test-user")
            .put("installation_id", installationId)
            .put("entitlement", "patreon")
            .put("entitlement_id", "member-1")
            .put("iat", NOW)
            .put("exp", NOW + 3600)
            .put("jti", "lease-test")
        val signingInput = encode(header.toString().toByteArray(StandardCharsets.UTF_8)) + "." +
            encode(claims.toString().toByteArray(StandardCharsets.UTF_8))
        val signer = Signature.getInstance("SHA256withECDSA")
        signer.initSign(keys.private)
        signer.update(signingInput.toByteArray(StandardCharsets.US_ASCII))
        return signingInput + "." + encode(derSignatureToRaw(signer.sign()))
    }

    private fun derSignatureToRaw(der: ByteArray): ByteArray {
        var cursor = 2
        check(der[cursor++] == 0x02.toByte())
        val rLength = der[cursor++].toInt() and 0xff
        val r = der.copyOfRange(cursor, cursor + rLength)
        cursor += rLength
        check(der[cursor++] == 0x02.toByte())
        val sLength = der[cursor++].toInt() and 0xff
        val s = der.copyOfRange(cursor, cursor + sLength)
        val raw = ByteArray(64)
        r.copyOfRange(maxOf(0, r.size - 32), r.size)
            .copyInto(raw, 32 - minOf(32, r.size))
        s.copyOfRange(maxOf(0, s.size - 32), s.size)
            .copyInto(raw, 64 - minOf(32, s.size))
        return raw
    }

    private fun encode(value: ByteArray): String =
        Base64.getUrlEncoder().withoutPadding().encodeToString(value)

    private companion object {
        const val NOW = 2_000_000_000L
        const val INSTALLATION_ID = "11111111-1111-4111-8111-111111111111"
    }
}
