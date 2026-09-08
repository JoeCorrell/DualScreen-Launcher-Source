#include "../src/CommandInbox.h"
#include <cassert>

int main() {
    using namespace c3;
    Snapshot s{}; s.adapterVerified=true; s.phase=Phase::Gameplay;
    s.generation=1; s.sequence=2; s.sampledAt=100;
    s.toggleArmor={9,Action::ToggleArmor,true,true};
    Command tap{1,1,2,100,s.toggleArmor}, out{};
    {CommandInbox q; assert(q.Submit(s,tap,100)); assert(q.Take(s,100,out));
        assert(out.request==1); assert(!q.Take(s,100,out)); assert(out.request==0);
        assert(!q.Submit(s,tap,100));}
    {CommandInbox q; auto next=tap; next.request=2;
        assert(q.Submit(s,tap,100)); assert(!q.Submit(s,next,100));
        assert(q.Take(s,100,out)); assert(!q.Submit(s,next,100));}
    {CommandInbox q; assert(q.Submit(s,tap,100)); assert(q.Cancel());
        assert(!q.Take(s,100,out)); assert(!q.Submit(s,tap,100));}
    {CommandInbox q; assert(q.Submit(s,tap,100)); auto changed=s; ++changed.sequence;
        assert(!q.Take(changed,100,out)); assert(!q.Take(s,100,out));}
    {CommandInbox q; assert(q.Submit(s,tap,100)); auto paused=s; paused.phase=Phase::Paused;
        assert(!q.Take(paused,100,out)); assert(!q.Take(s,100,out));}
    {CommandInbox q; assert(q.Submit(s,tap,100)); auto changed=s; changed.toggleArmor.allowed=false;
        assert(!q.Take(changed,100,out));}
    {CommandInbox q; assert(q.Submit(s,tap,100)); assert(!q.Take(s,851,out));}
    {CommandInbox q; auto trial=s; trial.readOnlyTrial=true;
        assert(!q.Submit(trial,tap,100)); assert(!q.Take(s,100,out));}
    {CommandInbox q; auto forged=tap; forged.capability.key=10;
        assert(!q.Submit(s,forged,100));}
    {CommandInbox q; auto future=tap; future.issuedAt=101;
        assert(!q.Submit(s,future,100));}
    {CommandInbox q; assert(q.Submit(s,tap,100)); auto newer=s; ++newer.generation;
        auto next=tap; next.generation=newer.generation;
        assert(q.Submit(newer,next,100)); assert(!q.Submit(s,tap,100));
        assert(q.Take(newer,100,out)); assert(out.generation==newer.generation);}
}
