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

Levels 1-40, up to three classes. From level 21 a character is epic: it gets the Epic Character feat, `EpicLevel` is set, and the epic feats of its class lists are open (skill ranks are bought before feats from level 21, since epic feats ask for up to level+3 ranks). There are no epic spells or epic spell slots. The seed is printed, and the same options and seed give byte-identical files on any platform. `--variety 0-3` controls how closely feat, skill and spell choices follow the class's `packages.2da` preference lists. Command-line options override a spec file.

## Prestige classes

`--classes fighter:9,divinechampion:3` (or `dwarvendefender`) takes prestige levels after base levels. Supported so far: Divine Champion (also `championoftorm`), Dwarven Defender and Pale Master (wizard levels only: its extra caster levels on top of sorcerer or bard levels are not modelled; it raises the wizard's spell slots by one level for every two Pale Master levels, rounded up); the others are refused until their rules (spell levels, weapon-of-choice picks, ability gains) are modelled. A prestige class cannot be the first class and counts toward the three-class limit. Entry requirements come from the install's `cls_pres_*.2da`: skills, feats, any-of feat groups, race, base attack bonus, arcane or any spell level, earlier classes. The plan checks what picks cannot change (base attack bonus by that level, rank caps, race, alignment) before building, then pins the required feats and weapon focus, buys the required skill ranks early (cross-class if need be) and raises the base ability scores the feat chains need (Dodge wants Dex 13). A build that cannot qualify stops with the reason. A random race is limited to those the plan allows. `bicverify` checks these premades level by level (the game's own premades ignore the entry requirements, which it reports as notes); some stored saves and hit points differ from the derived ones (see Limits).

## Starting equipment

Each character starts with the items of its first class's package (`packages.2da` `Equip2DA`), stored in the pack the way the game stores a new character: full item structs built from the install's `.uti` templates, laid out in the pack grid, stackables stacked, and one cloth robe worn. Weapons and armor are not equipped; do that in game. `bicverify` rebuilds these items and compares them with the game's own level-1 premades (fields, ObjectIds and pack positions).

## Guided builder

`bicgen --guided` walks through the build on the terminal: race, level, classes (a fixed split, a random mix, or the class at every level-up), alignment, gender, ability scores, name, then each level's choices (starting package, domains, school, ability increase, feats, spells, skills), the familiar or companion, and a final look at the sheet before anything is written.

At every prompt Enter takes the automatic pick, `?` lists the choices (`*` marks the ones on the class's package list), and a number, a name or part of a name picks one. `auto` finishes the current step automatically, `level` the current level, `all` the whole character, and `q` quits (nothing is written). At the feat prompt `skills` buys skills first, which is how to meet a skill rank prerequisite such as Ride for Mounted Combat.

Anything already given on the command line or in a spec file is not asked again. The session starts by printing the seed; the answers are saved next to the file as `NAME.answers`, and the same options, seed and answers replay the same character:

    build/bicgen --guided --seed 77 --out-dir out < out/miratest.answers

A session of nothing but Enters gives byte-for-byte the file the same seed gives without `--guided`. A pinned feat that could not be taken is shown as a warning at the review (the unguided builder refuses to write the file instead).

## Limits

No purchased or magical equipment (characters get their package's items and gold only, nothing is worn but a robe), only two prestige classes (below), the stored saves and hit points of a few epic and prestige premades still differ from the derived values by a few points (worn-item bonuses, Luck of Heroes, Strong Soul, Snake Blood, Sacred Defense, Epic Toughness and the epic save feats are modelled; what remains is the Pale Master Deathless Vigor hit points and small offsets in the epic save rows, the cause of which is not known; the game may recompute these on load), familiars and companions are random unless chosen in the guided builder, and appearance (head, skin, hair, portrait) is always random.

## License

MIT. See `LICENSE`. This project is not affiliated with or endorsed by Beamdog or Atari; Neverwinter Nights is their trademark, and no game data is distributed here.
