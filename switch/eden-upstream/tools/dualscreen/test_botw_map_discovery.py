"""Exercise production map parsing/fog against native BECO, no APK/Gradle."""
import argparse
import os
from pathlib import Path
import subprocess
import oead


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--java',type=Path,required=True)
    p.add_argument('--gradle-cache',type=Path,required=True)
    p.add_argument('--bootup',type=Path,required=True)
    p.add_argument('--work',type=Path,required=True)
    a=p.parse_args()
    a.work.mkdir(parents=True,exist_ok=True)
    pack=oead.Sarc(a.bootup.read_bytes())
    matches=[f for f in pack.get_files() if f.name and f.name.endswith('/MapTower.sbeco')]
    if len(matches)!=1:raise RuntimeError('Expected exactly one native MapTower.sbeco')
    raw=bytes(matches[0].data)
    raw=bytes(oead.yaz0.decompress(raw)) if raw[:4]==b'Yaz0' else raw
    fixture=a.work/'MapTower.beco'
    fixture.write_bytes(raw)
    stub=a.work/'Path.kt'
    stub.write_text('''package android.graphics
class Path {
    enum class Direction { CW }
    val rects=mutableListOf<FloatArray>()
    fun addRect(l:Float,t:Float,r:Float,b:Float,d:Direction) { check(l<r&&t<b);rects.add(floatArrayOf(l,t,r,b)) }
    fun covers(x:Float,z:Float)=rects.any{x>=it[0]&&x<it[2]&&z>=it[1]&&z<it[3]}
}
''')
    test=a.work/'MapTest.kt'
    test.write_text('''package com.dualscreen.mods.botw
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.*
import kotlin.random.Random
import org.json.JSONObject
fun main(args:Array<String>) {
    val bytes=File(args[0]).readBytes()
    val native=ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
    val rows=native.getInt(4);val div=native.getInt(8);val data=16+4*rows
    // Independent translation of the native lookup, not production runs().
    fun reference(x:Float,z:Float):Int {
        val row=min(rows-2,((z.coerceIn(-4000f,4000f)+4000f).roundToInt()/div))
        var column=(x.coerceIn(-5000f,4999f)+5000f).roundToInt()/(if(div==10)10 else 1)
        var at=data+native.getInt(16+4*row)*2
        val end=data+native.getInt(20+4*row)*2
        while(at<end) { val width=native.getShort(at+2).toInt()
            if(column<width)return native.getShort(at).toInt()
            column-=width;at+=4 }
        error("invalid native run")
    }
    val regions=BotwMapRegions(bytes)
    val rng=Random(123)
    var checks=0
    for(open in listOf(emptySet(),setOf(0),setOf(6),setOf(0,4,7,14),(0..14).toSet())) {
        val flags=open.map{"MapTower_%02d".format(it+1)}.toSet()
        val path=regions.hiddenPath(flags)
        repeat(1500) {
            val x=rng.nextFloat()*11999-5999.5f;val z=rng.nextFloat()*11999-5999.5f
            val expected=reference(x,z)
            check(regions.regionAt(x,z)==expected)
            check(path.covers(x,z)==(expected !in open)) { "fog mismatch at $x,$z in region $expected" }
            checks++
        }
    }
    val hex=bytes.joinToString(""){"%02x".format(it.toInt() and 255)}
    val root=JSONObject().put("status","ok").put("revision","save:1").put("regions",hex)
        .put("visible",org.json.JSONArray(listOf("MapTower_07")))
        .put("userMarkers",org.json.JSONArray())
    val first=requireNotNull(BotwMapDiscovery.parse(root.toString(),null))
    root.remove("regions")
    check(BotwMapDiscovery.parse(root.toString(),first)===first)
    root.put("userMarkers",org.json.JSONArray("[{\\"slot\\":0,\\"icon\\":37,\\"x\\":1,\\"z\\":2}]"))
    val second=requireNotNull(BotwMapDiscovery.parse(root.toString(),first))
    check(second!==first&&second.fog===first.fog&&second.userMarkers.single().x==1f)
    root.put("userMarkers",org.json.JSONArray())
    check(BotwMapDiscovery.parse(root.toString(),second)!!.userMarkers.isEmpty())
    root.put("revision","save:2")
    check(runCatching{BotwMapDiscovery.parse(root.toString(),first)}.isFailure)
    check(BotwMapDiscovery.parse("{\\"status\\":\\"unavailable\\"}",first)==null)
    check(runCatching{BotwMapRegions(ByteArray(24))}.isFailure)
    println("PASS: $checks native region/fog samples, save revisions, pin changes/removal, fail-closed unavailable state")
}
''')
    def jar(group,name,version):
        found=list((a.gradle_cache/group/name/version).glob(f'*/{name}-{version}.jar'))
        if len(found)!=1:raise RuntimeError(f'Missing/ambiguous {name}')
        return str(found[0])
    std=jar('org.jetbrains.kotlin','kotlin-stdlib','1.9.24')
    annotations=jar('org.jetbrains','annotations','13.0')
    json=jar('org.json','json','20250107')
    compiler=[jar('org.jetbrains.kotlin','kotlin-compiler-embeddable','1.9.24'),std,annotations,
        jar('org.jetbrains.kotlin','kotlin-reflect','1.6.10'),
        jar('org.jetbrains.kotlin','kotlin-script-runtime','1.9.24'),
        jar('org.jetbrains.intellij.deps','trove4j','1.0.20200330')]
    source=Path(__file__).resolve().parents[3]/'mods/botw/ui/src/main/java/com/dualscreen/mods/botw/BotwMapDiscovery.kt'
    classes=a.work/'classes'
    subprocess.run([str(a.java),'-Xmx384m','-cp',os.pathsep.join(compiler),
        'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler','-no-stdlib','-no-reflect',
        '-classpath',os.pathsep.join([std,annotations,json]),'-d',str(classes),
        str(source),str(stub),str(test)],check=True)
    subprocess.run([str(a.java),'-Xmx128m','-cp',os.pathsep.join([str(classes),std,json]),
        'com.dualscreen.mods.botw.MapTestKt',str(fixture)],check=True)


if __name__=='__main__':main()
