# Measured Layout Specification — 320×240 ST7789 LCD

Coordinates are LCD-content pixels (x: 0..319, y: 0..239), measured from the
supplied reference PNGs after removing the decorative device bezel
(content crop 1297×973 @ +76,+84 → scaled to 320×240). Borders are single
~2px lines with ~4px rounded (pixel-stepped) corners unless noted.

## Challenge Complete (ref: UI designs/chall_completionpage.png)

| Element                 | x (px)     | y (px)    | w×h      | font / notes |
|-------------------------|------------|-----------|----------|--------------|
| Branding "SYSRUPT…"     | 6 … 253    | 2 … 14    | —        | fontBig green (headerBar) |
| Score box "SCORE: N/100"| 214 … 319  | 0 … 17    | 106×18   | blue border, fontSmall |
| Header underline (blue) | 0 … ~310   | 18 … 19   | 2px line | |
| Metadata "> CAT > NAME" | 6 … ~145   | 20 … 33   | —        | cat blue, name white fontSmall |
| "[ DIFF ]" box (blue)   | 150 … 259  | 20 … 37   | —        | fontTiny, blue border |
| "POINTS: n"             | 263 … 316  | 22 … 33   | —        | fontTiny gray |
| Metadata separator blue | 0 … 319    | 36 … 37   | 2px line | full width |
| **Main panel** (blue)   | 0 … 319    | 40 … 237  | 320×198  | 2px border, radius 4 |
| Trophy emblem (image)   | 50 … 269   | 47 … 99   | 220×52   | A8 bitmap, recolor green, centered |
| Heading box (green)     | 54 … 265   | 100 … 122 | 212×23   | 2px green border r4, fontBig "CHALLENGE COMPLETE" |
| Description (2 lines)   | centered   | 124 … 146 | —        | fontTiny white, centered |
| Separator (blue)        | 28 … 292   | 148 … 150 | 2px line | inset, NOT full width |
| Score panel LEFT (blue) | 5 … 157    | 180 … 213 | 153×34   | caption fontTiny, value fontBig |
| Score panel RIGHT(blue) | 163 … 314  | 180 … 213 | 152×34   | |
| Button LEFT "NEXT…"(grn)| 5 … 156    | 215 … 235 | 152×21   | green border+text, faint fill |
| Button RIGHT "LIST"(blu)| 163 … 317  | 215 … 235 | 155×21   | blue border+text |
| Column centre gap       | ~158 … 162 | —         | ~6px     | between L/R columns |

## Reset Session (ref: UI designs/reset_page.png)

Header + metadata row identical to Challenge Complete.

| Element                     | x (px)     | y (px)    | w×h     | notes |
|-----------------------------|------------|-----------|---------|-------|
| **Main panel** (blue)       | 0 … 319    | 40 … 207  | 320×168 | 2px border, radius 4 |
| Warning triangle (image)    | ~52 … 90   | 46 … 75   | 38×30   | A8 recolor red; "!" is transparent hole |
| "RESET SESSION?" (red)      | ~95 … 279  | 51 … 75   | —       | fontBig red, letter_space −2 |
| Red dashed separator        | 22 … 288   | 79        | dash4/gap3 | under heading |
| Warning text (2 lines)      | centered   | 85 … 108  | —       | fontTiny white, centered |
| Red summary panel           | 6 … 303    | 103 … 177 | 297×74  | 2px red border, radius 4 |
|  · "THE FOLLOWING…" (red)   | +6 … …     | +4        | —       | fontTiny red |
|  · red dashed sep           | +6 … 289   | +14       | —       | full panel width |
|  · 5 items, triangular ▶    | bullet@+2  | +20,30,40,50,60 | — | 5×5 red triangle + fontTiny white |
| Button RESET (green)        | 5 … 153    | 141 … 165 | 148×24  | refresh icon + 2-line text, green |
| Button CANCEL (red)         | 159 … 307  | 141 … 165 | 148×24  | X icon + 2-line text, red (default focus) |
| Footer strip (blue top)     | 0 … 319    | 211 … 232 | —       | 3 groups + 2 blue dividers |

Baked assets (A8, recoloured at draw): `assets/TrophyImg.c` (220×52), `assets/ResetIcons.c`
(warn_tri 38×30, refresh 25×20, xmark 19×17). Shared builders `buildCompleteScreen`
(CompleteLayout.cpp) and `buildResetScreen` (ResetLayout.cpp) are called by both the production
apps and the host render harness, so the rendered PNGs are the exact production widget code.
