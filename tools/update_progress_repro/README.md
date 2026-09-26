# Update Fonts / Update Library progress repro

Instruments behind `docs/update-progress-2026-09-26.md`. They drive the
desktop simulator (`simulator_x3`) through Settings > Wi-Fi Networks (joins the
fake open network) and then Update Fonts or Update Library, against a MOCK
GitHub release: `CROSSPOINT_SIM_HTTP_MOCK_ROOT` answers the release JSON by URL
basename, and the asset URLs in it point at `slowserve.py`, a local server that
throttles to `RATE` bytes/s after a `DELAY`-second first byte. No token or
network is used; `CROSSPOINT_SIM_GITHUB_TOKEN` is set to a dummy only so the
screens get past NO_TOKEN.

    export UPD_WORK=$(mktemp -d)
    python3 mkfixture.py 8766                 # payload from ../../fs_/fonts and fs_ epubs
    RATE=1000000 DELAY=1.5 python3 slowserve.py 8766 &
    cp ../../.pio/build/simulator_x3/program "$UPD_WORK/program"
    ./run.sh "$UPD_WORK/program" fonts fonts          # or: library lib
    ./run.sh "$UPD_WORK/program" fonts cancel ';41000:BACK'
    python3 gaps.py "$UPD_WORK/fonts.log"             # max gap between presents

`gaps.py` reports two numbers: every present, and NEW-picture presents only (a
present whose preceding `[accum]` line says `changed=1`). The second is the one
that matters: in dark mode the phosphor trail re-presents the same picture ~60
times a second whenever the main thread is free, which inflates the first.
Kill the server afterwards (`pkill -f slowserve.py`).
