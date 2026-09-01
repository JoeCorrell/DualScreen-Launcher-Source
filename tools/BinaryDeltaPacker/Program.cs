using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;

internal static class Program
{
    private const int BlockSize = 32;
    private const int SourceStride = 4;
    private const int MinimumMatch = 64;
    private const int MaximumCandidatesPerHash = 48;
    private static readonly byte[] Magic = Encoding.ASCII.GetBytes("DSDP1\0\0\0");

    private static int Main(string[] args)
    {
        bool apply = args.Length == 4 &&
                     args[0].Equals("apply", StringComparison.OrdinalIgnoreCase);
        if (args.Length != 3 && !apply)
        {
            Console.Error.WriteLine(
                "Usage:\n" +
                "  BinaryDeltaPacker <original> <patched> <output.dsdiff.gz>\n" +
                "  BinaryDeltaPacker apply <source> <patch.dsdiff.gz> <output>");
            return 2;
        }

        try
        {
            if (apply)
            {
                ApplyPatch(args[1], args[2], args[3]);
                Console.WriteLine($"Applied {Path.GetFileName(args[2])} to {Path.GetFileName(args[3])}.");
                return 0;
            }

            byte[] original = File.ReadAllBytes(args[0]);
            byte[] patched = File.ReadAllBytes(args[1]);
            string output = Path.GetFullPath(args[2]);
            Directory.CreateDirectory(Path.GetDirectoryName(output)!);
            WritePatch(original, patched, output);
            VerifyPatch(original, patched, output);
            Console.WriteLine(
                $"Created {Path.GetFileName(output)}: {new FileInfo(output).Length:N0} bytes " +
                $"for {patched.Length:N0} output bytes.");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }

    private static void ApplyPatch(string sourcePath, string patchPath, string outputPath)
    {
        byte[] source = File.ReadAllBytes(sourcePath);
        string output = Path.GetFullPath(outputPath);
        Directory.CreateDirectory(Path.GetDirectoryName(output)!);

        using FileStream file = File.OpenRead(patchPath);
        using GZipStream gzip = new(file, CompressionMode.Decompress);
        using BinaryReader reader = new(gzip, Encoding.UTF8);
        if (!reader.ReadBytes(Magic.Length).SequenceEqual(Magic))
            throw new InvalidDataException("Bad magic");
        if (reader.ReadInt64() != source.Length)
            throw new InvalidDataException("Patch input length does not match source");
        if (!reader.ReadBytes(32).SequenceEqual(SHA256.HashData(source)))
            throw new InvalidDataException("Patch input hash does not match source");

        long outputLength = reader.ReadInt64();
        if (outputLength < 0 || outputLength > int.MaxValue)
            throw new InvalidDataException("Unsupported patch output length");
        byte[] outputHash = reader.ReadBytes(32);
        using MemoryStream reconstructed = new(checked((int)outputLength));
        while (true)
        {
            byte opcode = reader.ReadByte();
            if (opcode == byte.MaxValue) break;
            if (opcode == 0)
            {
                long offset = reader.ReadInt64();
                int length = reader.ReadInt32();
                if (offset < 0 || length < 0 || offset + length > source.Length)
                    throw new InvalidDataException("Patch copy range is outside the source");
                reconstructed.Write(source, checked((int)offset), length);
            }
            else if (opcode == 1)
            {
                int length = reader.ReadInt32();
                if (length < 0) throw new InvalidDataException("Negative patch literal length");
                byte[] literal = reader.ReadBytes(length);
                if (literal.Length != length) throw new EndOfStreamException("Truncated patch literal");
                reconstructed.Write(literal);
            }
            else
            {
                throw new InvalidDataException($"Unknown opcode {opcode}");
            }

            if (reconstructed.Length > outputLength)
                throw new InvalidDataException("Patch produced more data than declared");
        }

        byte[] result = reconstructed.ToArray();
        if (result.Length != outputLength ||
            !SHA256.HashData(result).SequenceEqual(outputHash))
        {
            throw new InvalidDataException("Patch output verification failed");
        }

        string temporary = output + ".partial";
        File.Delete(temporary);
        try
        {
            File.WriteAllBytes(temporary, result);
            File.Move(temporary, output, overwrite: true);
        }
        finally
        {
            File.Delete(temporary);
        }
    }

    private static void WritePatch(byte[] original, byte[] patched, string output)
    {
        Dictionary<ulong, List<int>> index = BuildIndex(original);
        string temporary = output + ".partial";
        File.Delete(temporary);
        try
        {
            using (FileStream file = new(
                       temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            {
                using (GZipStream gzip = new(
                           file, CompressionLevel.SmallestSize, leaveOpen: true))
                using (BinaryWriter writer = new(gzip, Encoding.UTF8, leaveOpen: false))
                {
                    writer.Write(Magic);
                    writer.Write((long)original.Length);
                    writer.Write(SHA256.HashData(original));
                    writer.Write((long)patched.Length);
                    writer.Write(SHA256.HashData(patched));

                    int position = 0;
                    int literalStart = 0;
                    while (position <= patched.Length - BlockSize)
                    {
                        Match match = FindBestMatch(
                            original, patched, index, position, literalStart);
                        if (match.Length < MinimumMatch)
                        {
                            position++;
                            continue;
                        }

                        WriteLiteral(
                            writer, patched, literalStart, match.TargetStart - literalStart);
                        writer.Write((byte)0);
                        writer.Write((long)match.SourceStart);
                        writer.Write(match.Length);
                        position = match.TargetStart + match.Length;
                        literalStart = position;
                    }

                    WriteLiteral(writer, patched, literalStart, patched.Length - literalStart);
                    writer.Write(byte.MaxValue);
                }
                file.Flush(flushToDisk: true);
            }
            File.Move(temporary, output, overwrite: true);
        }
        finally
        {
            File.Delete(temporary);
        }
    }

    private static Dictionary<ulong, List<int>> BuildIndex(byte[] source)
    {
        Dictionary<ulong, List<int>> index = new(source.Length / SourceStride);
        for (int offset = 0; offset <= source.Length - BlockSize; offset += SourceStride)
        {
            ulong hash = HashBlock(source.AsSpan(offset, BlockSize));
            if (!index.TryGetValue(hash, out List<int>? candidates))
            {
                candidates = [];
                index.Add(hash, candidates);
            }
            if (candidates.Count < MaximumCandidatesPerHash) candidates.Add(offset);
        }
        return index;
    }

    private static Match FindBestMatch(
        byte[] source,
        byte[] target,
        Dictionary<ulong, List<int>> index,
        int targetOffset,
        int literalStart)
    {
        ulong hash = HashBlock(target.AsSpan(targetOffset, BlockSize));
        if (!index.TryGetValue(hash, out List<int>? candidates)) return default;

        Match best = default;
        foreach (int candidate in candidates)
        {
            if (!target.AsSpan(targetOffset, BlockSize)
                    .SequenceEqual(source.AsSpan(candidate, BlockSize))) continue;

            int sourceStart = candidate;
            int targetStart = targetOffset;
            while (sourceStart > 0 && targetStart > literalStart &&
                   source[sourceStart - 1] == target[targetStart - 1])
            {
                sourceStart--;
                targetStart--;
            }

            int sourceEnd = candidate + BlockSize;
            int targetEnd = targetOffset + BlockSize;
            while (sourceEnd < source.Length && targetEnd < target.Length &&
                   source[sourceEnd] == target[targetEnd])
            {
                sourceEnd++;
                targetEnd++;
            }

            Match current = new(sourceStart, targetStart, targetEnd - targetStart);
            if (current.Length > best.Length) best = current;
        }
        return best;
    }

    private static ulong HashBlock(ReadOnlySpan<byte> bytes)
    {
        const ulong offsetBasis = 14695981039346656037UL;
        const ulong prime = 1099511628211UL;
        ulong hash = offsetBasis;
        foreach (byte value in bytes)
        {
            hash ^= value;
            hash *= prime;
        }
        return hash;
    }

    private static void WriteLiteral(BinaryWriter writer, byte[] source, int offset, int length)
    {
        if (length <= 0) return;
        writer.Write((byte)1);
        writer.Write(length);
        writer.Write(source, offset, length);
    }

    private static void VerifyPatch(byte[] original, byte[] expected, string path)
    {
        using FileStream file = File.OpenRead(path);
        using GZipStream gzip = new(file, CompressionMode.Decompress);
        using BinaryReader reader = new(gzip, Encoding.UTF8);
        if (!reader.ReadBytes(Magic.Length).SequenceEqual(Magic)) throw new InvalidDataException("Bad magic");
        if (reader.ReadInt64() != original.Length) throw new InvalidDataException("Bad input length");
        if (!reader.ReadBytes(32).SequenceEqual(SHA256.HashData(original))) {
            throw new InvalidDataException("Bad input hash");
        }
        if (reader.ReadInt64() != expected.Length) throw new InvalidDataException("Bad output length");
        byte[] outputHash = reader.ReadBytes(32);
        using MemoryStream output = new(expected.Length);
        while (true)
        {
            byte opcode = reader.ReadByte();
            if (opcode == byte.MaxValue) break;
            if (opcode == 0)
            {
                long offset = reader.ReadInt64();
                int length = reader.ReadInt32();
                output.Write(original, checked((int)offset), length);
            }
            else if (opcode == 1)
            {
                int length = reader.ReadInt32();
                output.Write(reader.ReadBytes(length));
            }
            else
            {
                throw new InvalidDataException($"Unknown opcode {opcode}");
            }
        }
        byte[] observed = output.ToArray();
        if (!observed.AsSpan().SequenceEqual(expected) ||
            !SHA256.HashData(observed).SequenceEqual(outputHash))
        {
            throw new InvalidDataException("Generated patch did not reproduce the target");
        }
    }

    private readonly record struct Match(int SourceStart, int TargetStart, int Length);
}
