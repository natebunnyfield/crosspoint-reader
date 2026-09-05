# Cutting a release without touching the Mac

Owner ask, 2026-09-05: "make an applescript to cut releases without me running
anything". This is the firmware twin of the simulator's
`ios/deploy.applescript`, and it works the same way: an AppleScript hands a
wrapper to Terminal.app, the wrapper pulls, bumps, builds, verifies, tags and
publishes, and the result lands on the phone as an ntfy notification.

## One command

```bash
ssh <your-mac> 'osascript ~/src/crosspoint-reader/scripts/release.applescript'
```

As an iOS Shortcut, one action: **Run Script Over SSH** with that `osascript`
line. Pin it next to "X3 Deploy". Nothing else to type: if the current version
is already tagged, the wrapper bumps the patch number itself.

Variants, passed as KEY=VALUE arguments (they reach the wrapper through `env`):

```bash
osascript scripts/release.applescript "CROSSPOINT_DRY_RUN=1"     # build + verify, no tag, no publish
osascript scripts/release.applescript "CROSSPOINT_AUTO_BUMP=0"   # refuse if the tag exists, like release.sh alone
```

## What the wrapper does

`scripts/release-from-repo.sh`, in order, refusing loudly at each step:

1. **Checkout.** `~/src/crosspoint-reader` (override with
   `CROSSPOINT_FIRMWARE_DIR`), origin must be the fork, tree must be clean,
   `main` is pulled `--ff-only`.
2. **Toolchain.** Finds `pio` in the usual install locations if the shell
   lacks it; needs `gh` logged in.
3. **Disk.** Under 6 GB free (`CROSSPOINT_MIN_FREE_GB`), it deletes the two
   regenerable build trees, `.pio/build` and `~/.platformio/build_cache`,
   before building. The 2026-09-05 release died on "No space left on device"
   in exactly that state.
4. **Version.** If `platformio.ini`'s version is already tagged, it bumps the
   patch number (`1.5.23-BD` -> `1.5.24-BD`), writes the usual history note
   above the version line, commits `chore(release): old -> new` with the
   commits since the old tag in the body, and pushes `main`. So every firing
   cuts the next patch release of whatever `main` holds.
5. **Release.** `./scripts/release.sh`, unchanged: build `gh_release`, verify
   the image against the source (B-033, B-046), tag with the full version
   string, publish with `gh release create`.

Every refusal and the final result go to the same ntfy topic the iOS deploy
uses, so the phone sees "published" with the release URL, "refused" with the
reason, or "failed" with the step.

## One-time setup on the Mac

The same as for the iOS deploy: allow the SSH-launched `osascript` to control
Terminal under Privacy & Security > Automation (the prompt appears on the
Mac's screen the first time), and the Mac must be logged in and unlocked when
you fire.
