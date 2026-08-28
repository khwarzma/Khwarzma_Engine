#include <khcomp/image_frame.hpp>
#include <algorithm>
#include <cmath>

namespace khcomp::image {

void ImageFramePipeline::extract_8x8_block(
    ReadOnlyBuffer src,
    uint16_t width,
    uint16_t block_x,
    uint16_t block_y,
    std::span<float, kBlockElements> block_out) noexcept
{
    const uint8_t* raw_ptr = src.data();
    for (size_t y = 0; y < kBlockSize; ++y) {
        const size_t row_offset = (static_cast<size_t>(block_y) + y) * width + block_x;
        for (size_t x = 0; x < kBlockSize; ++x) {
            // Level shift byte [0, 255] -> [-128.0, 127.0]
            block_out[y * kBlockSize + x] = static_cast<float>(raw_ptr[row_offset + x]) - 128.0f;
        }
    }
}

void ImageFramePipeline::store_8x8_block(
    std::span<const float, kBlockElements> block_in,
    uint16_t width,
    uint16_t block_x,
    uint16_t block_y,
    MutableBuffer dst) noexcept
{
    uint8_t* raw_ptr = dst.data();
    for (size_t y = 0; y < kBlockSize; ++y) {
        const size_t row_offset = (static_cast<size_t>(block_y) + y) * width + block_x;
        for (size_t x = 0; x < kBlockSize; ++x) {
            // Level unshift [-128.0, 127.0] -> [0, 255] with clamping
            const float val = std::round(block_in[y * kBlockSize + x] + 128.0f);
            const float clamped = std::clamp(val, 0.0f, 255.0f);
            raw_ptr[row_offset + x] = static_cast<uint8_t>(clamped);
        }
    }
}

Result<size_t> ImageFramePipeline::encode_grayscale_frame(
    ImageHeader header,
    ReadOnlyBuffer raw_pixels,
    core::BitStreamWriter& writer) noexcept
{
    if (raw_pixels.data() == nullptr || raw_pixels.size() < static_cast<size_t>(header.width) * header.height) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    if (header.width % kBlockSize != 0 || header.height % kBlockSize != 0 || header.width == 0 || header.height == 0) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    DctQuantEngine dct_engine(header.quality_factor);
    core::ContextModel model(2);
    core::ArithmeticEncoder encoder(&writer);

    alignas(64) std::array<float, kBlockElements> block_input{};
    alignas(64) std::array<float, kBlockElements> dct_coeffs{};
    alignas(64) std::array<int16_t, kBlockElements> quant_coeffs{};
    alignas(64) std::array<int16_t, kBlockElements> zigzag_coeffs{};

    int16_t prev_dc = 0;

    for (uint16_t by = 0; by < header.height; by += kBlockSize) {
        for (uint16_t bx = 0; bx < header.width; bx += kBlockSize) {
            extract_8x8_block(raw_pixels, header.width, bx, by, block_input);

            auto res_dct = dct_engine.forward_dct(block_input, dct_coeffs);
            if (!res_dct) return std::unexpected(res_dct.error());

            auto res_q = dct_engine.quantize(dct_coeffs, quant_coeffs);
            if (!res_q) return std::unexpected(res_q.error());

            auto res_zz = dct_engine.zigzag_serialize(quant_coeffs, zigzag_coeffs);
            if (!res_zz) return std::unexpected(res_zz.error());

            // 1. Delta DC Coding with ZigZag integer mapping for sign preservation
            const int16_t current_dc = zigzag_coeffs[0];
            const int16_t dc_delta = static_cast<int16_t>(current_dc - prev_dc);
            prev_dc = current_dc;

            const uint16_t u_dc = map_int16_to_uint16(dc_delta);
            auto r1 = encoder.encode_symbol(static_cast<uint8_t>((u_dc >> 8) & 0xFF), model);
            if (!r1) return std::unexpected(r1.error());
            auto r2 = encoder.encode_symbol(static_cast<uint8_t>(u_dc & 0xFF), model);
            if (!r2) return std::unexpected(r2.error());

            // 2. Find last non-zero AC coefficient index for EOB optimization
            size_t last_nonzero = 0;
            for (size_t i = kBlockElements - 1; i >= 1; --i) {
                if (zigzag_coeffs[i] != 0) {
                    last_nonzero = i;
                    break;
                }
            }

            // 3. Encode AC Coefficients
            if (last_nonzero > 0) {
                uint8_t zero_run = 0;
                for (size_t i = 1; i <= last_nonzero; ++i) {
                    const int16_t ac_val = zigzag_coeffs[i];
                    if (ac_val == 0) {
                        if (zero_run == 254) {
                            // Emit max run marker (run=254, val=0)
                            auto r = encoder.encode_symbol(254, model);
                            if (!r) return std::unexpected(r.error());
                            r = encoder.encode_symbol(0, model);
                            if (!r) return std::unexpected(r.error());
                            r = encoder.encode_symbol(0, model);
                            if (!r) return std::unexpected(r.error());
                            zero_run = 0;
                        } else {
                            ++zero_run;
                        }
                    } else {
                        auto r = encoder.encode_symbol(zero_run, model);
                        if (!r) return std::unexpected(r.error());

                        const uint16_t u_ac = map_int16_to_uint16(ac_val);
                        r = encoder.encode_symbol(static_cast<uint8_t>((u_ac >> 8) & 0xFF), model);
                        if (!r) return std::unexpected(r.error());

                        r = encoder.encode_symbol(static_cast<uint8_t>(u_ac & 0xFF), model);
                        if (!r) return std::unexpected(r.error());

                        zero_run = 0;
                    }
                }
            }

            // 4. Emit End-Of-Block (EOB) marker (0xFF marker byte)
            auto reob = encoder.encode_symbol(0xFF, model);
            if (!reob) return std::unexpected(reob.error());
        }
    }

    auto res_flush = encoder.flush();
    if (!res_flush) return std::unexpected(res_flush.error());

    return writer.bits_written() / 8;
}

Result<size_t> ImageFramePipeline::decode_grayscale_frame(
    ImageHeader header,
    core::BitStreamReader& reader,
    MutableBuffer output_pixels) noexcept
{
    if (output_pixels.data() == nullptr || output_pixels.size() < static_cast<size_t>(header.width) * header.height) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    if (header.width % kBlockSize != 0 || header.height % kBlockSize != 0 || header.width == 0 || header.height == 0) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    DctQuantEngine dct_engine(header.quality_factor);
    core::ContextModel model(2);
    core::ArithmeticDecoder decoder;

    auto res_init = decoder.set_reader(&reader);
    if (!res_init) return std::unexpected(res_init.error());

    alignas(64) std::array<int16_t, kBlockElements> zigzag_coeffs{};
    alignas(64) std::array<int16_t, kBlockElements> quant_coeffs{};
    alignas(64) std::array<float, kBlockElements> dequant_coeffs{};
    alignas(64) std::array<float, kBlockElements> block_output{};

    int16_t prev_dc = 0;

    for (uint16_t by = 0; by < header.height; by += kBlockSize) {
        for (uint16_t bx = 0; bx < header.width; bx += kBlockSize) {
            zigzag_coeffs.fill(0);

            // 1. Decode DC Delta
            auto dc_hi = decoder.decode_symbol(model);
            if (!dc_hi) return std::unexpected(dc_hi.error());
            auto dc_lo = decoder.decode_symbol(model);
            if (!dc_lo) return std::unexpected(dc_lo.error());

            const uint16_t u_dc = static_cast<uint16_t>((static_cast<uint16_t>(*dc_hi) << 8) | static_cast<uint16_t>(*dc_lo));
            const int16_t dc_delta = unmap_uint16_to_int16(u_dc);
            const int16_t current_dc = static_cast<int16_t>(prev_dc + dc_delta);
            zigzag_coeffs[0] = current_dc;
            prev_dc = current_dc;

            // 2. Decode AC Coefficients
            size_t ac_idx = 1;
            while (ac_idx < kBlockElements) {
                auto run_sym = decoder.decode_symbol(model);
                if (!run_sym) return std::unexpected(run_sym.error());

                const uint8_t run = *run_sym;
                if (run == 0xFF) {
                    // EOB marker reached
                    break;
                }

                ac_idx += run;
                if (ac_idx >= kBlockElements) break;

                auto val_hi = decoder.decode_symbol(model);
                if (!val_hi) return std::unexpected(val_hi.error());
                auto val_lo = decoder.decode_symbol(model);
                if (!val_lo) return std::unexpected(val_lo.error());

                const uint16_t u_ac = static_cast<uint16_t>((static_cast<uint16_t>(*val_hi) << 8) | static_cast<uint16_t>(*val_lo));
                const int16_t ac_val = unmap_uint16_to_int16(u_ac);

                if (ac_val != 0) {
                    zigzag_coeffs[ac_idx] = ac_val;
                    ++ac_idx;
                }
            }

            // 3. Deserialization -> Dequantization -> IDCT -> Store Frame
            auto res_des = dct_engine.zigzag_deserialize(zigzag_coeffs, quant_coeffs);
            if (!res_des) return std::unexpected(res_des.error());

            auto res_dq = dct_engine.dequantize(quant_coeffs, dequant_coeffs);
            if (!res_dq) return std::unexpected(res_dq.error());

            auto res_idct = dct_engine.inverse_dct(dequant_coeffs, block_output);
            if (!res_idct) return std::unexpected(res_idct.error());

            store_8x8_block(block_output, header.width, bx, by, output_pixels);
        }
    }

    return output_pixels.size();
}

} // namespace khcomp::image