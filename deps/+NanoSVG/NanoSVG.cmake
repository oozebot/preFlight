# The fltk fork (https://github.com/fltk/nanosvg) of https://github.com/memononen/nanosvg is used
# because it implements nsvgRasterizeXY(), which GLTexture::load_from_svg() uses for rasterizing
# svg files from their original size to a squared power of two texture on Windows systems using
# AMD Radeon graphics cards

add_cmake_project(NanoSVG
    URL https://github.com/fltk/nanosvg/archive/abcd277ea45e9098bed752cf9c6875b533c0892f.zip
    URL_HASH SHA256=e859938fbaee4b351bd8a8b3d3c7a75b40c36885ce00b73faa1ce0b98aa0ad34
)