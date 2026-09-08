// Audit the observed startup wrapper only. Not a gameplay hook contract.
// @category Crysis3Research
import java.io.PrintWriter;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class CrysisBootstrapAudit extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("Output path required");
        DecompInterface decompiler = new DecompInterface();
        try (PrintWriter out = new PrintWriter(args[0])) {
            decompiler.openProgram(currentProgram);
            // These call targets were disassembled from this exact file's PE entrypoint.
            long[] rvas = {0x52a2291L, 0x52a243cL, 0x52a24acL};
            for (long rva : rvas) {
                Address address = currentProgram.getImageBase().add(rva);
                disassemble(address);
                Function function = getFunctionAt(address);
                if (function == null) function = createFunction(address, "ObservedBootstrap_" + Long.toHexString(rva));
                out.println("OBSERVED FILE ENTRY WRAPPER " + address);
                if (function == null) { out.println("No function established"); continue; }
                var result = decompiler.decompileFunction(function, 30, monitor);
                out.println(result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage());
            }
        } finally { decompiler.dispose(); }
    }
}
