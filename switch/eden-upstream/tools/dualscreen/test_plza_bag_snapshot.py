"""Compile the production bag-to-UI mapper against deterministic native records.

No game file/memory writes. The absent save-block stub intentionally makes a
regression to the old encrypted snapshot source fail compilation.
"""
import argparse
import os
from pathlib import Path
import subprocess
from test_botw_carry_grab import function


def run(compiler, work):
    root = Path(__file__).resolve().parents[2]
    source = (root / 'src/android/app/src/main/jni/plza_companion.cpp').read_text()
    header = r'''
#include "plza_inventory_reader.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;using s32=int32_t;using VAddr=uint64_t;
#define LOG_INFO(...) do{}while(false)
namespace Core::Memory {struct Memory {};}
constexpr size_t ItemRecordCount=3000,SnapshotItemCapacity=512,InlineItemCapacity=20;
struct ItemEntry {u32 item_id{};u16 quantity{};u8 pocket{},flags{};};
struct SnapshotFrame {u32 item_count{};ItemEntry items[20]{},extra_items[492]{};};
struct TelemetryType {
    bool live_bag_log_initialized{},live_bag_log_available{},item_log_initialized{};
    VAddr logged_live_bag_owner{};
    std::array<std::array<u32,3>,ItemRecordCount> logged_items{};
} Telemetry;
PlzaInventory::Records NativeBag;
bool Available=true;
bool ReadCurrentBag(Core::Memory::Memory&,VAddr,PlzaInventory::Records& out,VAddr& owner) {
    owner=Available?0x100000:0;
    if(!Available)return false;
    out=NativeBag;return true;
}
'''
    main = r'''
int main(){
    Core::Memory::Memory memory;
    for(auto& record:NativeBag)record.pouch=UINT32_MAX;
    NativeBag[3]={1,6,1,0};NativeBag[17]={0,44,0,0};
    SnapshotFrame frame;
    assert(PopulateItems(memory,0x100000000,frame));
    assert(frame.item_count==2 && frame.items[0].item_id==3 && frame.items[1].quantity==44);
    NativeBag[17].quantity=43;frame={};
    assert(PopulateItems(memory,0x100000000,frame));
    assert(frame.items[1].quantity==43 && Telemetry.logged_items[17][1]==43);
    NativeBag[17].quantity=0;frame={};
    assert(PopulateItems(memory,0x100000000,frame));
    assert(frame.item_count==1 && frame.items[0].item_id==3 && frame.items[1].item_id==0);
    Available=false;frame={};
    assert(!PopulateItems(memory,0x100000000,frame));
    assert(frame.item_count==0 && !Telemetry.live_bag_log_available);
    Available=true;
    for(size_t i=1;i<NativeBag.size();++i)NativeBag[i]={1,1,1,0};
    frame={};assert(PopulateItems(memory,0x100000000,frame));
    assert(frame.item_count==512 && frame.extra_items[491].item_id==512);
    // Output capacity must not prevent recording the rest of the native deltas.
    assert(Telemetry.logged_items[2999][1]==1);
    std::cout<<"PASS production snapshot: native quantity, final stack, unavailable, capacity\n";
}
'''
    work.mkdir(parents=True, exist_ok=True)
    harness = work / 'plza-bag-snapshot.cpp'
    harness.write_text(header + function(source, 'MapPouch') + '\n' +
                       function(source, 'PopulateItems') + main)
    env = os.environ.copy()
    env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
    exe = work / 'plza-bag-snapshot.exe'
    subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-I', str(root / 'src/android/app/src/main/jni'), str(harness),
                    '-o', str(exe)], check=True, env=env)
    subprocess.run([str(exe)], check=True, env=env)
    action = function(source, 'PerformItemAction')
    assert 'ReadCurrentBag' in action and 'ReadObjectBlock' not in action
    snapshot = function(source, 'PopulateSnapshotData')
    assert 'candidate.availability &= ~Availability::Bag;' in snapshot


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    run(args.compiler, args.work)
