// Targeted retained-EXE analysis, not a whole-program auto-analysis pass.
// @category DualScreen
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

public class DumpNativeRegion extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 3) throw new IllegalArgumentException("startVA endVA outputPath");
        long start = Long.decode(args[0]), end = Long.decode(args[1]);
        if (end <= start || end - start > 0x10000) throw new IllegalArgumentException("region exceeds 64 KiB");
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try (PrintWriter output = new PrintWriter(Files.newBufferedWriter(Path.of(args[2]), StandardCharsets.UTF_8))) {
            output.println("// Targeted native region: " + args[0] + " .. " + args[1]);
            output.println("// INT3-padding candidate boundaries, not symbol-verified method names.");
            boolean padding = true;
            int candidates = 0;
            for (long cursor = start; cursor < end && !monitor.isCancelled(); ++cursor) {
                Address address = toAddr(cursor);
                int octet = currentProgram.getMemory().getByte(address) & 255;
                if (octet == 0xcc) { padding = true; continue; }
                if (!padding) continue;
                padding = false;
                Function function = getFunctionContaining(address);
                if (function == null) {
                    disassemble(address);
                    function = createFunction(address, null);
                }
                if (function == null || !function.getEntryPoint().equals(address)) continue;
                ++candidates;
                output.println("// CANDIDATE " + function.getEntryPoint());
                DecompileResults result = decompiler.decompileFunction(function, 10, monitor);
                if (result.decompileCompleted()) output.println(result.getDecompiledFunction().getC());
                else output.println("// DECOMPILE FAILED: " + result.getErrorMessage());
                output.flush();
            }
            output.println("// Candidate functions: " + candidates);
            println("Targeted region completed: " + candidates + " candidates -> " + args[2]);
        } finally { decompiler.dispose(); }
    }
}
