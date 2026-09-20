# Troubleshooting

## The installer says XCloudDLSS5Host.exe is missing

You downloaded GitHub's **Source code** archive. That archive contains source only. Use the Windows build artifact/release package, which contains the compiled host beside `Install.cmd`.

## No xCloud window was found

The host currently recognizes Microsoft Edge, Google Chrome and Brave. The normal launcher opens Xbox Cloud Gaming in Edge/Chrome app mode automatically. If you start the host manually, open Xbox Cloud Gaming first.

## DLSS carrier could not initialize

Check:

```text
%LOCALAPPDATA%\BetterXcloudDLSS5\Runtime\neural-runtime\DLSSVideoPlayer.log
```

The first alpha deliberately does not pre-judge GPU compatibility. It asks the actual NGX/runtime stack and reports failure if initialization is refused.

The upstream project officially documents RTX hardware. GTX 1660/1660 Super operation is experimental here.

## The picture is misaligned after resizing

Live resize is not implemented in 0.1. Start the xCloud app maximized, then leave its size unchanged. If it changes, restart **Better Xcloud DLSS5** from the Start menu.

## I need the original xCloud image immediately

Press **F8**. The neural overlay hides and the untouched browser remains underneath. Press F8 again to show it.

Press **F9** to stop the native host completely.

## The overlay stays on top after switching apps

The host automatically hides it when the captured browser is not the foreground window. If anything gets stuck, press F9 or end `XCloudDLSS5Host.exe` in Task Manager.

## Better xCloud features are missing

Better xCloud is installed separately from its official project. This repository does not silently repackage it. Ensure your userscript manager and Better xCloud are installed in the same browser profile used by the launcher.
