// Bounded decompilation for the locally extracted v393216 memory image.
// @category DualScreen
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.nio.file.Files;
import java.nio.file.Path;

public class PlzaTrace extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("output-dir function-rva...");
        // Verified through MOD0/DT_JMPREL/DT_SYMTAB: PLT 3160640 -> GOT 3EE8020 -> abort.
        // Marking the real import prevents fatal paths merging unrelated following functions.
        var abortAddress = toAddr(0x3160640L);
        disassemble(abortAddress);
        Function abort = getFunctionAt(abortAddress);
        if (abort == null) abort = createFunction(abortAddress, "plza_abort");
        if (abort == null) throw new IllegalStateException("abort import not found");
        abort.setNoReturn(true);
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
                if (f == null) f = createFunction(address, "plza_" + address);
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
