import java.io.*;
import java.nio.file.*;
import java.security.*;
import java.util.*;
import java.util.zip.ZipFile;
import jdk.security.jarsigner.JarSigner;

/** Uses existing publisher key without passing passwords on a command line or printing them. */
class SignPackage {
    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        Properties properties = new Properties();
        try (InputStream input = Files.newInputStream(root.resolve("keystore.properties"))) { properties.load(input); }
        char[] storePassword = properties.getProperty("storePassword").toCharArray();
        char[] keyPassword = properties.getProperty("keyPassword").toCharArray();
        KeyStore store = KeyStore.getInstance(root.resolve(properties.getProperty("storeFile")).toFile(), storePassword);
        KeyStore.PrivateKeyEntry entry = (KeyStore.PrivateKeyEntry) store.getEntry(properties.getProperty("keyAlias"), new KeyStore.PasswordProtection(keyPassword));
        JarSigner signer = new JarSigner.Builder(entry).digestAlgorithm("SHA-256").signerName("DSMOD").build();
        try (ZipFile input = new ZipFile(args[1]); OutputStream output = Files.newOutputStream(Path.of(args[2]))) { signer.sign(input, output); }
        Arrays.fill(storePassword, '\0'); Arrays.fill(keyPassword, '\0');
        System.out.println("Signed external module package with launcher publisher certificate.");
    }
}
