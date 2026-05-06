#include <stdexcept>
#include <zstd.h>


#include "file_helper.h"


using namespace std;

namespace file_helper {
    StreamDecompressor::StreamDecompressor() {
        ZSTD_DCtx* dctx = ZSTD_createDCtx();
        if (!dctx) {
            throw runtime_error("Error : Failed to create ZStd decompression context.");
        }

        ctx = dctx;

        internal_buffer_.resize(ZSTD_DStreamOutSize());
    }

    StreamDecompressor::~StreamDecompressor() {
        if (ctx) {
            ZSTD_freeDCtx(static_cast<ZSTD_DCtx*>(ctx));
        }
    }

    void StreamDecompressor::decompress_chunk(vector<char> &chunk) {
        if (chunk.empty()) return;

        auto* dctx = static_cast<ZSTD_DCtx*>(ctx);

        ZSTD_inBuffer input = {chunk.data(), chunk.size(), 0};

        vector<char> decompressed_result;
        bool has_more_output = true;

        while (input.pos < input.size || has_more_output) {
            ZSTD_outBuffer output = {internal_buffer_.data(), internal_buffer_.size(), 0};

            size_t ret = ZSTD_decompressStream(dctx , &output , &input);

            if (ZSTD_isError(ret)) {
                throw runtime_error(string("Error : ZSTD decompression error.") + ZSTD_getErrorName(ret));
            }

            decompressed_result.insert(
                decompressed_result.end() ,
                internal_buffer_.begin() ,
                internal_buffer_.begin() + static_cast<int>(output.pos)
            );

            has_more_output = (output.pos == output.size);
        }

        chunk = std::move(decompressed_result);
    }

}
