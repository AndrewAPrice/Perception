The following 'exclude' files can be uncommented from third_party.json when the dependencies have been ported.

Requires dng_area_task.h in Adobe DNG SDK:
source/src/codec/SkRawCodec.cpp
source/src/codec/SkRawCodec.h

Requires wuffs-v0.3.c:
source/src/codec/SkWuffsCodec.cpp
source/src/codec/SkWuffsCodec.h

Requires webp/decode.h.h:
source/src/codec/SkWebpCodec.cpp
source/src/codec/SkWebpCodec.h

Requires avif/avif.h:
source/src/codec/SkAvifCodec.cpp
source/src/codec/SkAvifCodec.h

Requires jxl/codestream_header.h:
source/src/codec/SkJpegxlCodec.cpp
source/src/codec/SkJpegxlCodec.h
