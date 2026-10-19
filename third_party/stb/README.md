# stb_image_write

Unmodified `stb_image_write.h` v1.16 from
[nothings/stb](https://github.com/nothings/stb/tree/2c980bb59875b0d32144a71867fbdebb2f77cd20),
commit `2c980bb59875b0d32144a71867fbdebb2f77cd20`.

Used under the MIT option in [LICENSE](LICENSE). Copyright (c) 2017 Sean Barrett.
Keep the upstream source, license and attribution together when updating.

The viewport's private `src/capture.cpp` compiles the encoder. The wrapper
converts premultiplied linear-sRGB RGBA8 to straight sRGB RGBA8, then adds
the PNG sRGB chunk (perceptual intent). No runtime or build-time download
is needed. This dependency is not linked into core or Vulkan targets.
