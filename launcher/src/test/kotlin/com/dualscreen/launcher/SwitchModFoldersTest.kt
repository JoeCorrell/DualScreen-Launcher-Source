package com.dualscreen.launcher

import com.dualscreen.launcher.switch.SwitchModFolders
import com.google.common.truth.Truth.assertThat
import java.io.File
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class SwitchModFoldersTest {
    @get:Rule val temporary = TemporaryFolder()
    private val title = "01006BB00C6F0000"
    private data class Node(val name: String, val children: List<Node> = emptyList())
    private fun node(name: String, vararg children: Node) = Node(name, children.toList())
    private fun scan(root: Node) = SwitchModFolders.discover(root, title, Node::name, Node::children)

    @Test fun directExefsIsOneModAndItsContentsAreNotScanned() {
        val result = scan(node("FPS mod", node("exefs", node("not another mod"))))
        assertThat(result.map { it.name }).containsExactly("FPS mod")
        assertThat(result.single().path).isEmpty()
    }

    @Test fun folderContainingDirectAndAtmosphereModsOffersIndividualChoices() {
        for (wrapper in listOf("contents", "titles")) {
            val result = scan(node("Collection",
                node("FPS mod", node("exefs")),
                node("No blur", node("atmosphere", node(wrapper, node(title, node("romfs"))))),
            ))
            assertThat(result.map { it.name }).containsExactly("FPS mod", "No blur")
            assertThat(result.single { it.name == "No blur" }.path)
                .containsExactly("No blur", "atmosphere", wrapper, title).inOrder()
        }
    }

    @Test fun anotherTitlesPayloadIsNotOffered() {
        assertThat(scan(node("Mod", node("atmosphere", node("contents",
            node("0100F2C0115B6000", node("romfs")),
        ))))).isEmpty()
    }

    @Test fun payloadRootDetectionIsCaseInsensitive() {
        assertThat(scan(node("Mod", node("ExeFS")))).hasSize(1)
    }

    @Test(expected = IllegalArgumentException::class)
    fun traversalNamesAreRejected() { SwitchModFolders.child(temporary.root, "../other") }

    @Test(expected = IllegalArgumentException::class)
    fun broadTreesAreBounded() { scan(Node("Collection", List(513) { Node("$it") })) }

    @Test fun longAssetNamesArePreservedWithoutTruncation() {
        val name = "A".repeat(140) + ".bfres"
        assertThat(SwitchModFolders.child(temporary.root, name).name).isEqualTo(name)
    }

    @Test fun onlySelectedModsArePublished() {
        val staging = temporary.newFolder(".dualscreen-import-test")
        File(staging, "One").mkdir()
        File(staging, "Two").mkdir()
        SwitchModFolders.publish(staging, temporary.root, listOf("Two"))
        assertThat(File(temporary.root, "Two").isDirectory).isTrue()
        assertThat(File(temporary.root, "One").exists()).isFalse()
        assertThat(File(staging, "One").isDirectory).isTrue()
    }

    @Test fun failedPublishRollsBackNewModsButPreservesExistingMods() {
        val existing = temporary.newFolder("Existing")
        val staging = temporary.newFolder(".dualscreen-import-test")
        File(staging, "One").mkdir()
        val result = runCatching {
            SwitchModFolders.publish(staging, temporary.root, listOf("One", "Missing"))
        }
        assertThat(result.isFailure).isTrue()
        assertThat(File(temporary.root, "One").exists()).isFalse()
        assertThat(existing.isDirectory).isTrue()
    }

    @Test fun existingModIsNeverOverwritten() {
        val existing = temporary.newFolder("Existing")
        val staging = temporary.newFolder(".dualscreen-import-test")
        File(staging, "Existing").mkdir()
        assertThat(runCatching { SwitchModFolders.publish(staging, temporary.root, listOf("Existing")) }.isFailure).isTrue()
        assertThat(existing.isDirectory).isTrue()
        assertThat(File(staging, "Existing").isDirectory).isTrue()
    }
}
