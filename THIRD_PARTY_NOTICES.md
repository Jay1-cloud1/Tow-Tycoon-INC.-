# Third-party notices

Tow Tycoon itself is all rights reserved (see LICENSE.txt). The Windows download also ships open-source libraries as separate `.dll` files, each under its own license. The release workflow copies the full license text for every bundled library into the `licenses` folder of the zip.

The main libraries are:

| Library | License |
| --- | --- |
| SDL2, SDL2_image, SDL2_ttf | zlib |
| FreeType | FreeType License (FTL) |
| HarfBuzz | MIT |
| libpng, zlib | libpng / zlib |
| libjpeg-turbo, libtiff, libwebp, libavif, libaom, dav1d, rav1e, SVT-AV1, libjxl, libyuv, zstd, brotli, bzip2, liblzma, lcms2, libdeflate, PCRE2, Highway, LERC | permissive (BSD, MIT, Apache-2.0 or similar) |
| GLib, libiconv, gettext (libintl), Graphite2 | LGPL (shipped as unmodified, replaceable DLLs) |
| JBIG-KIT (libjbig) | GPL-2.0 |
| GCC runtime (libgcc, libstdc++), winpthreads | GPL with the GCC Runtime Library Exception / MIT |

JBIG-KIT is pulled in by libtiff in the MSYS2 build of SDL2_image. Its GPL license is a concern for a closed-source game, so it should be removed from the shipped build (for example by building SDL2_image without TIFF support, since the game only loads PNG files) before a public 1.0 release.

## Game graphics

`Assets/openttd/` holds sprites taken from OpenGFX, the OpenTTD base graphics set. OpenGFX is licensed under GPL-2.0, which requires the sprites (and possibly the game that ships them) to be shared under the same terms. Credit OpenGFX and ship its license text with any build that includes these files, and replace them with original art before a commercial or all-rights-reserved 1.0 release.
