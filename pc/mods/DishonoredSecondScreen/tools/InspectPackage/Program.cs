using System.Text.Json;
using UELib;
using UELib.Core;

if (args.Length < 2) {
    Console.Error.WriteLine("Usage: InspectPackage <decompressed.upk> <output-directory> [owner filters...]");
    return 2;
}
Directory.CreateDirectory(args[1]);
using var diagnostics = new StreamWriter(Path.Combine(args[1], "reader.log"));
var originalOut = Console.Out;
Console.SetOut(diagnostics);
Console.SetError(diagnostics);
var package = UnrealLoader.LoadPackage(args[0], FileAccess.Read);
package.InitializePackage(UnrealPackage.InitFlags.RegisterClasses | UnrealPackage.InitFlags.Construct);
var filters = args.Skip(2).ToArray();
var objects = package.Objects.Where(o => (int)o > 0).ToArray();
var errors = new List<object>();
// Load property and enum references before formatting function signatures.
// Never deserialize class-default objects or arbitrary game assets here.
foreach (var obj in objects.Where(o => o is UProperty or UEnum)) {
    try { obj.Load(); }
    catch (Exception e) { errors.Add(new { name = obj.GetReferencePath(), error = e.Message }); }
}
var selected = objects.Where(o => filters.Length == 0 || filters.Any(f =>
    o.GetReferencePath().Contains(f, StringComparison.OrdinalIgnoreCase))).ToArray();
var rows = new List<object>();
foreach (var obj in selected) {
    var row = new Dictionary<string, object?> {
        ["index"] = (int)obj, ["name"] = obj.Name.ToString(),
        ["path"] = obj.GetReferencePath(), ["class"] = obj.Class?.Name.ToString(),
        ["owner"] = obj.Outer?.GetReferencePath(),
        ["serialOffset"] = obj.ExportTable?.SerialOffset,
        ["serialSize"] = obj.ExportTable?.SerialSize,
    };
    try {
        if (obj is UProperty property) {
            row["type"] = property.GetFriendlyType();
            row["flags"] = ((ulong)property.PropertyFlags).ToString("X16");
            row["arrayDim"] = property.ArrayDim;
            // Serialized element size is metadata, never a runtime field offset.
            row["serializedElementSize"] = property.ElementSize;
            row["nextIndex"] = property.NextField == null ? 0 : (int)property.NextField;
        } else if (obj is UFunction function) {
            function.Load(); function.PostInitialize();
            row["flags"] = ((ulong)function.FunctionFlags).ToString("X8");
            row["nativeToken"] = function.NativeToken;
            row["scriptSize"] = function.ScriptSize;
            row["parameters"] = objects.OfType<UProperty>().Where(p => p.Outer == obj &&
                ((ulong)p.PropertyFlags & 0x80) != 0).Select(p => new {
                    name = p.Name.ToString(), type = p.GetFriendlyType(),
                    flags = ((ulong)p.PropertyFlags).ToString("X16"), index = (int)p,
                    nextIndex = p.NextField == null ? 0 : (int)p.NextField,
                }).ToArray();
            var name = $"{(int)obj:D6}-{obj.Outer?.Name}-{obj.Name}.uc";
            File.WriteAllText(Path.Combine(args[1], name), function.Decompile());
        } else if (obj is UEnum enumeration) {
            row["declaration"] = enumeration.Decompile();
        }
    } catch (Exception e) {
        row["error"] = e.Message;
        errors.Add(new { name = obj.GetReferencePath(), error = e.Message });
    }
    rows.Add(row);
}
var options = new JsonSerializerOptions { WriteIndented = true };
File.WriteAllText(Path.Combine(args[1], "objects.json"), JsonSerializer.Serialize(rows, options));
File.WriteAllText(Path.Combine(args[1], "errors.json"), JsonSerializer.Serialize(errors, options));
Console.SetOut(originalOut);
Console.WriteLine($"Inspected {rows.Count} exports; {errors.Count} reader errors retained in {args[1]}");
return errors.Count == 0 ? 0 : 1;
