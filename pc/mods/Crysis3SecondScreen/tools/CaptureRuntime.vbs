' Explicitly invoked research helper. No game injection or input operations.
' Run hidden in the existing Wine prefix and retain an exit-status record.
Option Explicit
Dim fs, shell, folder, command, result, status, captureMode
Set fs = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")
folder = fs.GetParentFolderName(WScript.ScriptFullName)
If fs.FolderExists(folder & "\capture") Then WScript.Quit 10
If fs.FileExists(folder & "\capture-exit.txt") Then WScript.Quit 10
command = Chr(34) & folder & "\Crysis3RuntimeCapture.exe" & Chr(34)
captureMode = " --auto "
If WScript.Arguments.Count = 1 Then
    If WScript.Arguments(0) = "p" Then
        captureMode = " --probe-auto "
    ElseIf WScript.Arguments(0) = "i" Then
        captureMode = " --inspect-auto "
    ElseIf WScript.Arguments(0) = "e" Then
        captureMode = " --performance-auto "
    Else
        WScript.Quit 2
    End If
End If
command = command & captureMode & Chr(34) & folder & "\capture" & Chr(34)
command = "cmd.exe /d /s /c " & Chr(34) & command & " > " & Chr(34)
command = command & folder & "\capture-tool.log" & Chr(34) & " 2>&1" & Chr(34)
result = shell.Run(command, 0, True)
Set status = fs.CreateTextFile(folder & "\capture-exit.txt", False)
status.WriteLine CStr(result)
status.Close
WScript.Quit result
