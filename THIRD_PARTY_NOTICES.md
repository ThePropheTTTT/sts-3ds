# Third-party notices

This repository contains original C code, Python extraction/build tooling, and
documentation for a non-commercial fan port. It does not distribute the game
itself or any of its extracted art, audio, fonts, card text, or event text.

## Slay the Spire

*Slay the Spire* and all related names, card text, artwork, audio, and other
game content are the property of Mega Crit, Humble Games, and their respective
rights holders. This project is not affiliated with, endorsed by, or sponsored
by them.

The extraction tools require the user to provide their own legally purchased
copy of the game. Generated assets and generated C headers are ignored by Git
and are not part of this repository.

## sts_lightspeed

No source code from
[sts_lightspeed](https://github.com/BoardEngineer/sts_lightspeed) is included
in this repository. Early design notes mentioned reusing it; the port was
ultimately written as original C code, with combat rules checked against the
game's own behaviour. There is therefore no MIT notice for that project to
carry here, and no C++/SDL2 code is vendored.

## Build-time dependencies

The build links against devkitPro components, including libctru, citro2d, and
citro3d. They are not vendored in this repository; their own licenses apply.
See <https://devkitpro.org/> and the license files installed with devkitPro.

The Python tooling uses Pillow and NumPy. They are installed separately and
are not vendored; their own licenses apply.
