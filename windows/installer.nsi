; NSIS installer for Pomodoro Desk.
; Build: makensis windows/installer.nsi   (run from the repo root, after building dist/pomodoro.exe)
;
; Installs per-user (no admin prompt), adds a Start Menu shortcut so the app is
; indexed by Windows Search and can be right-click "Pin to taskbar" from there,
; plus a Desktop shortcut and a normal entry in Apps & Features.

!define APP_NAME "Pomodoro Desk"
!define APP_EXE "pomodoro.exe"
!define APP_PUBLISHER "Bharath"
!define APP_VERSION "2.0.0"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\PomodoroDesk"

RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\PomodoroDesk"
Name "${APP_NAME}"
OutFile "..\dist\PomodoroDeskSetup.exe"

Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

Section "Install"
    SetOutPath "$INSTDIR"
    File "..\dist\pomodoro.exe"

    CreateShortCut "$SMPROGRAMS\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
    CreateShortCut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"

    WriteUninstaller "$INSTDIR\Uninstall.exe"

    WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "${APP_PUBLISHER}"
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
SectionEnd

Section "Uninstall"
    Delete "$INSTDIR\${APP_EXE}"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    Delete "$SMPROGRAMS\${APP_NAME}.lnk"
    Delete "$DESKTOP\${APP_NAME}.lnk"
    DeleteRegKey HKCU "${UNINST_KEY}"
SectionEnd
