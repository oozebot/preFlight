add_cmake_project(JPEG
    URL https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/3.1.0.zip
    URL_HASH SHA256=2ddfaadf8b660050ff066a03833416bf8500624f014877b80eff16e799f68e81
    CMAKE_ARGS
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
)

set(DEP_JPEG_DEPENDS ZLIB)
