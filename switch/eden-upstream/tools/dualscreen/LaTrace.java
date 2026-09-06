// Targeted, bounded decompilation of the locally extracted LA executable.
// Import its decompressed memory image at zero; use -noanalysis -max-cpu 1.
// Arguments: output directory, then independently verified function entry RVAs.
// @category DualScreen
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.nio.file.Files;
import java.nio.file.Path;

public class LaTrace extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("output-dir function-rva...");
        Path output = Path.of(args[0]);
        Files.createDirectories(output);
        DecompInterface dc = new DecompInterface();
        try {
            dc.openProgram(currentProgram);
            for (int i = 1; i < args.length; ++i) {
                monitor.checkCancelled();
                var address = toAddr(Long.decode(args[i]));
                disassemble(address);
                Function f = getFunctionAt(address);
                if (f == null) f = createFunction(address, "la_" + address);
                if (f == null) throw new IllegalStateException("No function at " + address);
                var result = dc.decompileFunction(f, 45, monitor);
                if (!result.decompileCompleted()) {
                    println("FAILED " + address + " " + result.getErrorMessage());
                    continue;
                }
                Files.writeString(output.resolve(address + ".c"), result.getDecompiledFunction().getC());
                println("DECOMPILED " + address);
            }
        } finally {
            dc.dispose();
        }
    }
}
