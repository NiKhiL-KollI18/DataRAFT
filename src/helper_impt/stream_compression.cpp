#include <stdexcept>
#include <zstd.h>


#include "file_helper.h"

using namespace std;

namespace file_helper {
    StreamCompressor::StreamCompressor(int compression_level) {
        ZSTD_CCtx *cctx = ZSTD_createCCtx();
        if (!cctx) {
            throw runtime_error("Error : Failed to create Zstd compression context.");
        }

        ZSTD_CCtx_setParameter(cctx , ZSTD_c_compressionLevel , compression_level);

        ctx = cctx;

        internal_buffer_.resize(ZSTD_CStreamOutSize());
    }

    StreamCompressor::~StreamCompressor() {
        if (ctx) {
            ZSTD_freeCCtx(static_cast<ZSTD_CCtx*>(ctx));
        }
    }

    void StreamCompressor::compress_chunk(vector<char> &chunk, bool is_last_chunk) {
        if (chunk.empty() && !is_last_chunk) return;

        auto* cctx = static_cast<ZSTD_CCtx*>(ctx);

        ZSTD_inBuffer input = {chunk.data(), chunk.size(), 0};

        vector<char> compressed_result;

        ZSTD_EndDirective mode = is_last_chunk ? ZSTD_e_end : ZSTD_e_continue;

        while (input.pos < input.size || is_last_chunk) {
            ZSTD_outBuffer output = {internal_buffer_.data(), internal_buffer_.size(), 0};

            size_t remaining = ZSTD_compressStream2(cctx , &output , &input , mode);

            if (ZSTD_isError(remaining)) {
                throw runtime_error(string("ZSTD compression error : ") + ZSTD_getErrorName(remaining));
            }

            compressed_result.insert(
              compressed_result.end(),
              internal_buffer_.begin(),
              internal_buffer_.begin() + static_cast<long long>(output.pos)
            );

            if (is_last_chunk && remaining == 0) break;
            if (!is_last_chunk && input.pos == input.size) break;
        }
        chunk = std::move(compressed_result);
    }
}
