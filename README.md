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

    build/bicgen --guided                                 # build one interactively

Levels 1-20, up to three classes. The seed is printed, and the same options and seed give byte-identical files on any platform. `--variety 0-3` controls how closely feat, skill and spell choices follow the class's `packages.2da` preference lists. Command-line options override a spec file.

## Guided builder

`bicgen --guided` walks through the build on the terminal: race, level, classes (a fixed split, a random mix, or the class at every level-up), alignment, gender, ability scores, name, then each level's choices (starting package, domains, school, ability increase, feats, spells, skills), the familiar or companion, and a final look at the sheet before anything is written.

At every prompt Enter takes the automatic pick, `?` lists the choices (`*` marks the ones on the class's package list), and a number, a name or part of a name picks one. `auto` finishes the current step automatically, `level` the current level, `all` the whole character, and `q` quits (nothing is written). At the feat prompt `skills` buys skills first, which is how to meet a skill rank prerequisite such as Ride for Mounted Combat.

Anything already given on the command line or in a spec file is not asked again. The session starts by printing the seed; the answers are saved next to the file as `NAME.answers`, and the same options, seed and answers replay the same character:

    build/bicgen --guided --seed 77 --out-dir out < out/miratest.answers

A session of nothing but Enters gives byte-for-byte the file the same seed gives without `--guided`.

## Limits

No starting equipment (gold only), no prestige classes, no epic levels, familiars and companions are random unless chosen in the guided builder, and appearance (head, skin, hair, portrait) is always random.

## License

MIT. See `LICENSE`. This project is not affiliated with or endorsed by Beamdog or Atari; Neverwinter Nights is their trademark, and no game data is distributed here.
