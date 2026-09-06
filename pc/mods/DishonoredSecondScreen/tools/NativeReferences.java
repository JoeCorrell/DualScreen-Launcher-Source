// @category DualScreen.Dishonored
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;

public class NativeReferences extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            for (int i = 1; i < args.length; i++) {
                Address target = toAddr(Long.decode(args[i]));
                out.println("TARGET " + target);
                for (Reference ref : getReferencesTo(target)) {
                    out.println(ref.getReferenceType() + " " + ref.getFromAddress() +
                        " " + getFunctionContaining(ref.getFromAddress()) + " " +
                        getInstructionAt(ref.getFromAddress()));
                }
            }
        }
    }
}
