' Explicit separate-process DirectInput forwarding check. Never opens the game.
Option Explicit
Dim fs, shell, folder, command, result, status
Set fs = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")
folder = fs.GetParentFolderName(WScript.ScriptFullName)
If fs.FileExists(folder & "\smoke-exit.txt") Then WScript.Quit 10
command = Chr(34) & folder & "\ProxySmoke.exe" & Chr(34) & " "
command = command & Chr(34) & folder & "\Crysis3Probe.dll" & Chr(34)
command = "cmd.exe /d /s /c " & Chr(34) & command & " > " & Chr(34)
command = command & folder & "\smoke.log" & Chr(34) & " 2>&1" & Chr(34)
result = shell.Run(command, 0, True)
Set status = fs.CreateTextFile(folder & "\smoke-exit.txt", False)
status.WriteLine CStr(result)
status.Close
WScript.Quit result
