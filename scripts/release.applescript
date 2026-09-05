-- release.applescript — cut a firmware release in Terminal.app, hands-free.
--
-- Mirrors crosspoint-simulator/ios/deploy.applescript. gh and pio live in the
-- login shell's PATH and the build wants the Mac's own toolchain, so the
-- command is handed to Terminal.app under the logged-in user rather than run
-- in the caller's (SSH or agent) shell.
--
-- From the phone (Terminus/Blink, or a Shortcuts "Run Script Over SSH" action):
--   ssh <mac> 'osascript ~/src/crosspoint-reader/scripts/release.applescript'
--
-- Arguments are KEY=VALUE pairs passed through `env` into the Terminal
-- subshell (env vars on the osascript caller's shell are LOST otherwise):
--   osascript scripts/release.applescript "CROSSPOINT_DRY_RUN=1"
--   osascript scripts/release.applescript "CROSSPOINT_AUTO_BUMP=0"
--
-- Requirements (one-time): allow osascript/sshd-launched processes to control
-- Terminal under Privacy & Security > Automation, and the Mac must be logged
-- in and unlocked. AppleScript returns once Terminal starts the command; the
-- result arrives as release-from-repo.sh's ntfy notification on the phone.

on run argv
    set wrapperPath to "$HOME/src/crosspoint-reader/scripts/release-from-repo.sh"

    set envPrefix to ""
    repeat with arg in argv
        set envPrefix to envPrefix & (quoted form of (arg as text)) & " "
    end repeat

    set scriptCmd to ""
    if envPrefix is not "" then
        set scriptCmd to "env " & envPrefix
    end if
    set scriptCmd to scriptCmd & wrapperPath

    tell application "Terminal"
        activate
        do script scriptCmd
    end tell
end run
