# Session icons

Images placed here populate the Session dialog's icon selector. Each one
becomes a row showing the image beside its name, and selecting a row puts that
image in the dialog's icon area.

- Supported extensions: `.png`, `.svg`, `.jpg`, `.jpeg`, `.bmp`, `.xpm`.
- The combo-box label is the file name without its extension, so name files the
  way they should read in the UI.
- Rows are sorted by that label. The first one is selected when the dialog
  opens, and `Custom` is always last.
- No source change is needed to add an icon. `make install` copies every
  supported image in this directory to `$(pkgdatadir)/images`, and an
  uninstalled run reads them straight from this directory.

The shipped set is the project's twelve GNOME-foot PNGs, restored from
`s1.png`–`s12.png` and renamed after the colour each one shows. Where the foot
is white the name is the circle colour; where the circle is blue the name
carries the foot colour as well:

| File | Circle | Foot |
| --- | --- | --- |
| `gnome_blue_green.png` | blue | green |
| `gnome_blue_indigo.png` | blue | indigo |
| `gnome_blue_pink.png` | blue | pink |
| `gnome_blue_red.png` | blue | red |
| `gnome_blue_yellow.png` | blue | yellow |
| `gnome_green.png` | green | white |
| `gnome_magenta.png` | magenta | white |
| `gnome_maroon.png` | maroon | white |
| `gnome_orange.png` | orange | white |
| `gnome_purple.png` | purple | white |
| `gnome_teal.png` | teal | white |
| `gnome_yellow.png` | yellow | white |

This file is documentation only and is not installed.
