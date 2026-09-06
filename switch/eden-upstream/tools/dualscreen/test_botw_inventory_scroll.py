"""Exercise production Kotlin scroll/hit-test code without Gradle or an APK build."""
import argparse
import os
from pathlib import Path
import re
import subprocess


def method(source, name):
    start = source.index('    private fun ' + name + '(')
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--java', type=Path, required=True)
    parser.add_argument('--gradle-cache', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    source = (Path(__file__).resolve().parents[2] /
              'src/android/app/src/main/java/org/yuzu/yuzu_emu/activities/BotwCompanionView.kt').read_text()
    constants = re.findall(r'        private const val GRID_\w+ =[ \t]*(?:\n            )?[^\n]+', source)
    state = source[source.index('    private var inventoryScrollY'):source.index('    private var controllerActive')]
    methods = '\n'.join(method(source, name) for name in [
        'maximumRowOffset', 'inventoryCellAt', 'inventoryItemAtCell',
        'setInventoryScroll', 'updateScrollbarPosition', 'clampSelection'])
    harness = '''import kotlin.math.*
class ScrollHarness {
    data class Item(val id: Long)
    private var items = emptyList<Item>()
    private var selectedItem = 0
    private var redraws = 0
    private fun filteredItems() = items
    private fun postInvalidateOnAnimation() { redraws++ }
''' + '    companion object {\n' + '\n'.join(constants) + '\n    }\n' + state + '\n' + methods + '''
    fun run() {
        var checks = 0
        for (size in listOf(0, 1, 19, 20, 21, 25, 41, 100, 260)) {
            items = (0 until size).map { Item(it.toLong()+1) }
            rowOffset = 0
            for (offset in listOf(-10f, 0f, 1f, 63.5f, 127.9f, 128f, 129f, 1023.5f, 99999f)) {
                setInventoryScroll(offset)
                val expected = offset.coerceIn(0f, maximumRowOffset()*128f)
                check(inventoryScrollY == expected)
                val before = inventoryScrollY
                clampSelection()
                check(inventoryScrollY == before) // full refresh must preserve sub-row pixels
                val previousRedraws = redraws
                setInventoryScroll(before)
                check(redraws == previousRedraws)
                for (dy in listOf(0.25f, 40f, 121.9f, 123f, 127.9f, 255.5f, 505.75f)) {
                    val y = GRID_START_Y+dy
                    val contentY = dy+inventoryScrollY
                    val expectedRow = (contentY/128f).toInt()
                    val cell = inventoryCellAt(GRID_START_X+10f, y)
                    if (contentY%128f > GRID_CELL_HEIGHT) {
                        check(cell == null) // the gaps never activate an adjacent item
                    } else {
                        check(cell != null)
                        check(rowOffset*GRID_COLUMNS+cell == expectedRow*GRID_COLUMNS)
                        val item = inventoryItemAtCell(cell)?.second
                        check(item?.id == items.getOrNull(expectedRow*GRID_COLUMNS)?.id)
                    }
                    checks++
                }
                check(inventoryCellAt(GRID_START_X+1f, GRID_START_Y-1f) == null)
                check(inventoryCellAt(GRID_START_X+1f, GRID_BOTTOM+1f) == null)
                check(inventoryCellAt(GRID_START_X+GRID_CELL_WIDTH+1f, GRID_START_Y) == null)
            }
            updateScrollbarPosition(GRID_BOTTOM)
            check(inventoryScrollY == maximumRowOffset()*128f)
            updateScrollbarPosition(GRID_START_Y)
            check(inventoryScrollY == 0f)
        }
        println("PASS: $checks production Kotlin hit-tests, fractional refresh stability, scroll limits and redraw coalescing")
    }
}
fun main() { ScrollHarness().run() }
'''
    args.work.mkdir(parents=True, exist_ok=True)
    kt = args.work / 'InventoryScrollTest.kt'
    kt.write_text(harness)
    def jar(group, artifact, version):
        matches = list((args.gradle_cache/group/artifact/version).glob(f'*/{artifact}-{version}.jar'))
        if len(matches) != 1:
            raise RuntimeError(f'Missing/ambiguous cached {artifact} {version}')
        return str(matches[0])
    stdlib = jar('org.jetbrains.kotlin', 'kotlin-stdlib', '1.9.24')
    annotations = jar('org.jetbrains', 'annotations', '13.0')
    compiler = jar('org.jetbrains.kotlin', 'kotlin-compiler-embeddable', '1.9.24')
    reflect = jar('org.jetbrains.kotlin', 'kotlin-reflect', '1.6.10')
    script = jar('org.jetbrains.kotlin', 'kotlin-script-runtime', '1.9.24')
    trove = jar('org.jetbrains.intellij.deps', 'trove4j', '1.0.20200330')
    classes = args.work/'classes'
    subprocess.run([str(args.java), '-Xmx384m', '-cp', os.pathsep.join([
        compiler, stdlib, annotations, reflect, script, trove]),
        'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler', '-no-stdlib', '-no-reflect',
        '-classpath', os.pathsep.join([stdlib, annotations]), '-d', str(classes), str(kt)], check=True)
    subprocess.run([str(args.java), '-Xmx64m', '-cp', os.pathsep.join([str(classes), stdlib]),
                    'InventoryScrollTestKt'], check=True)
    page = method(source, 'drawInventoryPage')
    assert 'Loading exact inventory textures' not in page
    assert 'if (!inventoryTexturesReady)' not in page
    print('PASS: no whole-grid texture-readiness gate')


if __name__ == '__main__':
    main()
