# Deep-sky source tables

Small, unmodified catalogue files used by `astroml data dso` (tools/data/dso.py). They are kept in
the repository so release builds do not depend on the CDS query service.

| File | Catalogue | Reference |
|---|---|---|
| `barnard_VII_220A.dat` | CDS VII/220A, Barnard's Catalogue of 349 Dark Objects in the Sky | Barnard E.E., 1927, *A Photographic Atlas of Selected Regions of the Milky Way*, Carnegie Institution of Washington |
| `sharpless_VII_20.dat` | CDS VII/20, Catalogue of HII Regions (Sh 2) | Sharpless S., 1959, ApJS 4, 257 |

Both were retrieved from the VizieR FTP mirror (`/ftp/cats/VII/220A/barnard.dat`,
`/ftp/cats/VII/20/catalog.dat.gz`). Byte layouts are described in the
catalogues' ReadMe files. This research made use of the VizieR catalogue access
tool, CDS, Strasbourg, France.

NGC, IC, Messier, Caldwell and Melotte data come from OpenNGC (CC BY-SA 4.0,
Mattia Verga), downloaded at a pinned commit by `astroml data fetch`.
Constellation figures come from d3-celestial (BSD-3-Clause, Olaf Frohn).
