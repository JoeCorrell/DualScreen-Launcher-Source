// Map integrity-checked live evidence and decompile explicitly observed targets.
// @category Crysis3Research
import java.io.FileInputStream;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.HexFormat;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;

public class CrysisLiveAudit extends GhidraScript {
    private void check(Path path, String hash) throws Exception {
        String actual = HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(path)));
        if (!actual.equals(hash)) throw new IllegalArgumentException("Evidence hash mismatch: " + path);
    }
    private void block(Path file, String name, long address, boolean execute) throws Exception {
        if (currentProgram.getMemory().getBlock(toAddr(address)) != null) return;
        try (FileInputStream input = new FileInputStream(file.toFile())) {
            MemoryBlock block = currentProgram.getMemory().createInitializedBlock(
                name, toAddr(address), input, Files.size(file), monitor, false);
            block.setRead(true); block.setWrite(false); block.setExecute(execute);
        }
    }
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) throw new IllegalArgumentException("Capture directory, output, observed targets required");
        Path root = Path.of(args[0]);
        check(root.resolve("loaded-text.bin"), "e2cb3e908f599a2bb90c338a04ef5bec2a89b8a563e9f5f985374b153544989f");
        check(root.resolve("loaded-rdata.bin"), "376e0b44ad449a52aa98a5ac8fde81cef9819306be0cf41ef3b6074e3f3d17fe");
        check(root.resolve("loaded-fault-page.bin"), "6d977c4e29755c57cbad9a123e071d0bf8ef5ecac6cde397db1875f2c7215846");
        check(root.resolve("loaded-headers.bin"), "fb530657a66ea51a6a7495c36085e0c856d6daccffbacc1db8c037d48c55e25d");
        MemoryBlock code = currentProgram.getMemory().getBlock(toAddr(0x37001000L));
        if (code == null || code.getStart().getOffset() != 0x37001000L || code.getSize() != 42457088L)
            throw new IllegalArgumentException("Import raw loaded-text.bin at 37001000");
        code.setRead(true); code.setWrite(false); code.setExecute(true);
        block(root.resolve("loaded-rdata.bin"), "live_rdata", 0x3987f000L, false);
        block(root.resolve("loaded-headers.bin"), "live_headers", 0x37000000L, false);
        block(root.resolve("loaded-fault-page.bin"), "observed_fault_page", 0x39c16000L, true);
        DecompInterface decompiler = new DecompInterface();
        try (PrintWriter out = new PrintWriter(args[1])) {
            decompiler.openProgram(currentProgram);
            out.println("LIVE SNAPSHOT RESEARCH. Types/names are inferred; no hook ABI or ownership verified.");
            for (int i = 2; i < args.length; ++i) {
                Address address = toAddr(Long.decode(args[i]));
                disassemble(address);
                Function function = getFunctionAt(address);
                if (function == null) function = createFunction(address, "Observed_" + address);
                out.println("\nOBSERVED TARGET " + address);
                if (function == null) { out.println("No function established"); continue; }
                var result = decompiler.decompileFunction(function, 25, monitor);
                out.println(result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage());
                out.flush();
            }
        } finally { decompiler.dispose(); }
    }
}
