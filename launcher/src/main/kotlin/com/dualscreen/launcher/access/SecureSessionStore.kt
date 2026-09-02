package com.dualscreen.launcher.access

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.nio.charset.StandardCharsets
import java.security.KeyStore
import java.util.UUID
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** Stores only the opaque installation bearer under an Android Keystore key. */
internal class SecureSessionStore(context: Context) {
    private val preferences = context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)

    @Synchronized
    fun load(): InstallationSession? {
        val installationId = preferences.getString(INSTALLATION_ID, null) ?: return null
        val envelope = preferences.getString(SESSION_TOKEN, null) ?: return null
        if (runCatching { UUID.fromString(installationId) }.isFailure) return null
        return runCatching {
            val pieces = envelope.split('.')
            require(pieces.size == 3 && pieces[0] == "v1")
            val cipher = Cipher.getInstance(CIPHER)
            cipher.init(
                Cipher.DECRYPT_MODE,
                secretKey(),
                GCMParameterSpec(TAG_BITS, decode(pieces[1])),
            )
            cipher.updateAAD(installationId.toByteArray(StandardCharsets.UTF_8))
            val token = String(cipher.doFinal(decode(pieces[2])), StandardCharsets.UTF_8)
            require(token.startsWith("dsl_session_") && token.length <= 128)
            InstallationSession(installationId, token)
        }.getOrElse {
            clear()
            null
        }
    }

    @Synchronized
    fun save(session: InstallationSession) {
        val cipher = Cipher.getInstance(CIPHER)
        cipher.init(Cipher.ENCRYPT_MODE, secretKey())
        cipher.updateAAD(session.installationId.toByteArray(StandardCharsets.UTF_8))
        val ciphertext = cipher.doFinal(session.token.toByteArray(StandardCharsets.UTF_8))
        val envelope = "v1.${encode(cipher.iv)}.${encode(ciphertext)}"
        check(
            preferences.edit()
                .putString(INSTALLATION_ID, session.installationId)
                .putString(SESSION_TOKEN, envelope)
                .commit(),
        ) { "Could not securely persist the access session." }
    }

    @Synchronized
    fun clear() {
        preferences.edit().clear().commit()
    }

    private fun secretKey(): SecretKey {
        val keyStore = KeyStore.getInstance(KEYSTORE).apply { load(null) }
        (keyStore.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }
        val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE)
        generator.init(
            KeyGenParameterSpec.Builder(
                KEY_ALIAS,
                KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
            )
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setRandomizedEncryptionRequired(true)
                .build(),
        )
        return generator.generateKey()
    }

    private fun encode(value: ByteArray): String =
        Base64.encodeToString(value, Base64.NO_WRAP or Base64.NO_PADDING or Base64.URL_SAFE)

    private fun decode(value: String): ByteArray =
        Base64.decode(value, Base64.NO_WRAP or Base64.NO_PADDING or Base64.URL_SAFE)

    private companion object {
        const val PREFERENCES = "dualscreen_access_secure_v1"
        const val INSTALLATION_ID = "installation_id"
        const val SESSION_TOKEN = "session_token"
        const val KEYSTORE = "AndroidKeyStore"
        const val KEY_ALIAS = "dualscreen_access_session_v1"
        const val CIPHER = "AES/GCM/NoPadding"
        const val TAG_BITS = 128
    }
}
