#include "../src/AttachmentCapture.h"
#include <cassert>
#include <iostream>

int main() {
    c3::AttachmentCapture capture;
    assert(!capture.Read(1,10,100).items.known);
    capture.Clear(1,10);
    assert(capture.Add(1,10,20,30,"TEST default","TEST icon",true,{0,true},"",true));
    assert(capture.Add(1,10,20,31,"TEST attachment","TEST icon",true,{},"TEST detail"));
    assert(!capture.Read(1,10,100).items.known);
    assert(capture.Mounted(1,10,20,31));
    assert(capture.Update(1,10,100));
    auto result=capture.Read(1,10,100);
    assert(result.weapon==10 && result.items.complete && result.items.size()==2);
    assert(!result.items.entries[0].available.known && !result.items.entries[0].movieAttached);
    assert(result.items.entries[1].available.value && result.items.entries[1].movieAttached);
    assert(!result.items.entries[1].ammoCount.known);
    assert(!capture.Read(1,11,100).items.known && !capture.Read(2,10,100).items.known);
    assert(!capture.Read(1,10,99).items.known && !capture.Read(1,10,851).items.known);
    capture.Clear(1,10); assert(!capture.Read(1,10,101).items.known);
    assert(capture.Update(1,10,101)); assert(capture.Read(1,10,101).items.size()==0);
    assert(capture.Read(1,10,101).items.known);
    capture.Clear(1,10);
    assert(capture.Add(1,10,20,30,"TEST","",true,{},""));
    assert(!capture.Add(1,10,20,30,"DUPLICATE","",true,{},""));
    assert(!capture.Update(1,10,102));
    capture.Clear(1,10); assert(!capture.Mounted(1,10,20,30)); assert(!capture.Update(1,10,103));
    capture.Clear(1,10); assert(!capture.Add(2,10,20,30,"TEST","",true,{},""));
    assert(!capture.Update(1,10,104));
    capture.Clear(1,10); assert(!capture.Add(1,11,20,30,"TEST","",true,{},""));
    capture.Clear(1,10); assert(!capture.Add(1,10,20,30,std::string(192,'x'),"",true,{},""));
    capture.Clear(1,10);
    for (unsigned i=1;i<=12;++i) assert(capture.Add(1,10,20,i,"TEST","",true,{},""));
    assert(!capture.Add(1,10,20,13,"TEST","",true,{},"")); assert(!capture.Update(1,10,105));
    capture.Invalidate(2); assert(!capture.Read(1,10,105).items.known);
    std::cout<<"PASS: attachment transactions, default semantics, ownership, duplicate/overflow/stale rejection\n";
}
