// Bounded tracing of functions selected from the exact local Odyssey NSO symbols.
// Arguments: output directory, then RVA:size:mangled-name entries from NSO dynsym.
// @category DualScreen
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressSet;
import java.nio.file.Files;
import java.nio.file.Path;

public class SmoTrace extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2 || args.length > 65)
            throw new IllegalArgumentException("output-dir rva:size:name... (max 64 functions)");
        Path output = Path.of(args[0]);
        Files.createDirectories(output);
        // Name every selected function before decompiling callers.
        for (int i = 1; i < args.length; ++i) {
            String[] entry = args[i].split(":", 3);
            if (entry.length != 3) throw new IllegalArgumentException("Expected RVA:size:name");
            var address = toAddr(Long.decode(entry[0]));
            long size = Long.decode(entry[1]);
            if (size < 4 || size > 0x100000 || size % 4 != 0)
                throw new IllegalArgumentException("Invalid ARM64 symbol size");
            disassemble(address);
            if (getFunctionAt(address) == null) createFunction(address, entry[2]);
            var function = getFunctionAt(address);
            if (function == null) throw new IllegalStateException("No function: " + entry[2]);
            // Ghidra's unrestricted body finder can follow a tail branch into an
            // unlabelled callee. The NSO's exact function extent prevents that.
            function.setBody(new AddressSet(address, address.add(size - 1)));
        }
        DecompInterface dc = new DecompInterface();
        try {
            dc.openProgram(currentProgram);
            for (int i = 1; i < args.length; ++i) {
                monitor.checkCancelled();
                String[] entry = args[i].split(":", 3);
                var address = toAddr(Long.decode(entry[0]));
                var function = getFunctionAt(address);
                if (function == null) throw new IllegalStateException("No function: " + entry[2]);
                var result = dc.decompileFunction(function, 30, monitor);
                if (!result.decompileCompleted()) {
                    println("FAILED " + entry[2] + " " + result.getErrorMessage());
                    continue;
                }
                Files.writeString(output.resolve(address + ".c"),
                    "// NSO symbol: " + entry[2] + "; size=" + entry[1] + "\n" + result.getDecompiledFunction().getC());
                println("DECOMPILED " + address + " " + entry[2]);
            }
        } finally {
            dc.dispose();
        }
    }
}
