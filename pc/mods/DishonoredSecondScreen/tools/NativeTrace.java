// @category DualScreen.Dishonored
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;

/** Read-only analysis output; no source/game binary patches. Arguments: output, absolute VAs. */
public class NativeTrace extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("output file and addresses required");
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            out.println("IMAGE " + currentProgram.getName() + " BASE " + currentProgram.getImageBase());
            for (int i = 1; i < args.length; i++) {
                monitor.checkCancelled();
                Address address = toAddr(Long.decode(args[i]));
                Function function = getFunctionAt(address);
                if (function == null) function = getFunctionContaining(address);
                if (function == null) {
                    disassemble(address);
                    function = createFunction(address, null);
                }
                out.println("\nFUNCTION " + address + " " + function);
                for (Reference ref : getReferencesTo(address))
                    out.println("REFERENCE " + ref.getFromAddress() + " " + getFunctionContaining(ref.getFromAddress()));
                if (function == null) { out.println("UNRESOLVED"); continue; }
                DecompileResults result = decompiler.decompileFunction(function, 40, monitor);
                if (result.decompileCompleted()) out.println(result.getDecompiledFunction().getC());
                else out.println("DECOMPILE ERROR " + result.getErrorMessage());
                out.flush();
            }
        } finally { decompiler.dispose(); }
    }
}
