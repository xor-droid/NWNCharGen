# NWNCharGen

A character generator for Neverwinter Nights: Enhanced Edition, written in POSIX C (C99, no external libraries). It writes real `.bic` files.

Game rules (2DAs, TLK) are read at runtime from your own NWN install. No game data is included in this repository. Vanilla rules only (no haks or CEP).

Status: NWN1:EE only. NWN2:EE support is planned. A generated specialist wizard has been loaded in NWN1:EE; other classes and levels are verified against the premade characters shipped with the game, but not yet load-tested in-game.

## Build

    make
    make check      # byte-reproducibility check against tests/golden.sha256

Tools are built into `build/`: `bicgen` (the generator), `bicverify` (replays characters against the rules), `gffdump`, `reslist`.

## Use

    build/bicgen --seed 1234                              # random level-1 character
    build/bicgen --level 12 --classes "fighter:6,wizard:6" --seed 7
    build/bicgen --school illusion --seed 5               # specialist wizard
    build/bicgen --spec examples/duelist.ini              # from a spec file
    build/bicgen --pin "Weapon Finesse" --avoid "Cleave"  # steer feat picks
    build/bicgen --find-feat whirlwind                    # look up feats

Levels 1-20, up to three classes. The seed is printed, and the same options and seed give byte-identical files on any platform. `--variety 0-3` controls how closely feat, skill and spell choices follow the class's `packages.2da` preference lists. Command-line options override a spec file.

## Limits

No starting equipment (gold only), no prestige classes, no epic levels, familiars and companions are random, and there is no interactive builder yet.
