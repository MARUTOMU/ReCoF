# Menu font

Inter 4.1 by The Inter Project Authors (https://github.com/rsms/inter), under the SIL Open Font License 1.1 ([OFL.txt](OFL.txt)). GameUI.dll carries these two files as RCDATA resources (tools/units.json "rcdata", B398) and registers them for its own process, so no font file is added to the game folder.

They are the release's extras/ttf/Inter-Regular.ttf and Inter-SemiBold.ttf cut down to the characters the game's languages use, with fontTools 4.68:

```
python -m fontTools.subset Inter-<Weight>.ttf --unicodes="U+0020-007E,U+00A0-017F,U+0370-03FF,U+0400-045F,U+2010-2027,U+2030-203A,U+20AC,U+2122,U+2190-2193" --layout-features="kern,liga,calt" --output-file=Inter-<Weight>-recof.ttf
```
