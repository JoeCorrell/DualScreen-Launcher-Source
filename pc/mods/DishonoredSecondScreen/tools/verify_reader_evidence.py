"""Read-only checks of native reader evidence; does not build or run the mod.

Requires pefile/capstone and the user's copied executable/package metadata.
This checks binary/schema contracts, not C++ correctness or live behavior.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import capstone
import pefile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=Path(".research/dishonored/original/Dishonored.exe"))
    parser.add_argument("--metadata", type=Path, default=Path(".work/dishonored/package/objects.json"))
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    digest = hashlib.sha256(args.exe.read_bytes()).hexdigest()
    if digest != "67ced86b1675b8d27c742a84be41eaa7eba41c748a7f4ec1fe05d107d4f91ae5":
        raise SystemExit("Unsupported executable: evidence checks require the exact investigated build")
    pe = pefile.PE(str(args.exe), fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    checks = []

    def pointer(address, expected, meaning):
        value, = struct.unpack("<I", pe.get_data(address - base, 4))
        if value != expected:
            raise AssertionError(f"{meaning}: {address:08x} -> {value:08x}, expected {expected:08x}")
        checks.append({"meaning": meaning, "address": f"{address:08x}", "pointer": f"{value:08x}"})

    def instruction(address, mnemonic, operands, meaning):
        decoded = next(decoder.disasm(pe.get_data(address - base, 15), address, count=1), None)
        if decoded is None or (decoded.mnemonic, decoded.op_str) != (mnemonic, operands):
            actual = None if decoded is None else (decoded.mnemonic, decoded.op_str)
            raise AssertionError(f"{meaning}: {address:08x}: {actual}, expected {(mnemonic, operands)}")
        checks.append({"meaning": meaning, "address": f"{address:08x}", "instruction": f"{mnemonic} {operands}"})

    instruction(0x00402C53, "mov", "ecx, dword ptr [ecx + 4]", "Rotator yaw is second int32")
    instruction(0x00402C6B, "mov", "edi, ecx", "Yaw sine index source")
    instruction(0x00402C79, "add", "ecx, 0x4000", "Yaw cosine quarter-turn offset")
    instruction(0x00402C7F, "sar", "edi, 2", "Yaw lookup quantization")
    instruction(0x00402C8B, "and", "edi, 0x3fff", "Yaw lookup wraps at 65536 rotator units")
    instruction(0x00402C91, "movss", "xmm2, dword ptr [edi*4 + 0x1417838]", "Native yaw sine lookup")
    instruction(0x00BDAD18, "mov", "ecx, dword ptr [0x1462794]", "Saved results fallback tweak class")
    instruction(0x00BDAD20, "call", "0x4967f0", "Saved results fallback default object getter")
    instruction(0x00BBC2D6, "mov", "dword ptr [0x1462794], eax", "Main menu tweak class registration global")
    pointer(0x0110F718 + 0x10, 0x00B48BA0, "Pawn tweak interface getter")
    instruction(0x00ABF473, "mov", "dword ptr [esi + 0x4bc], 0x110f718", "Pawn tweak interface receiver")
    instruction(0x00B48BA0, "mov", "eax, dword ptr [ecx + 0x28c]", "Tweak pointer at pawn+748")
    pointer(0x0110F784 + 8, 0x00B49270, "Pawn attributes interface getter")
    instruction(0x00ABF441, "mov", "dword ptr [esi + 0x4a8], 0x110f784", "Pawn attribute interface receiver")
    instruction(0x00B49270, "mov", "eax, dword ptr [ecx + 0x2d4]", "Attributes at pawn+77c")
    instruction(0x00AA8B83, "mov", "edi, dword ptr [eax + 0x210]", "Charm slot base")
    instruction(0x00AA8B89, "push", "0x1451824", "Charm slot modifier FName")
    instruction(0x00AA8B99, "call", "0xec32e0", "Charm slot truncating conversion")
    instruction(0x00AA8B9E, "add", "eax, edi", "Charm slot modifier addition")
    instruction(0x00C7424A, "mov", "eax, dword ptr [esi + 0x38]", "Attribute map hash size")
    instruction(0x00C74258, "mov", "edx, dword ptr [esi + 0x34]", "Attribute map secondary buckets")
    instruction(0x00C74260, "lea", "eax, [esi + 0x30]", "Attribute map inline bucket")
    instruction(0x00C74280, "lea", "eax, [edx + edx*2]", "Attribute sparse record stride factor3")
    instruction(0x00C74283, "shl", "eax, 5", "Attribute sparse record stride96")
    instruction(0x00C74294, "mov", "edx, dword ptr [eax + 0x58]", "Attribute collision chain")
    instruction(0x00C85EA2, "lea", "esi, [edi + 0x38]", "DisAttributes map field")
    instruction(0x00C85EC4, "lea", "esi, [eax + 8]", "Attribute value after FName key")
    instruction(0x00C85ECB, "test", "byte ptr [esi + 0xc], 2", "Cached attribute dirty flag")
    instruction(0x00C85ED4, "call", "0xc85db0", "Dirty getter mutates cached calculation")
    instruction(0x00C85ED9, "movss", "xmm0, dword ptr [esi + 0x10]", "Clean cached attribute value")
    instruction(0x004251CA, "mov", "eax, dword ptr [ebx + 0x10]", "Sparse bit-array secondary storage")
    instruction(0x004251F9, "mov", "eax, dword ptr [ebx + 0x14]", "Sparse bit-array length")
    instruction(0x00BD24F0, "mov", "eax, dword ptr [esi + 0xb38]", "Only collected charms")
    instruction(0x00BD24F6, "mov", "ecx, dword ptr [eax + edi*8 + 4]", "Owned charm definition index")
    instruction(0x00BD2505, "movzx", "esi, byte ptr [edi]", "Owned charm level")
    instruction(0x00BD2539, "mov", "eax, dword ptr [ebx + 8]", "Charm levels array")
    instruction(0x00BD2545, "lea", "ebx, [eax + edx*4]", "Charm level address after index*7")
    instruction(0x00BD258F, "mov", "eax, dword ptr [ebx]", "Charm localized name")
    instruction(0x00BD2605, "mov", "eax, dword ptr [ebx + 0xc]", "Charm localized effect text")
    pointer(0x01368110, 0x01175E42, "DisAbstractItem native registration name")
    instruction(0x00C4E7B2, "mov", "dword ptr [esi], 0x10eb5d0", "DisAbstractItem native vtable")
    pointer(0x010EB5D0 + 0x124, 0x00A4DC00, "Base readable getter")
    instruction(0x00A4DC0B, "mov", "eax, dword ptr [edi + 0x54]", "Readable FString length")
    instruction(0x00A4DC24, "mov", "edx, dword ptr [edi + 0x50]", "Readable FString characters")
    pointer(0x0110DB58 + 0x19c, 0x00AB6AB0, "Concrete controller tick hook")
    instruction(0x009CA746, "mov", "dword ptr [ebp - 0x80], 0x9ca440", "Native viewport WNDCLASSEX.lpfnWndProc identity")
    instruction(0x009CA7A3, "call", "dword ptr [0xf935d0]", "Native viewport RegisterClassExW")
    instruction(0x009C68F7, "mov", "dword ptr [ebx + 0x68], eax", "Native viewport owns CreateWindowExW result")
    pointer(0x01158FA0 + 0x230, 0x00BD95A0, "Journal native upgrades-list dispatch")
    pointer(0x01158FA0 + 0x234, 0x00BE02B0, "Journal native inventory-details dispatch")
    instruction(0x00BE0319, "mov", "eax, dword ptr [esi + 0x274]", "Inventory details use native journal records")
    instruction(0x00BE034F, "call", "0xbdf5f0", "Inventory record converted into native detail fields")
    instruction(0x00BD2287, "mov", "ebx, dword ptr [0x1452de8]", "Upgrades list obtains actual player pawn")
    instruction(0x00BD228D, "add", "ebx, 0xb50", "Owned upgrade array on Corvo")
    instruction(0x00BD22D8, "cmp", "byte ptr [edi + 0xd0], 6", "Native upgrade journal section predicate")
    instruction(0x00BD2321, "cmp", "dword ptr [edx + 0xe0], edi", "Owned successor conceals earlier upgrade tier")
    instruction(0x00BD235D, "mov", "eax, dword ptr [edi + 0xb8]", "Purchased upgrade localized title")
    instruction(0x00BD23A8, "mov", "eax, dword ptr [edi + 0xd4]", "Purchased upgrade native artwork path")
    instruction(0x00BD23E0, "mov", "eax, dword ptr [edi + 0xc4]", "Purchased upgrade native description")
    instruction(0x00C1AF04, "mov", "dword ptr [esi + 0x3c], 0x11630d4", "Inventory item attributes interface receiver")
    pointer(0x011630D4 + 8, 0x00BFEA80, "Inventory item attributes interface getter")
    instruction(0x00BFEA80, "mov", "eax, dword ptr [ecx + 0xa8]", "Live item attributes at item+e4")
    instruction(0x00C86419, "mov", "eax, dword ptr [edx + 8]", "Attribute interface dispatch slot")
    instruction(0x00C86420, "call", "0xc85e90", "Attribute interface calls modified value getter")
    instruction(0x009E70C1, "push", "0x10c7c34", "Melee damage attribute name literal")
    instruction(0x009E70D8, "mov", "dword ptr [0x145184c], ecx", "Melee damage FName storage")
    instruction(0x009E7325, "push", "0x10c811c", "Ranged damage attribute name literal")
    instruction(0x009E733C, "mov", "dword ptr [0x14518dc], ecx", "Ranged damage FName storage")
    instruction(0x009E735C, "mov", "dword ptr [0x14518e4], ecx", "Headshot multiplier FName storage")
    instruction(0x00C078F1, "push", "0x145184c", "Melee context reads weapon damage attribute")
    instruction(0x00C25291, "push", "0x14518e4", "Ranged context reads headshot multiplier")
    instruction(0x00C252B6, "push", "0x14518dc", "Ranged context reads weapon damage attribute")
    pointer(0x0110F7D0 + 0x728, 0x00AB5100, "Corvo teardown hook")
    pointer(0x0110F7D0 + 0x118, 0x00AB8790, "Corvo custom save restoration hook")
    instruction(0x00AB8BDE, "ret", "8", "Corvo restore consumes archive and flags stack arguments")
    pointer(0x010D7B48 + 0x114, 0x00A223B0, "Map-note custom save writer")
    pointer(0x010D7B48 + 0x118, 0x00A31810, "Map-note custom save reader")
    pointer(0x010E7FA8 + 0x124, 0x00A51500, "Audiograph native readable getter")
    instruction(0x00A515B7, "ret", "4", "Native readable getter consumes one FString output argument")
    pointer(0x0110F620 + 0x60, 0x00AA2030, "Corvo speaker hook-parameter getter")
    instruction(0x00AA2030, "mov", "ecx, dword ptr [ecx + 0x510]", "Speaker interface resolves pawn+9dc audio-log actor")
    instruction(0x00A5B16C, "mov", "dword ptr [esi], 0x10ee498", "Native audio-log dummy actor vtable")
    pointer(0x010EE420 + 0x64, 0x00848560, "Audio-log speaker component getter")
    instruction(0x00848560, "mov", "eax, dword ptr [ecx + 8]", "Audio-log actor+250 conversation component")
    instruction(0x00C8B5EE, "test", "byte ptr [eax + 0x4c], 1", "Conversation component readiness")
    instruction(0x00C8B5F4, "add", "eax, 0x124", "Native conversation hook-parameter storage")
    instruction(0x00430F5A, "ret", "", "Native FString destructor has no stack parameters")
    instruction(0x00BD42BC, "mov", "edx, dword ptr [edi + 0x94]", "Mission narrative summary definitions")
    instruction(0x00BD42C2, "lea", "eax, [esi + esi*8]", "Summary stride is nine words")
    instruction(0x00BD42FD, "jne", "0xbd4335", "Unmatched saved summary predicates conceal description")
    instruction(0x00BD4B6E, "mov", "ebx, dword ptr [edx + 0x18]", "Native acquired-map legend storage")
    instruction(0x00BD4B9B, "mov", "esi, dword ptr [ebx + esi + 4]", "Legend localized description FString")
    instruction(0x00C04ABE, "mov", "edi, dword ptr [eax + 0x204]", "Native mana elixir capacity")
    instruction(0x00C04AC6, "mov", "edi, dword ptr [eax + 0x1fc]", "Native health elixir capacity")
    instruction(0x00C04ADC, "cmp", "eax, edi", "Elixir addition clamps to the current tweak capacity")
    pointer(0x0110DB58 + 0x560, 0x00AB4460, "Controller native power selection")
    pointer(0x0110DB58 + 0x524, 0x00AB4850, "Controller native item selection")
    pointer(0x010D0BF8 + 0x188, 0x004D8910, "Concrete engine input gate")
    instruction(0x004D8910, "xor", "eax, eax", "Concrete input gate returns false")
    instruction(0x004D8912, "ret", "", "Concrete input gate has no stack parameters")
    pointer(0x011186A8 + 0x12c, 0x0092DC90, "Base task status returns empty string")
    instruction(0x00AC12F0, "xor", "eax, eax", "Completed task predicate zero-extends result")
    instruction(0x00BBF150, "mov", "ecx, dword ptr [0x143d878]", "Current map-info world root")
    instruction(0x00BBF15C, "mov", "ecx, dword ptr [ecx + 0x2c4]", "Current world-info receiver")
    instruction(0x007816E0, "mov", "eax, dword ptr [ecx + 0x2c0]", "Persistent world-info receiver")
    instruction(0x007816E6, "mov", "eax, dword ptr [eax + 0x410]", "Persistent game-info pointer")
    instruction(0x0078D673, "mov", "ecx, dword ptr [eax + 0x40]", "Streamed map loaded level")
    instruction(0x0078D67B, "mov", "eax, dword ptr [eax + 0x4a8]", "Streamed world-info map definition")
    instruction(0x0078D683, "mov", "eax, dword ptr [esi + 0x4a8]", "Fallback world-info map definition")
    instruction(0x00415200, "mov", "eax, 0x125e660", "Native active localization language buffer")
    instruction(0x00AA11A0, "mov", "eax, dword ptr [ecx + 0xa94]", "Pawn darkness manager")
    instruction(0x00C4802C, "mov", "esi, dword ptr [edi + 0x2c8]", "Darkness threshold count")
    instruction(0x00C48069, "mov", "eax, dword ptr [edi + 0x2c4]", "Darkness threshold storage")
    instruction(0x00C4806F, "mov", "ecx, dword ptr [ebx + 0x38]", "Accumulated darkness score")
    instruction(0x00C48072, "cmp", "ecx, dword ptr [eax + esi*4]", "Compare score against indexed threshold")
    instruction(0x00C48075, "jge", "0xc48083", "Highest qualifying darkness index")
    instruction(0x00A01EF1, "mov", "edx, dword ptr [edi + 0x448]", "Current mission for chaos category")
    instruction(0x00A01EF9, "cmp", "eax, dword ptr [edi + edx*4 + 0x15dc]", "Mission-specific high-chaos threshold")
    instruction(0x00A01F00, "setge", "cl", "High chaos uses inclusive comparison")
    pointer(0x0115CEB0 + 0x178, 0x00BF3020, "Base active-power mana consumption")
    instruction(0x00BF305D, "mov", "edi, dword ptr [eax + 0x8c]", "Consumed power mana definition")
    instruction(0x00BF30AC, "mov", "ecx, dword ptr [esi + 0x80]", "Power mana charged to owning player")
    instruction(0x00BF30B3, "call", "0xaa90a0", "Native fixed mana spend")
    instruction(0x00BBF2D7, "cmp", "esi, dword ptr [0x1451008]", "Base game excludes random free-cast branch")
    pointer(0x011187E0 + 0x12c, 0x00ACBE10, "Custom task evaluated status getter")
    instruction(0x009FAC56, "shl", "eax, 4", "Current chapter target record stride")
    instruction(0x009FAC59, "lea", "eax, [eax + ecx + 0x454]", "Current chapter target storage")
    instruction(0x009FAC60, "mov", "ecx, dword ptr [eax + 4]", "Target exposure requires a nonempty portrait")
    pointer(0x0110DB58 + 0x590, 0x00AA6BF0, "Controller existing profile getter")
    instruction(0x00AA6BF0, "mov", "eax, dword ptr [ecx + 0x49c]", "Controller player root")
    instruction(0x00AA6BF6, "mov", "ecx, dword ptr [eax + 0xa4]", "Player data object")
    instruction(0x00AA6BFC, "mov", "edx, dword ptr [ecx + 0x58]", "Existing profile object")
    instruction(0x008F39B4, "mov", "ecx, dword ptr [edi + 0x44]", "Profile settings storage")
    instruction(0x008F39BA, "cmp", "dword ptr [ecx + eax*8 + 4], ebx", "Settings ID at record+4, stride24")
    instruction(0x008F839A, "cmp", "byte ptr [ebx], 6", "Native blob settings type")
    instruction(0x008F83A0, "mov", "edi, dword ptr [ebx + 4]", "Settings blob byte count")
    instruction(0x008F83DB, "mov", "edx, dword ptr [ebx + 8]", "Settings blob data pointer")
    instruction(0x009382E1, "or", "edx, 1", "Saved mission record validity bit")
    instruction(0x009382E6, "mov", "word ptr [eax + 2], cx", "Saved mission narrative summary bits")
    instruction(0x00BDAE0A, "mov", "ecx, dword ptr [ebx + 0x13c]", "Main-menu mission-stat definitions")
    instruction(0x00BDAE1A, "cmp", "dword ptr [edi + 0x8c], edx", "Saved result matches mission setup")
    instruction(0x00BDAE22, "mov", "eax, dword ptr [edi + 0x90]", "Saved result matches mission number")
    instruction(0x00BCBF75, "test", "al, 1", "Mission statistic checkbox flag")
    instruction(0x00BCBF7B, "and", "al, 2", "Checkbox nonzero predicate flag")
    instruction(0x00BCBF8C, "sete", "al", "Checkbox compares count predicate to definition")
    instruction(0x00BD192D, "mov", "edi, dword ptr [eax + 0xa90]", "Native journal keyring root")
    instruction(0x00BD1936, "add", "edi, 0x38", "Native keyring current owned collection")
    instruction(0x00BD199A, "mov", "eax, dword ptr [eax + 0x14]", "Key's localized FString length")

    instruction(0x0078D376, "call", "0x41fc50", "World map name starts at the outermost package")
    instruction(0x0041FC56, "mov", "ecx, dword ptr [eax + 0x24]", "Outermost package follows UObject Outer")
    instruction(0x0078D385, "mov", "ebx, dword ptr [esi + 0x2c0]", "World map name persistent WorldInfo")
    instruction(0x0078D38D, "cmp", "dword ptr [ebx + 0x318], esi", "World map name streaming level count")
    instruction(0x0078D3DE, "mov", "eax, dword ptr [ebx + 0x314]", "World map name streaming level storage")
    instruction(0x0078D3EF, "call", "0x788910", "Persistent streaming class resolution")
    instruction(0x0078D434, "lea", "ecx, [edi + 0x38]", "Persistent streaming PackageName FName")
    instruction(0x0078D437, "call", "0x454a00", "Persistent streaming name conversion")
    instruction(0x00415200, "mov", "eax, 0x125e660", "Area localization uses the active game language")
    pointer(0x01151FC0 + 0x1dc, 0x00BB1FF0, "HUD tick dispatch for marker investigation")
    instruction(0x00BBD575, "mov", "dword ptr [edi + 0x30], ebx", "Marker last-render flags preserve actual visibility")
    instruction(0x00BCCA2D, "test", "eax, eax", "Native map definitions prefer an existing note tweak override")
    instruction(0x00BCCA3E, "mov", "ecx, dword ptr [0x14627b0]", "Native map definitions fallback class")
    instruction(0x00BCCA46, "call", "0x4967f0", "Native map definitions fallback default-object getter")
    instruction(0x004967F6, "cmp", "dword ptr [esi + 0x13c], 0", "Default-object getter may create a missing object")
    instruction(0x004968C8, "mov", "eax, dword ptr [esi + 0x13c]", "Read-only loaded class-default object field")
    pointer(0x01158588 + 0x14, 0x00BC5830, "Task marker render dispatch")
    instruction(0x00BCE6B9, "mov", "dword ptr [esi], 0x1158588", "Task marker exact vtable")
    instruction(0x00BC586E, "mov", "eax, dword ptr [ebx + 0x1c]", "Marker render sixth stack argument is flags")
    instruction(0x00BC5871, "fld", "dword ptr [ebx + 0x18]", "Marker render fifth stack argument is native distance")
    instruction(0x00BC58A5, "call", "0xbbd4f0", "Task marker delegates base rendering before its distance label")
    instruction(0x00BC5B72, "ret", "0x18", "Task marker render consumes six 32-bit stack arguments")
    instruction(0x00BC5A75, "movss", "xmm0, dword ptr [ebx + 0x18]", "Native label reads current render distance")
    instruction(0x00BC5A7A, "mulss", "xmm0, dword ptr [0xfb7e7c]", "Native label converts world distance to metres")
    pointer(0x00FB7E7C, struct.unpack("<I", struct.pack("<f", 0.01))[0], "Native marker distance multiplier float bits")
    instruction(0x00BCE736, "lea", "eax, [esi + 0x38]", "Task marker localized target label destination")
    instruction(0x00BCE73B, "call", "0x488d90", "Task marker uses game's localization function")
    instruction(0x00BAD534, "test", "byte ptr [ebx + 0x1c0], 0x80", "HUD objective-marker preference")
    instruction(0x00BAD547, "call", "0xad9240", "HUD gets currently tracked task targets")
    instruction(0x00AD9295, "cmp", "byte ptr [edi + 0x45], 0", "Marker selector requires active parent objective")
    instruction(0x00AD92A5, "test", "al, 0x10", "Marker selector excludes hidden parent objectives")
    instruction(0x00AD92B3, "test", "al, 0x20", "Marker selector requires tracked parent objective")
    instruction(0x00AD9310, "cmp", "byte ptr [ebx + 0x5d], 1", "Marker selector excludes completed and failed tasks")
    instruction(0x00AD9320, "test", "al, 0x10", "Marker selector requires tracked task")
    instruction(0x00AD9364, "mov", "edx, dword ptr [ebx + 0x68]", "Live task-target record storage")
    instruction(0x00AD9379, "test", "byte ptr [eax + 0x120], 8", "Marker selector excludes actor pending-kill flag")
    instruction(0x00BAD736, "mov", "cl, byte ptr [eax + 0x10a]", "Task target opt-in actor visibility flags")
    instruction(0x00BAD781, "mov", "eax, dword ptr [edx + 0x120]", "Task target current actor hidden flag")
    instruction(0x00BAD787, "mov", "ecx, dword ptr [ebx + 0x524]", "HUD-owned task marker array")
    instruction(0x00ABF49B, "mov", "dword ptr [esi + 0x4cc], 0x110f620", "Corvo audio speaker interface")
    pointer(0x0110F620 + 0x28, 0x00AA1FF0, "Corvo audio-log playback entry")
    pointer(0x0110F620 + 0x60, 0x00AA2030, "Corvo audio-log hook parameter forwarding")
    pointer(0x010EE420 + 0x28, 0x00CA6E00, "Audio actor native fire-hook entry")
    pointer(0x010EE420 + 0x30, 0x00C9CE30, "Audio actor playing-state query")
    pointer(0x010EE420 + 0x34, 0x00CA4B70, "Audio actor native stop entry")
    pointer(0x010EE420 + 0x60, 0x00C8B5E0, "Audio actor existing hook parameter getter")
    pointer(0x010EE420 + 0x64, 0x00848560, "Audio actor existing conversation component getter")
    instruction(0x00AA2030, "mov", "ecx, dword ptr [ecx + 0x510]", "Pawn speaker receiver forwards to pawn+9dc actor")
    instruction(0x00C8B5F4, "add", "eax, 0x124", "Existing conversation hook parameters")
    instruction(0x00BC1A06, "mov", "dword ptr [eax + 8], ebx", "Native play sets audio-log FName index")
    instruction(0x00BC1A0B, "mov", "dword ptr [eax + 0xc], ecx", "Native play sets audio-log FName number")
    instruction(0x00BC1A16, "push", "0x76", "Native audiograph playback dialog hook")
    instruction(0x00BC1A79, "push", "0xd", "Journal native stop secondary reason")
    instruction(0x00BC1A7B, "push", "9", "Journal native stop primary reason")
    instruction(0x00CA4B87, "ret", "8", "Audio stop wrapper consumes two stack arguments")
    instruction(0x00C9CE53, "mov", "ecx, dword ptr [eax + 0x8c]", "Native playing-state running instance")
    instruction(0x00C9CE5F, "cmp", "dword ptr [ecx + 0xc], edx", "Native playing-state requires current dialog")
    instruction(0x00C9CE86, "movzx", "edx, byte ptr [eax + 0x14]", "Native running-instance playing state")
    instruction(0x00A51572, "mov", "dword ptr [eax + 8], edx", "Transcript getter also mutates shared log name index")
    instruction(0x00A51576, "mov", "dword ptr [eax + 0xc], esi", "Transcript getter also mutates shared log name number")

    pointer(0x010D7B48 + 0x3C0, 0x00A36050, "Concrete note interaction vtable slot")
    instruction(0x00A36078, "mov", "edi, ecx", "Note interaction ECX receiver")
    instruction(0x00A3614F, "mov", "edx, dword ptr [ebp + 8]", "Note interaction's pawn stack argument")
    instruction(0x00A36165, "or", "dword ptr [edi + 0x3c0], eax", "Interaction persists the native acquired flag")
    instruction(0x00A363F8, "mov", "eax, dword ptr [ebp - 0x14]", "Preserve native interaction result")
    instruction(0x00A3640C, "ret", "4", "Note interaction consumes one stack argument")

    objects = json.loads(args.metadata.read_text(encoding="utf-8-sig"))
    task_classes = sorted(obj['name'] for obj in objects if
        obj['path'].startswith("Class'") and obj['name'].startswith('DishonoredTask_') and not obj.get('class'))
    if task_classes != ['DishonoredTask_Base', 'DishonoredTask_Custom']:
        raise AssertionError(f"Unreviewed task status class coverage: {task_classes}")
    checks.append({'meaning': 'Installed task classes covered by verified Base/Custom status virtuals', 'classes': task_classes})
    fields = {obj["path"]: obj for obj in objects}
    for path, kind, dimension in [
        ("IntProperty'DishonoredGame.DishonoredPlayerPawn.m_WhaleBoneCharmSlots'", "int", 10),
        ("ObjectProperty'DishonoredGame.DisTweaks_PlayerPawn.m_pMoneyItem'", "DisAbstractItem", 1),
        ("ObjectProperty'DishonoredGame.DisTweaks_PlayerPawn.m_pRuneItem'", "DisAbstractItem", 1),
        ("ObjectProperty'DishonoredGame.DisTweaks_PlayerPawn.m_pCharmListTweaks'", "DisTweaks_WhaleBoneCharmList", 1),
        ("ObjectProperty'DishonoredGame.DishonoredPawn.m_pAttributes'", "DisAttributes", 1),
        ("IntProperty'DishonoredGame.DisTweaks_ActivePowerBase.m_RuneCosts'", "int", 2),
        ("IntProperty'DishonoredGame.DishonoredInventory.m_ElixirCounts'", "int", 2),
        ("StructProperty'DishonoredGame.DisTweaks_GFxMoviePlayerNote.m_Maps'", "DisUIMapContent", 14),
        ("ObjectProperty'DishonoredGame.DisTweaks_InventoryItem.m_pPlayerSpecificTweaks'", "DisTweaks_InventoryItem_PlayerSpecific", 1),
        ("ObjectProperty'DishonoredGame.DishonoredInventoryItem.m_pOwningInventory'", "DishonoredInventory", 1),
        ("ObjectProperty'DishonoredGame.DishonoredInventoryItem.m_pAttributes'", "DisAttributes", 1),
        ("NameProperty'DishonoredGame.DishonoredGameInfo.m_CurrentChapterTag'", "name", 1),
        ("IntProperty'DishonoredGame.DishonoredGameInfo.m_CurrentMissionNum'", "int", 1),
        ("IntProperty'DishonoredGame.DishonoredGameInfo.m_MissionChaosThreshold'", "int", 21),
        ("ObjectProperty'DishonoredGame.DishonoredPlayerPawn.m_pDarknessManager'", "DisDarknessManager", 1),
        ("IntProperty'DishonoredGame.DisDarknessManager.m_DarknessScore'", "int", 1),
        ("ArrayProperty'DishonoredGame.DisTweaks_PlayerPawn.m_DarknessLevels'", "array<int>", 1),
        ("StrProperty'DishonoredGame.DishonoredTask_Custom.m_ActiveStatus'", "string", 1),
        ("StructProperty'DishonoredGame.DishonoredGameInfo.m_CurrentChapterTargets'", "DisChapterTarget", 2),
        ("ObjectProperty'DishonoredGame.DisTweaks_ChapterInfo.m_ChapterTargets'", "DisTweaks_ChapterTarget", 2),
        ("ObjectProperty'DishonoredGame.DishonoredPlayerPawn.m_pKeyRing'", "DisKeyRing", 1),
        ("ArrayProperty'DishonoredGame.DisKeyRing.m_Keys'", "array<DisKeyInfo>", 1),
        ("StrProperty'DishonoredGame.DishonoredGameInfo.m_MissionNames'", "string", 21),
        ("ArrayProperty'DishonoredGame.DisTweaks_GFxMoviePlayerMainMenu.m_MissionStatsTweaks'", "array<DisTweaks_MissionStats>", 1),
        ("ObjectProperty'DishonoredGame.DisGlobalUIManager.m_pHUD'", "DisGFxMoviePlayerHUD", 1),
        ("ArrayProperty'DishonoredGame.DisGFxMoviePlayerHUD.m_TaskMarkers'", "array<Pointer>", 1),
        ("ArrayProperty'DishonoredGame.DishonoredTask_Base.m_Targets'", "array<DisTaskTarget>", 1),
        ("NameProperty'DishonoredGame.DisAbstractItemAudioLog.m_AudioLogName'", "name", 1),
        ("StructProperty'DishonoredGame.DisConversationComponent.m_pDialogRunningInst'", "Pointer", 1),
        ("StructProperty'DishonoredGame.DisConversationComponent.m_HookParams'", "DisHookParameters", 1),
    ]:
        field = fields[path]
        if (field["type"], field["arrayDim"]) != (kind, dimension):
            raise AssertionError(f"Cooked field contract changed: {path}")
        checks.append({"meaning": "Cooked field type/dimension", "field": path, "type": kind, "arrayDim": dimension})
    report = {"executableSha256": digest, "scope": "Static native/schema evidence only; no build or gameplay execution", "checks": checks}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Verified {len(checks)} native/schema evidence checks. No mod build or runtime validation performed.")


if __name__ == "__main__":
    main()
