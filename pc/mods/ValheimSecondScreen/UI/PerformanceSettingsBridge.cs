using System;
using System.Collections.Generic;
using System.Reflection;
using BepInEx.Configuration;

namespace ValheimSecondScreen.UI
{
    /// <summary>
    /// Optional, reflection-only bridge to ValheimPerformance. Keeping the
    /// assembly name out of this project's references means the lower-screen
    /// mod still loads when the performance mod is not installed.
    /// </summary>
    internal static class PerformanceSettingsBridge
    {
        private const string AssemblyName = "ValheimPerformance";
        private const string PluginTypeName = "ValheimPerformance.Plugin";

        private static Type? _pluginType;

        public static bool Available => Entry("QueuedFrames") != null;

        public static string Version
        {
            get
            {
                Type? type = ResolvePluginType();
                if (type == null) return "NOT LOADED";
                object? declared = type.GetField("PluginVersion",
                    BindingFlags.Public | BindingFlags.Static)?.GetRawConstantValue();
                if (declared is string version && !string.IsNullOrEmpty(version))
                    return version;
                return type.Assembly.GetName().Version?.ToString(3) ?? "UNKNOWN";
            }
        }

        public static T Get<T>(string fieldName, T fallback)
        {
            ConfigEntryBase? entry = Entry(fieldName);
            if (entry == null) return fallback;
            try
            {
                object value = entry.BoxedValue;
                return value is T typed ? typed : fallback;
            }
            catch
            {
                return fallback;
            }
        }

        public static void Set<T>(string fieldName, T value)
        {
            ConfigEntryBase? entry = Entry(fieldName);
            if (entry == null) return;
            try
            {
                entry.BoxedValue = value!;
            }
            catch (Exception error)
            {
                Plugin.Log?.LogWarning("[SecondScreen] Could not save performance setting "
                    + fieldName + ": " + error.Message);
            }
            ApplyNow();
        }

        public static void Reset(params string[] fieldNames)
        {
            var entries = new List<ConfigEntryBase>();
            foreach (string fieldName in fieldNames)
            {
                ConfigEntryBase? entry = Entry(fieldName);
                if (entry != null) entries.Add(entry);
            }
            ResetEntries(entries);
        }

        public static void ResetAll()
        {
            Type? type = ResolvePluginType();
            if (type == null) return;
            var entries = new List<ConfigEntryBase>();
            foreach (FieldInfo field in type.GetFields(
                         BindingFlags.Public | BindingFlags.Static))
            {
                if (field.GetValue(null) is ConfigEntryBase entry)
                    entries.Add(entry);
            }
            ResetEntries(entries);
        }

        private static void ResetEntries(List<ConfigEntryBase> entries)
        {
            if (entries.Count == 0) return;

            ConfigFile file = entries[0].ConfigFile;
            bool saveOnSet = file.SaveOnConfigSet;
            try
            {
                file.SaveOnConfigSet = false;
                foreach (ConfigEntryBase entry in entries)
                    entry.BoxedValue = entry.DefaultValue;
                if (saveOnSet) file.Save();
            }
            catch (Exception error)
            {
                Plugin.Log?.LogWarning("[SecondScreen] Could not reset performance settings: "
                    + error.Message);
            }
            finally
            {
                file.SaveOnConfigSet = saveOnSet;
            }
            ApplyNow();
        }

        private static ConfigEntryBase? Entry(string fieldName)
        {
            Type? type = ResolvePluginType();
            if (type == null) return null;
            try
            {
                return type.GetField(fieldName,
                    BindingFlags.Public | BindingFlags.Static)?.GetValue(null)
                    as ConfigEntryBase;
            }
            catch
            {
                return null;
            }
        }

        private static Type? ResolvePluginType()
        {
            if (_pluginType != null) return _pluginType;

            foreach (Assembly assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                if (!string.Equals(assembly.GetName().Name, AssemblyName,
                                   StringComparison.OrdinalIgnoreCase)) continue;
                _pluginType = assembly.GetType(PluginTypeName, false);
                if (_pluginType != null) return _pluginType;
            }

            _pluginType = Type.GetType(PluginTypeName + ", " + AssemblyName, false);
            return _pluginType;
        }

        private static void ApplyNow()
        {
            Type? type = ResolvePluginType();
            if (type == null) return;
            try
            {
                object? instance = type.GetProperty("Instance",
                    BindingFlags.Public | BindingFlags.Static)?.GetValue(null, null);
                type.GetMethod("ApplySettingsNow",
                    BindingFlags.Public | BindingFlags.Instance)?.Invoke(instance, null);
            }
            catch (Exception error)
            {
                Plugin.Log?.LogWarning("[SecondScreen] Performance setting will apply on "
                    + "the next refresh: " + error.Message);
            }
        }
    }
}
