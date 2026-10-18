# MikkTSpace

Unmodified `mikktspace.c` and `mikktspace.h` from
[mmikk/MikkTSpace](https://github.com/mmikk/MikkTSpace/tree/3e895b49d05ea07e4c2133156cfa94369e19e409),
commit `3e895b49d05ea07e4c2133156cfa94369e19e409`.

Copyright (C) 2011 Morten S. Mikkelsen. The zlib-style license notice is
preserved in both source files and the accompanying [LICENSE](LICENSE).
Keep the sources, license and attribution together when updating them.
Git attributes preserve the upstream source bytes and formatting.

The Hydra adapter compiles the C source once as C++, without changing it;
its renderer-owned wrapper stays in
[`generated_tangents.cpp`](../../adapters/hydra2/src/generated_tangents.cpp).
There is no new runtime or build-time download.
