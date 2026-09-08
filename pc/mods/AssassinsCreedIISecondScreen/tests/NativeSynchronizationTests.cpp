#include "NativeSynchronizationFixture.h"
int main(){
    Fixture f;auto s=ReadSynchronization(f,f.roots);assert(s);
    assert(s->earned==175 && s->maximum==300 && s->storyEarned==150 && s->storyMaximum==200);
    assert(s->side[0].total==1 && s->side[0].completed==0 && s->side[0].completedValue==25);
    assert(s->story.completed==1 && s->story.total==2 && s->sequences.completed==0 && s->sequences.total==1);
    Fixture complete;complete.Word(0x5011c,100);complete.Word(0x8001c,100);complete.Word(0x6000c,282300);
    auto finished=ReadSynchronization(complete,complete.roots);
    assert(finished && finished->story.completed==2 && finished->sequences.completed==1 &&
        finished->side[0].completed==1 && finished->sideTitles[0]==282300);
    auto calls=f.calls;
    for(size_t i=1;i<=calls;++i){Fixture broken;broken.fail=i;assert(!ReadSynchronization(broken,broken.roots));}
    for(uint32_t a:{0x223f81cu,0x2212490u,0x223f814u,0x16000u,0x20008u,0x21000u,0x22008u,0x30024u,
            0x32000u,0x40000u,0x4003cu,0x5001cu,0x20010u,0x60018u,0x70000u,0x70008u,0x72000u,0x8001cu}){
        Fixture changed;changed.mutate=a;assert(!ReadSynchronization(changed,changed.roots));
    }
    Fixture dlc;dlc.Word(0x40008,2);s=ReadSynchronization(dlc,dlc.roots);assert(s && s->earned==75 && s->maximum==200);
    dlc.Word(0x40008,6);s=ReadSynchronization(dlc,dlc.roots);assert(s && s->earned==175 && s->maximum==300);
    Fixture dlcChanged;dlcChanged.Word(0x40008,4);dlcChanged.mutate=0x902c0;assert(!ReadSynchronization(dlcChanged,dlcChanged.roots));
    Fixture unloaded;unloaded.Word(0x42008,0);s=ReadSynchronization(unloaded,unloaded.roots);assert(s && s->earned==75 && s->maximum==300);
    unloaded.Word(0x32008,0);s=ReadSynchronization(unloaded,unloaded.roots);assert(s && s->earned==75 && s->maximum==200);
    Fixture unknown;unknown.Word(0x70000,0x1e9d059);assert(!ReadSynchronization(unknown,unknown.roots));
    Fixture invalid;invalid.Word(0x8001c,101);assert(!ReadSynchronization(invalid,invalid.roots));
    Fixture empty;empty.Word(0x30028,0);empty.Word(0x23008,0);assert(!ReadSynchronization(empty,empty.roots));
    Fixture neighbor;neighbor.mutate=0x80020;assert(ReadSynchronization(neighbor,neighbor.roots));
    std::puts("Native weighted synchronization, DLC, unreadable state and transition tests passed");
}
