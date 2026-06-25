' ============================================================
' CleanupUserDb.vbs - WiX v4 deferred custom action
' ============================================================
' Runs on uninstall (NOT upgrade) to remove the user's SQLite
' database so a fresh install doesn't see stale data.
'
' The MSI property AppDataFolder resolves to the per-user Roaming
' AppData directory (e.g., C:\Users\Bob\AppData\Roaming).
' As a fallback we ask the Shell for the same folder via the
' KnownFolder id &H1C (=ssfAPPDATA, =0x1C = 28).
' ============================================================

On Error Resume Next

Dim appData, dbPath
appData = Session.Property("AppDataFolder")
If appData = "" Then
    appData = CreateObject("Shell.Application").Namespace(&H1C).Self.Path
End If

dbPath = appData & "\DocuSearch\docusearch.db"

Dim fso
Set fso = CreateObject("Scripting.FileSystemObject")
If fso.FileExists(dbPath) Then
    fso.DeleteFile dbPath, True
End If

' Also remove the -wal and -shm sidecar files if present.
If fso.FileExists(dbPath & "-wal") Then
    fso.DeleteFile dbPath & "-wal", True
End If
If fso.FileExists(dbPath & "-shm") Then
    fso.DeleteFile dbPath & "-shm", True
End If
