"""Compile production preview parser/math on the JVM, without Gradle or an APK."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import zipfile


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--java', type=Path, required=True)
    p.add_argument('--gradle-cache', type=Path, required=True)
    p.add_argument('--capture', type=Path, required=True)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--android-jar', type=Path,
                   help='Also type-check the complete renderer against Android and cached Filament APIs')
    a = p.parse_args()
    a.work.mkdir(parents=True, exist_ok=True)
    capture = json.loads(a.capture.read_text(encoding='utf-8-sig'))
    skeleton = capture['units'][0]['skeleton']
    raw = bytes.fromhex(skeleton['resourceBones'])
    matrices = bytes.fromhex(skeleton['worldMatrices'])
    bones = []
    for i, name in enumerate(skeleton['names']):
        parent = struct.unpack_from('<H', raw, i*80+26)[0]
        world = list(struct.unpack_from('<16f', matrices, i*64))
        world[15] = 1
        bones.append(['Armature_'+name, -1 if parent == 65535 else parent, world])
    # Root here is the captured skeleton's world root, which makes its local identity.
    # Every remaining native parent/child pair is exercised, including Weapon_R.
    fixture = {'status':'ok','player':2,'root':bones[0][2], 'bones':bones,
               'items':[{'id':3,'actor':'Item_Fruit_A','world':bones[50][2]},
                        {'id':4,'actor':'Item_Fruit_A','world':bones[50][2]}]}
    fixture_path = a.work/'native-pose.json'
    fixture_path.write_text(json.dumps(fixture))
    renderer = (Path(__file__).resolve().parents[2]/
        'src/android/app/src/main/java/org/yuzu/yuzu_emu/activities/BotwPlayerRenderView.kt').read_text()
    def method(name):
        start = renderer.index('fun '+name+'(')
        brace = renderer.index('{', start)
        depth, end = 1, brace+1
        while depth:
            depth += (renderer[end] == '{') - (renderer[end] == '}')
            end += 1
        return renderer[start:end]
    # Exercise the actual portrait-cache/wake methods with renderer side effects
    # counted, without starting an Android VM or constructing a Filament engine.
    lifecycle = '''
private enum class PlayerRenderProfile { Botw, Sshd }
private data class BotwPlayerAppearance(val weapon:String?=null)
private class Portrait(val width:Int=100,val height:Int=100,val isRecycled:Boolean=false)
private object Looper { fun myLooper()=1;fun getMainLooper()=1 }
private class Handler { fun removeCallbacks(r:Any) {} }
private class PreviewLifecycle {
    val profile=PlayerRenderProfile.Botw
    var shouldBeVisible=true;var released=false;var releasing=false
    var appearance:BotwPlayerAppearance?=BotwPlayerAppearance()
    var cachedAppearance=appearance;var frameBitmap:Portrait?=Portrait()
    var engine:Any?=null;val mainHandler=Handler();val heldPreviewPoll=Any()
    var polls=0;var starts=0;var alpha=0f;var visibility=0;var bodyReady=false
    val indexedAssets=listOf(1);val playerPipelineMode=1
    val viewportWidth=100;val viewportHeight=100;var lastReportedHeldItemCount=0
    val INVISIBLE=0;val VISIBLE=1
    fun invalidate() {}
    fun cancelPreviewAction(clearHeldItem:Boolean) {}
    fun releaseRenderer(keepBitmap:Boolean,terminal:Boolean) { engine=null }
    fun beginAppearanceRender(a:BotwPlayerAppearance) { starts++;engine=Any() }
    fun pollHeldPreview() { if(engine!=null) polls++ }
'''+method('wakeHeldPreview')+'\n'+method('setAppearance')+'''
}
private fun testCachedHoldStartup() {
    val view=PreviewLifecycle()
    view.setAppearance(BotwPlayerAppearance(),true)
    check(view.starts==0) // idle portrait remains cached
    view.wakeHeldPreview() // first Hold, without any Eat/menu initialization
    check(view.starts==1 && view.polls==1 && view.cachedAppearance==null)
    view.wakeHeldPreview();check(view.starts==1 && view.polls==2)
    val reentry=PreviewLifecycle();reentry.lastReportedHeldItemCount=2
    reentry.setAppearance(BotwPlayerAppearance(),true)
    check(reentry.starts==1) // opening bottom screen with held collection
    val hidden=PreviewLifecycle();hidden.shouldBeVisible=false
    hidden.wakeHeldPreview();check(hidden.starts==0 && hidden.polls==0)
    val released=PreviewLifecycle();released.released=true
    released.wakeHeldPreview();check(released.starts==0)
    println("PASS: production cached-portrait Hold startup, held reentry, duplicate wake, visibility/lifecycle guards")
}
'''
    harness = a.work/'HeldTransformsTest.kt'
    harness.write_text('''package org.yuzu.yuzu_emu.activities
import java.io.File
import org.json.JSONObject
import kotlin.math.*
private fun near(a: FloatArray,b: FloatArray) {
    check(a.indices.all { abs(a[it]-b[it]) < 0.003f }) { "transform mismatch" }
}
fun main(args:Array<String>) {
    testCachedHoldStartup()
    val identity=floatArrayOf(1f,0f,0f,0f,0f,1f,0f,0f,0f,0f,1f,0f,0f,0f,0f,1f)
    var cases=0
    for (angle in -180..180 step 5) for (scale in listOf(.25f,1f,2f)) {
        val t=angle*PI.toFloat()/180
        val p=floatArrayOf(cos(t)*scale,0f,-sin(t)*scale,0f,0f,scale,0f,0f,
            sin(t)*scale,0f,cos(t)*scale,0f,1838f,257f,912f,1f)
        val c=identity.copyOf().also { it[12]=.2f;it[13]=.11f;it[14]=-.15f }
        val w=BotwHeldTransforms.multiply(p,c)
        near(BotwHeldTransforms.relative(p,w),c)
        near(BotwHeldTransforms.multiply(p,BotwHeldTransforms.inverse(p)),identity)
        val blended=BotwHeldTransforms.interpolate(identity,p,.5f)
        // Rotation interpolation must not collapse the hand/basis at 180 degrees.
        for(column in 0..2) {
            val length=sqrt((0..2).sumOf { row ->
                (blended[column*4+row]*blended[column*4+row]).toDouble() }).toFloat()
            check(abs(length-(1f+scale)/2f)<.003f)
        }
        near(BotwHeldTransforms.interpolate(p,p,.5f),p)
        near(BotwHeldTransforms.interpolate(identity,p,0f),identity)
        near(BotwHeldTransforms.interpolate(identity,p,1f),p)
        cases++
    }
    val text=File(args[0]).readText()
    val state=JSONObject(text)
    val pose=requireNotNull(BotwHeldPreview.parse(text))
    check(pose.items.size==2 && pose.items[0].actor==pose.items[1].actor)
    check(pose.items[0].id!=pose.items[1].id)
    val worlds=mutableListOf<FloatArray>()
    val bones=state.getJSONArray("bones")
    fun matrix(array:org.json.JSONArray)=FloatArray(16){array.getDouble(it).toFloat()}
    for (i in 0 until bones.length()) {
        val b=bones.getJSONArray(i)
        val parent=b.getInt(1)
        val w=BotwHeldTransforms.multiply(if(parent<0) matrix(state.getJSONArray("root"))
            else worlds[parent],pose.bones.getValue(b.getString(0)))
        near(w,matrix(b.getJSONArray(2)))
        worlds.add(w)
    }
    near(pose.items[0].handLocal,identity)
    var stationary=pose
    for (angle in 0..360 step 5) {
        val t=angle*PI.toFloat()/180
        val sway=floatArrayOf(cos(t),sin(t),0f,0f,-sin(t),cos(t),0f,0f,
            0f,0f,1f,0f,angle*.001f,.2f,-.1f,1f)
        val moving=pose.copy(items=pose.items.map { it.copy(handLocal=sway) })
        stationary=moving.stationaryAttachments(stationary)
        stationary.items.forEach { near(it.handLocal,identity) }
    }
    // Spawn-time native scale still settles, but world movement cannot move the pivot.
    val scaled=identity.copyOf().also { it[0]=.5f;it[5]=.5f;it[10]=.5f;it[12]=10f }
    val resized=pose.copy(items=pose.items.map { it.copy(handLocal=scaled) })
        .stationaryAttachments(stationary)
    resized.items.forEach { near(it.handLocal,scaled.copyOf().also { m -> m[12]=0f }) }
    // Count, ordered identity, actor type, player and release invalidate the entire
    // layout: a second ingredient can rearrange the first, including duplicates.
    val changed=pose.copy(items=listOf(pose.items[0].copy(handLocal=scaled)))
    check(changed.stationaryAttachments(stationary)===changed)
    val reordered=pose.copy(items=pose.items.reversed())
    check(reordered.stationaryAttachments(stationary)===reordered)
    val replaced=pose.copy(items=pose.items.map { it.copy(id=it.id+10,handLocal=scaled) })
    check(replaced.stationaryAttachments(stationary)===replaced)
    val actorChanged=pose.copy(items=pose.items.map { it.copy(actor="Item_Meat_02") })
    check(actorChanged.stationaryAttachments(stationary)===actorChanged)
    val playerChanged=pose.copy(playerId=99)
    check(playerChanged.stationaryAttachments(stationary)===playerChanged)
    val released=pose.copy(items=emptyList())
    check(released.stationaryAttachments(stationary).items.isEmpty())
    check(pose.stationaryAttachments(null)===pose)
    println("PASS: stationary multi-item portrait across 73 movement/rotation samples, native scale settling, collection/release/player invalidation")
    check(BotwHeldPreview.parse("{\\"status\\":\\"unavailable\\"}")==null)
    check(BotwHeldPreview.parse("{\\"status\\":\\"ok\\",\\"items\\":[]}")!!.items.isEmpty())
    state.getJSONArray("items").getJSONObject(1).put("id",3)
    check(runCatching{BotwHeldPreview.parse(state.toString())}.isFailure)
    check(runCatching{BotwHeldTransforms.inverse(FloatArray(16))}.isFailure)
    println("PASS: $cases rotation/scale/translation cases, ${bones.length()} captured native bones, duplicate materials, release and invalid-state rejection")
}
'''+lifecycle)
    def jar(group, name, version):
        matches = list((a.gradle_cache/group/name/version).glob(f'*/{name}-{version}.jar'))
        if len(matches) != 1:
            raise RuntimeError(f'Missing/ambiguous cached {name}')
        return str(matches[0])
    std = jar('org.jetbrains.kotlin','kotlin-stdlib','1.9.24')
    annotations = jar('org.jetbrains','annotations','13.0')
    json_jar = jar('org.json','json','20250107')
    compiler = [jar('org.jetbrains.kotlin','kotlin-compiler-embeddable','1.9.24'),std,
                annotations,jar('org.jetbrains.kotlin','kotlin-reflect','1.6.10'),
                jar('org.jetbrains.kotlin','kotlin-script-runtime','1.9.24'),
                jar('org.jetbrains.intellij.deps','trove4j','1.0.20200330')]
    source = Path(__file__).resolve().parents[2]/'src/android/app/src/main/java/org/yuzu/yuzu_emu/activities/BotwHeldPreview.kt'
    classes = a.work/'classes'
    subprocess.run([str(a.java),'-Xmx384m','-cp',os.pathsep.join(compiler),
        'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler','-no-stdlib','-no-reflect',
        '-classpath',os.pathsep.join([std,annotations,json_jar]),'-d',str(classes),
        str(source),str(harness)],check=True)
    subprocess.run([str(a.java),'-Xmx64m','-cp',os.pathsep.join([str(classes),std,json_jar]),
        'org.yuzu.yuzu_emu.activities.HeldTransformsTestKt',str(fixture_path)],check=True)
    if a.android_jar:
        deps = [std, annotations, str(a.android_jar)]
        for artifact in ['filament-android', 'gltfio-android']:
            archives = list((a.gradle_cache/'com.google.android.filament'/artifact).glob('*/*/*.aar'))
            if len(archives) != 1:
                raise RuntimeError(f'Missing/ambiguous cached {artifact}: {archives}')
            destination = a.work/f'{artifact}.jar'
            with zipfile.ZipFile(archives[0]) as archive:
                destination.write_bytes(archive.read('classes.jar'))
            deps.append(str(destination))
        app_stub = a.work/'AppStubs.kt'
        app_stub.write_text('''package org.yuzu.yuzu_emu
object BuildConfig { const val SSHD_PLAYER_PIPELINE_MODE=1; const val BOTW_PLAYER_PIPELINE_MODE=1; const val BOTW_SNAPSHOT_POLL_MODE=2 }
object NativeLibrary {
    fun getBotwHeldPreview():String = error("compile stub only")
    fun getBotwMapDiscovery(revision:String):String = error("compile stub only")
    fun getBotwCompanionSnapshot():String = error("compile stub only")
    fun getBotwCompanionStatsSnapshot():String = error("compile stub only")
    fun performBotwCompanionAction(action:Int, argument:Long):Boolean = error("compile stub only")
    fun getDualScreenRomfsAsset(path:String):ByteArray? = error("compile stub only")
    fun getDualScreenRomfsRevision():String = error("compile stub only")
}
''')
        utils_stub = a.work/'UtilsStubs.kt'
        utils_stub.write_text('''package org.yuzu.yuzu_emu.utils
object DirectoryInitialization { val userDirectory:String = "" }
object Log { fun info(s:String) {} ; fun warning(s:String) {}; fun error(s:String) {} }
''')
        subprocess.run([str(a.java),'-Xmx384m','-cp',os.pathsep.join(compiler),
            'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler','-no-stdlib','-no-reflect',
            '-classpath',os.pathsep.join(deps),'-d',str(a.work/'view-classes'),
            str(source),str(source.with_name('BotwPlayerRenderView.kt')),
            str(source.with_name('BotwMapDiscovery.kt')),
            str(source.with_name('BotwCompanionView.kt')),
            str(source.with_name('BotwControllerBridge.kt')),
            str(app_stub),str(utils_stub)],check=True)
        print('PASS: complete BOTW/SSHD player renderer type-check against Android + Filament (no APK)')


if __name__ == '__main__':
    main()
