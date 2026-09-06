// Exact-build interoperability research. Output belongs in ignored .work/ac2.
// @category DualScreen.AC2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;
import java.util.LinkedHashSet;

public class Ac2Trace extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("output file, followed by hex addresses");
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            out.println("Program: " + currentProgram.getName());
            out.println("Executable SHA256: " + currentProgram.getExecutableSHA256());
            LinkedHashSet<Function> functions = new LinkedHashSet<>();
            for (int i = 1; i < args.length; i++) {
                Address address = toAddr(args[i].replace("0x", ""));
                out.println("\nADDRESS " + address);
                Function direct = getFunctionContaining(address);
                if (direct == null && currentProgram.getMemory().getBlock(address) != null &&
                    currentProgram.getMemory().getBlock(address).isExecute()) {
                    disassemble(address);
                    direct = createFunction(address, null);
                }
                if (direct != null) functions.add(direct);
                for (Reference reference : getReferencesTo(address)) {
                    Function caller = getFunctionContaining(reference.getFromAddress());
                    out.println("REFERENCE " + reference + " FUNCTION " + caller);
                    if (caller != null) functions.add(caller);
                }
            }
            for (Function function : functions) {
                monitor.checkCancelled();
                out.println("\nFUNCTION " + function.getEntryPoint() + " " + function.getName());
                var result = decompiler.decompileFunction(function, 45, monitor);
                if (result.decompileCompleted() && result.getDecompiledFunction() != null)
                    out.println(result.getDecompiledFunction().getC());
                else out.println("DECOMPILATION FAILED " + result.getErrorMessage());
                out.flush();
            }
        } finally { decompiler.dispose(); }
    }
}
