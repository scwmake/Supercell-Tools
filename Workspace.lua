workspace "ScFlash"
    architecture "x64"

    configurations {
        "Debug",
        "Release"
    }

    include "./"

    group "Compression"
		include "dependencies/Compression"
        include "dependencies/Compression/dependencies/lzma"
        include "dependencies/Compression/dependencies/lzham"
        include "dependencies/Compression/dependencies/zstd"

