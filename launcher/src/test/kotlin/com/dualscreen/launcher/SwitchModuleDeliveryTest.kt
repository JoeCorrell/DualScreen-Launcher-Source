package com.dualscreen.launcher

import com.dualscreen.launcher.switch.selectSwitchModManifest
import org.json.JSONObject
import org.junit.Assert.assertSame
import org.junit.Test

class SwitchModuleDeliveryTest {
    private fun manifest(delivery: String, title: String = "01007EF00011E000") =
        JSONObject().put("schema", 1).put("titleId", title).put("delivery", delivery)

    @Test fun bundledMigrationSurvivesNewerCatalogWithLegacyDescriptor() {
        val bundled = manifest("external-module")
        assertSame(bundled, selectSwitchModManifest(manifest("launcher-apk"), bundled))
    }
    @Test fun oldLaunchersKeepTheirEmbeddedDelivery() {
        val remote = manifest("launcher-apk")
        assertSame(remote, selectSwitchModManifest(remote, manifest("launcher-apk")))
    }
    @Test fun newerExternalDescriptorsStillUpdateNormally() {
        val remote = manifest("external-module").put("package", "botw/package.dsmod")
        assertSame(remote, selectSwitchModManifest(remote, manifest("external-module")))
    }
    @Test fun missingBundledDescriptorAndOtherTitlesAreNotOverridden() {
        val remote = manifest("launcher-apk")
        assertSame(remote, selectSwitchModManifest(remote, null))
        assertSame(remote, selectSwitchModManifest(remote, manifest("external-module", "0100F43008C44000")))
    }
    @Test fun malformedOrUnknownRemoteDeliveryIsNotHidden() {
        val invalid = manifest("launcher-apk").put("schema", 2)
        assertSame(invalid, selectSwitchModManifest(invalid, manifest("external-module")))
        val unknown = manifest("unknown")
        assertSame(unknown, selectSwitchModManifest(unknown, manifest("external-module")))
    }
}
