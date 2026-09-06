import com.dualscreen.companion.api.SignedModuleArchive;
import java.io.File;
import java.util.Set;

class VerifyArchive {
    public static void main(String[] args) throws Exception {
        File out = new File(args[1]);
        if (!out.mkdirs()) throw new AssertionError("Fresh test directory required");
        boolean accepted = false;
        try {
            var files = SignedModuleArchive.extract(new File(args[0]), out, Set.of(args[2]));
            if (files.size() < 3) throw new AssertionError("Incomplete accepted archive");
            accepted = true;
        } catch (SecurityException expected) { System.out.println("Rejected: " + expected.getMessage()); }
        if (accepted != Boolean.parseBoolean(args[3])) throw new AssertionError("Unexpected verification result");
        System.out.println("PASS " + (accepted ? "authenticated archive" : "rejected archive"));
    }
}
