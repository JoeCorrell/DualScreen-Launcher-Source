"""Validate exported native clips/cues and execute production held-placement math."""
import json
import math
import os
import subprocess
import wave
from pathlib import Path

MOD = Path(__file__).resolve().parent
ROOT = MOD.parents[2]
ASSETS = MOD/'assets/botw/player'


def main():
    actors = json.loads((ASSETS/'item-presentation.json').read_text())['actors']
    for name in ('Item_Fruit_A', 'Item_Meat_02', 'Item_Meat_11'):
        assert actors[name] == 'EatFood'
    assert actors['Item_Cook_A_01'] == 'EatCookingSkewer'
    assert actors['Item_Cook_C_17'] == 'Drink'
    assert actors['Item_Cook_O_01'] == 'EatBizarre'
    assert actors['Item_Cook_O_02'] == 'EatTooHard'
    clips = json.loads((ASSETS/'animations.json').read_text())['clips']
    assert set(actors.values()) <= clips.keys()
    cues = json.loads((ASSETS/'sounds/cues.json').read_text())['clips']
    for key, clip in clips.items():
        frames = clip['frames']
        assert frames[0]['timeMs'] == 0
        assert frames[-1]['timeMs'] == clip['durationMs']
        assert all(a['timeMs'] < b['timeMs'] <= a['timeMs']+34 for a,b in zip(frames, frames[1:]))
        assert all(len(m)==16 and all(map(math.isfinite,m)) for f in frames for m in f['bones'].values())
        assert clip['holdLastFrame'] == key.startswith('Hold')
        if key.startswith('Hold'):
            continue
        timeline = cues[clip['source']]
        assert timeline
        assert timeline == sorted(timeline, key=lambda c:c['timeMs'])
        for cue in timeline:
            assert 0 <= cue['timeMs'] < cue['endMs'] <= clip['durationMs']
            for filename in cue.get('alternatives', [cue['file']]):
                with wave.open(str(ASSETS/'sounds'/filename)) as audio:
                    assert audio.getnchannels() in (1,2) and audio.getsampwidth()==2
                    assert 0 < audio.getnframes()*audio.getnchannels()*2 < 1024*1024
    print('PASS: 399 actor selectors; native one-shot clips at 30 Hz; 35 timed cues; all PCM assets')
    work = ROOT/'.work/botw-presentation/tests'
    work.mkdir(parents=True, exist_ok=True)
    harness = work/'PlacementTest.kt'
    harness.write_text('''package com.dualscreen.mods.botw
fun main() {
    fun matrix(scale:Float,y:Float)=floatArrayOf(scale,0f,0f,0f,0f,scale,0f,0f,0f,0f,scale,0f,0f,y,0f,1f)
    val spawn=BotwHeldPreview(1,emptyMap(),listOf(
        BotwHeldModel(11,"Item_Meat_02",matrix(1f,.8f)),
        BotwHeldModel(12,"Item_Meat_11",matrix(1f,.9f))))
    val settled=spawn.copy(items=spawn.items.map { it.copy(handLocal=matrix(.3f,.11f)) })
    val captured=settled.stationaryAttachments(spawn)
    check(captured.items.all { it.handLocal.contentEquals(matrix(.3f,.11f)) })
    // Movement after scale settling cannot shake the portrait or move its pivots.
    for (i in -100..100) {
        val moving=settled.copy(items=settled.items.map { it.copy(handLocal=matrix(.3f,i.toFloat())) })
        val retained=moving.stationaryAttachments(captured)
        check(retained.items.indices.all { retained.items[it] === captured.items[it] })
    }
    val single=settled.copy(items=listOf(settled.items.first()))
    check(single.stationaryAttachments(captured)===single)
    val reordered=settled.copy(items=settled.items.reversed())
    check(reordered.stationaryAttachments(captured)===reordered)
    val released=settled.copy(items=emptyList())
    check(released.stationaryAttachments(captured).items.isEmpty())
    println("PASS: production meat scale/pivot settlement; 201 movement samples; single/multi/reorder/release")
}
''')
    cache=Path(os.environ['USERPROFILE'])/'.gradle/caches/modules-2/files-2.1'
    def jar(group,name,version):
        files=list((cache/group/name/version).glob(f'*/{name}-{version}.jar'))
        assert len(files)==1
        return str(files[0])
    std=jar('org.jetbrains.kotlin','kotlin-stdlib','1.9.24')
    annotations=jar('org.jetbrains','annotations','13.0')
    deps=[std,annotations,jar('org.json','json','20250107')]
    compiler=[jar('org.jetbrains.kotlin','kotlin-compiler-embeddable','1.9.24'),std,annotations,
              jar('org.jetbrains.kotlin','kotlin-reflect','1.6.10'),
              jar('org.jetbrains.kotlin','kotlin-script-runtime','1.9.24'),
              jar('org.jetbrains.intellij.deps','trove4j','1.0.20200330')]
    java=Path('C:/Program Files/Microsoft/jdk-17.0.19.10-hotspot/bin/java.exe')
    subprocess.run([str(java),'-Xmx384m','-cp',os.pathsep.join(compiler),
                    'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler','-no-stdlib','-no-reflect',
                    '-classpath',os.pathsep.join(deps),'-d',str(work/'classes'),
                    str(MOD/'ui/src/main/java/com/dualscreen/mods/botw/BotwHeldPreview.kt'),
                    str(harness)],check=True)
    subprocess.run([str(java),'-Xmx64m','-cp',os.pathsep.join([str(work/'classes')]+deps),
                    'com.dualscreen.mods.botw.PlacementTestKt'],check=True)


if __name__ == '__main__':
    main()
