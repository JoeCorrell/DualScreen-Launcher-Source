package com.dualscreen.launcher

import com.dualscreen.companion.api.ModuleCodeCache
import java.io.File
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class ModuleCodeCacheTest {
    @get:Rule val temporary = TemporaryFolder()
    private fun source(): File = temporary.newFile("ui.jar").apply { writeText("verified UI fixture") }

    @Test fun verifiedCopyIsOutsideSignedPackageAndReusable() {
        val source = source()
        val cache = temporary.newFolder("cache")
        val digest = ModuleCodeCache.hash(source)
        val copy = ModuleCodeCache.verifiedDex(source, cache, digest)
        assertTrue(copy.canonicalPath.startsWith(cache.canonicalPath + File.separator))
        assertEquals(digest, ModuleCodeCache.hash(copy))
        File(copy.parentFile, "oat/arm64/ui.vdex").apply { parentFile.mkdirs(); writeText("ART") }
        assertEquals(copy, ModuleCodeCache.verifiedDex(source, cache, digest))
        assertFalse(File(source.parentFile, "oat").exists())
        copy.setWritable(true)
    }

    @Test fun invalidSourceCannotBecomeExecutable() {
        assertThrows(SecurityException::class.java) {
            ModuleCodeCache.verifiedDex(source(), temporary.newFolder(), "0".repeat(64))
        }
    }

    @Test fun alteredCachedCodeIsRejected() {
        val source = source()
        val cache = temporary.newFolder()
        val digest = ModuleCodeCache.hash(source)
        val copy = ModuleCodeCache.verifiedDex(source, cache, digest)
        copy.setWritable(true); copy.writeText("tampered")
        assertThrows(SecurityException::class.java) { ModuleCodeCache.verifiedDex(source, cache, digest) }
    }

    @Test fun legacyArtIsPreservedOutsideSignedPackage() {
        val pkg = temporary.newFolder("package")
        val cache = temporary.newFolder("cache")
        val paths = setOf("oat/arm64/ui.odex", "oat/arm64/ui.vdex", "oat/arm64/ui.art")
        paths.forEach { File(pkg, it).apply { parentFile.mkdirs(); writeText(it) } }
        ModuleCodeCache.relocateLegacyArt(pkg, cache, paths)
        assertTrue(paths.all { !File(pkg, it).exists() })
        assertEquals(paths, cache.walkTopDown().filter { it.isFile }.map { it.readText() }.toSet())
    }

    @Test fun arbitraryExtraCodeIsNotIgnoredOrMoved() {
        val pkg = temporary.newFolder("package")
        val cache = temporary.newFolder("cache")
        val extra = File(pkg, "oat/arm64/evil.dex").apply { parentFile.mkdirs(); writeText("unexpected") }
        assertThrows(SecurityException::class.java) {
            ModuleCodeCache.relocateLegacyArt(pkg, cache, setOf("oat/arm64/evil.dex"))
        }
        assertTrue(extra.exists())
        assertTrue(cache.listFiles()!!.isEmpty())
    }

    @Test fun observedThorProfileIsRelocatedAndValidationCanRepeat() {
        val pkg = temporary.newFolder("package")
        val cache = temporary.newFolder("cache")
        val ui = File(pkg, "ui.jar").apply { writeText("signed UI") }
        val digest = ModuleCodeCache.hash(ui)
        val profile = File(pkg, "oat/ui.jar.cur.prof").apply { parentFile.mkdirs(); writeText("ART profile") }
        ModuleCodeCache.relocateLegacyArt(pkg, cache, setOf("oat/ui.jar.cur.prof"))
        assertFalse(profile.exists())
        assertEquals(setOf("ui.jar"), pkg.walkTopDown().filter { it.isFile }.map { it.relativeTo(pkg).invariantSeparatorsPath }.toSet())
        assertEquals(digest, ModuleCodeCache.hash(ui))
        val dex = ModuleCodeCache.verifiedDex(ui, cache, digest)
        File(dex.parentFile, "oat/ui.jar.cur.prof").apply { parentFile.mkdirs(); writeText("new ART profile") }
        ModuleCodeCache.relocateLegacyArt(pkg, cache, emptySet())
        assertEquals(setOf("ui.jar"), pkg.walkTopDown().filter { it.isFile }.map { it.relativeTo(pkg).invariantSeparatorsPath }.toSet())
        dex.setWritable(true)
    }

    @Test fun profileLookalikesRemainRejected() {
        val pkg = temporary.newFolder("package")
        val cache = temporary.newFolder("cache")
        listOf("oat/evil.jar.cur.prof", "oat/ui.jar.cur.prof.dex", "assets/oat/ui.jar.cur.prof").forEach { path ->
            assertThrows(SecurityException::class.java) { ModuleCodeCache.relocateLegacyArt(pkg, cache, setOf(path)) }
        }
    }
}
