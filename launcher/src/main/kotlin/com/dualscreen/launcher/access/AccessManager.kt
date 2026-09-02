package com.dualscreen.launcher.access

import android.content.Context
import android.net.Uri
import android.os.SystemClock
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import java.io.IOException

class AccessManager(context: Context) {
    private val configurationResult = AccessConfiguration.load()
    private val configuration = configurationResult.getOrNull()
    private val store = SecureSessionStore(context.applicationContext)
    private val api = configuration?.let(::AccessApiClient)
    private val leaseVerifier = configuration?.let(::AccessLeaseVerifier)
    private val operationLock = Mutex()
    private val mutableState = MutableStateFlow<AccessState>(initialState())
    val state: StateFlow<AccessState> = mutableState.asStateFlow()

    @Volatile
    private var lastSuccessfulVerificationElapsed = 0L

    suspend fun verifyNow(showChecking: Boolean = true): AccessState = operationLock.withLock {
        if (configuration == null || api == null || leaseVerifier == null) return@withLock mutableState.value
        if (showChecking) mutableState.value = AccessState.Checking
        val next = try {
            val session = ensureSession(api)
            val decision = withContext(Dispatchers.IO) { api.entitlementStatus(session.token) }
            decision.toState(session, leaseVerifier)
        } catch (failure: Throwable) {
            failure.toAccessState()
        }
        mutableState.value = next
        if (next is AccessState.Granted) {
            lastSuccessfulVerificationElapsed = SystemClock.elapsedRealtime()
        }
        next
    }

    suspend fun verifyIfStale(maxAgeMillis: Long): AccessState {
        val current = mutableState.value
        val elapsed = SystemClock.elapsedRealtime() - lastSuccessfulVerificationElapsed
        return if (current is AccessState.Granted && elapsed in 0 until maxAgeMillis) {
            current
        } else {
            verifyNow(showChecking = current !is AccessState.Granted)
        }
    }

    suspend fun beginPatreonLogin(): Result<Uri> = operationLock.withLock {
        val client = api ?: return@withLock Result.failure(
            IllegalStateException(mutableState.value.message),
        )
        mutableState.value = AccessState.Checking
        try {
            val session = ensureSession(client)
            val uri = withContext(Dispatchers.IO) { client.beginPatreonOAuth(session.token) }
            mutableState.value = AccessState.SignInRequired(
                "Complete Patreon sign-in in your browser, then return to DualScreen Launcher.",
            )
            Result.success(Uri.parse(uri.toString()))
        } catch (failure: Throwable) {
            mutableState.value = failure.toAccessState()
            Result.failure(failure)
        }
    }

    suspend fun redeemCode(code: String): AccessState = operationLock.withLock {
        val client = api
        val verifier = leaseVerifier
        if (client == null || verifier == null) return@withLock mutableState.value
        if (code.isBlank()) {
            return@withLock AccessState.Denied("Enter the one-time access code you received.")
                .also { mutableState.value = it }
        }
        mutableState.value = AccessState.Checking
        val next = try {
            val session = ensureSession(client)
            val decision = withContext(Dispatchers.IO) { client.redeemCode(session.token, code) }
            decision.toState(session, verifier)
        } catch (failure: Throwable) {
            failure.toAccessState()
        }
        mutableState.value = next
        if (next is AccessState.Granted) {
            lastSuccessfulVerificationElapsed = SystemClock.elapsedRealtime()
        }
        next
    }

    suspend fun handleOAuthCallback(uri: Uri): AccessState? {
        if (uri.scheme != "dualscreen" || uri.host != "auth" || uri.path != "/callback") return null
        return when (uri.getQueryParameter("result")) {
            "complete" -> {
                val completionToken = uri.getQueryParameter("completion_token").orEmpty()
                if (!completionToken.startsWith("dsl_oauth_complete_") || completionToken.length > 128) {
                    AccessState.Unavailable("Patreon sign-in could not be authenticated. Start sign-in again.")
                        .also { mutableState.value = it }
                } else {
                    completePatreonLogin(completionToken)
                }
            }
            "denied" -> AccessState.Denied("Patreon sign-in was cancelled. Sign in to verify an active membership.")
                .also { mutableState.value = it }
            else -> AccessState.Unavailable("Patreon sign-in could not be completed. Try again.")
                .also { mutableState.value = it }
        }
    }

    private suspend fun completePatreonLogin(completionToken: String): AccessState {
        val client = api ?: return mutableState.value
        val completionFailure = operationLock.withLock {
            mutableState.value = AccessState.Checking
            try {
                val session = ensureSession(client)
                withContext(Dispatchers.IO) {
                    client.completePatreonOAuth(session.token, completionToken)
                }
                null
            } catch (failure: Throwable) {
                failure.toAccessState().also { mutableState.value = it }
            }
        }
        return completionFailure ?: verifyNow()
    }

    suspend fun signOut(): AccessState = operationLock.withLock {
        val client = api ?: return@withLock mutableState.value
        val next = try {
            store.load()?.let { session ->
                withContext(Dispatchers.IO) { client.signOut(session.token) }
            }
            AccessState.SignInRequired("Patreon account disconnected. Sign in or redeem a one-time code to continue.")
        } catch (failure: Throwable) {
            failure.toAccessState()
        }
        lastSuccessfulVerificationElapsed = 0L
        mutableState.value = next
        next
    }

    private suspend fun ensureSession(client: AccessApiClient): InstallationSession {
        store.load()?.let { return it }
        return withContext(Dispatchers.IO) { client.registerInstallation() }
            .also(store::save)
    }

    private fun AccessDecision.toState(
        session: InstallationSession,
        verifier: AccessLeaseVerifier,
    ): AccessState {
        if (!allowed) {
            return when (reason) {
                "sign_in_required" -> AccessState.SignInRequired(message)
                "reauthorization_required" -> AccessState.Denied(message, reauthorizationRequired = true)
                "verification_unavailable" -> AccessState.Unavailable(message)
                else -> AccessState.Denied(message)
            }
        }
        val signedLease = lease ?: return AccessState.Unavailable(AUTHENTICATION_FAILURE)
        val responseExpiry = expiresAtEpochSeconds ?: return AccessState.Unavailable(AUTHENTICATION_FAILURE)
        return try {
            val verified = verifier.verify(signedLease, session.installationId, responseExpiry)
            val responseSource = when (source) {
                "patreon" -> AccessSource.PATREON
                "code" -> AccessSource.CODE
                else -> null
            }
            require(responseSource == verified.source)
            AccessState.Granted(verified.source, verified.expiresAtEpochSeconds, message)
        } catch (_: Throwable) {
            AccessState.Unavailable(AUTHENTICATION_FAILURE)
        }
    }

    private fun Throwable.toAccessState(): AccessState = when (this) {
        is AccessServiceException -> when {
            errorCode == "invalid_session" -> AccessState.Denied(
                "This installation authorization is no longer valid. It may have been revoked; contact the app owner for help.",
            )
            statusCode in 400..499 -> AccessState.Denied(message)
            else -> AccessState.Unavailable(NETWORK_FAILURE)
        }
        is IOException -> AccessState.Unavailable(NETWORK_FAILURE)
        else -> AccessState.Unavailable(NETWORK_FAILURE)
    }

    private fun initialState(): AccessState = configurationResult.fold(
        onSuccess = { AccessState.Checking },
        onFailure = {
            AccessState.ConfigurationError(
                "This build is not connected to the secure access service. The app owner must configure the access service URL and lease public key.",
            )
        },
    )

    private companion object {
        const val NETWORK_FAILURE =
            "Access could not be securely verified. Check your internet connection and try again; the app remains locked while verification is unavailable."
        const val AUTHENTICATION_FAILURE =
            "The access response could not be authenticated. The app remains locked; contact the app owner."
    }
}
