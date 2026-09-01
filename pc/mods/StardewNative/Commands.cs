using System;
using System.Collections.Concurrent;
using System.Text.Json;

namespace StardewSecondScreen
{
    internal readonly struct Command
    {
        public readonly string Type;
        public readonly int A;
        public readonly int B;
        public readonly string Key;

        public Command(string type, int a, int b, string key = "")
        {
            Type = type;
            A = a;
            B = b;
            Key = key;
        }
    }

    internal sealed class CommandQueue
    {
        private const int Limit = 64;
        private readonly ConcurrentQueue<Command> _pending = new();

        public void Accept(string raw)
        {
            if (_pending.Count >= Limit) return;
            try
            {
                using var document = JsonDocument.Parse(raw);
                var root = document.RootElement;
                if (root.ValueKind != JsonValueKind.Object) return;
                var type = Field(root, "type");
                if (string.IsNullOrEmpty(type)) return;
                _pending.Enqueue(new Command(
                    type,
                    Number(root, "slot") ?? Number(root, "from") ??
                        Number(root, "value") ?? -1,
                    Number(root, "to") ?? -1,
                    Field(root, "key") ?? ""));
            }
            catch (JsonException)
            {
                // Invalid input is ignored without reaching the game thread.
            }
        }

        public void Drain(Action<Command> apply, Action<Command, Exception>? onFailure = null)
        {
            while (_pending.TryDequeue(out var command))
            {
                try { apply(command); }
                catch (Exception failure) { onFailure?.Invoke(command, failure); }
            }
        }

        public void Clear()
        {
            while (_pending.TryDequeue(out _)) { }
        }

        private static string? Field(JsonElement root, string key)
        {
            if (!root.TryGetProperty(key, out var value) ||
                value.ValueKind != JsonValueKind.String) return null;
            return value.GetString();
        }

        private static int? Number(JsonElement root, string key)
        {
            if (!root.TryGetProperty(key, out var value)) return null;
            if (value.ValueKind == JsonValueKind.Number && value.TryGetInt32(out var number))
                return number;
            return value.ValueKind == JsonValueKind.String &&
                int.TryParse(value.GetString(), out number)
                    ? number
                    : null;
        }
    }
}
