package com.dualscreen.launcher.access

import com.dualscreen.launcher.BuildConfig
import java.net.URI

internal data class AccessConfiguration(
    val apiBaseUrl: String,
    val leasePublicKey: String,
) {
    companion object {
        fun load(): Result<AccessConfiguration> = runCatching {
            val rawUrl = BuildConfig.ACCESS_API_BASE_URL.trim().trimEnd('/')
            val uri = URI(rawUrl)
            require(uri.scheme.equals("https", ignoreCase = true) && !uri.host.isNullOrBlank()) {
                "Access service URL must be a valid HTTPS origin."
            }
            require(uri.rawQuery == null && uri.rawFragment == null) {
                "Access service URL cannot contain a query or fragment."
            }
            require(uri.path.isNullOrEmpty() || uri.path == "/") {
                "Access service URL must be an HTTPS origin without a path."
            }
            val publicKey = BuildConfig.ACCESS_LEASE_PUBLIC_KEY.trim()
            require(publicKey.isNotBlank()) { "Access lease public key is missing." }
            AccessConfiguration(rawUrl, publicKey)
        }
    }
}

sealed interface AccessState {
    val message: String

    data object Checking : AccessState {
        override val message = "Securely verifying access…"
    }

    data class Granted(
        val source: AccessSource,
        val expiresAtEpochSeconds: Long,
        override val message: String,
    ) : AccessState

    data class SignInRequired(
        override val message: String,
    ) : AccessState

    data class Denied(
        override val message: String,
        val reauthorizationRequired: Boolean = false,
    ) : AccessState

    data class Unavailable(
        override val message: String,
    ) : AccessState

    data class ConfigurationError(
        override val message: String,
    ) : AccessState
}

enum class AccessSource { PATREON, CODE }

internal data class AccessDecision(
    val allowed: Boolean,
    val source: String?,
    val reason: String?,
    val message: String,
    val lease: String?,
    val expiresAtEpochSeconds: Long?,
    val accountLinked: Boolean,
)

internal data class InstallationSession(
    val installationId: String,
    val token: String,
)

internal class AccessServiceException(
    val statusCode: Int,
    val errorCode: String?,
    override val message: String,
) : Exception(message)
