#include <khcomp/video_ring_buffer.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace khcomp::video {

uint32_t MotionEstimator::compute_sad(
    ReadOnlyBuffer curr_frame,
    ReadOnlyBuffer ref_frame,
    uint16_t width,
    uint16_t block_x,
    uint16_t block_y,
    int32_t ref_x,
    int32_t ref_y) noexcept
{
    const uint8_t* curr_ptr = curr_frame.data();
    const uint8_t* ref_ptr = ref_frame.data();
    uint32_t sad = 0;

    for (size_t y = 0; y < image::kBlockSize; ++y) {
        const size_t curr_row = (static_cast<size_t>(block_y) + y) * width + block_x;
        const size_t ref_row = (static_cast<size_t>(ref_y) + y) * width + ref_x;

        for (size_t x = 0; x < image::kBlockSize; ++x) {
            const int32_t diff = static_cast<int32_t>(curr_ptr[curr_row + x]) - static_cast<int32_t>(ref_ptr[ref_row + x]);
            sad += static_cast<uint32_t>(std::abs(diff));
        }
    }

    return sad;
}

MotionVector MotionEstimator::find_best_motion_vector(
    ReadOnlyBuffer curr_frame,
    ReadOnlyBuffer ref_frame,
    uint16_t width,
    uint16_t height,
    uint16_t block_x,
    uint16_t block_y,
    int32_t search_window) noexcept
{
    uint32_t min_sad = 0xFFFFFFFFU;
    MotionVector best_mv{0, 0};

    const int32_t min_dx = std::max(-search_window, -static_cast<int32_t>(block_x));
    const int32_t max_dx = std::min(search_window, static_cast<int32_t>(width - block_x - image::kBlockSize));

    const int32_t min_dy = std::max(-search_window, -static_cast<int32_t>(block_y));
    const int32_t max_dy = std::min(search_window, static_cast<int32_t>(height - block_y - image::kBlockSize));

    for (int32_t dy = min_dy; dy <= max_dy; ++dy) {
        for (int32_t dx = min_dx; dx <= max_dx; ++dx) {
            const int32_t ref_x = static_cast<int32_t>(block_x) + dx;
            const int32_t ref_y = static_cast<int32_t>(block_y) + dy;

            const uint32_t sad = compute_sad(curr_frame, ref_frame, width, block_x, block_y, ref_x, ref_y);
            if (sad < min_sad) {
                min_sad = sad;
                best_mv = MotionVector{static_cast<int8_t>(dx), static_cast<int8_t>(dy)};
            }
        }
    }

    return best_mv;
}

void MotionEstimator::compensate_block(
    ReadOnlyBuffer ref_frame,
    uint16_t width,
    uint16_t block_x,
    uint16_t block_y,
    MotionVector mv,
    std::span<float, image::kBlockElements> pred_out) noexcept
{
    const uint8_t* ref_ptr = ref_frame.data();
    const size_t ref_x = static_cast<size_t>(static_cast<int32_t>(block_x) + mv.dx);
    const size_t ref_y = static_cast<size_t>(static_cast<int32_t>(block_y) + mv.dy);

    for (size_t y = 0; y < image::kBlockSize; ++y) {
        const size_t ref_row = (ref_y + y) * width + ref_x;
        for (size_t x = 0; x < image::kBlockSize; ++x) {
            pred_out[y * image::kBlockSize + x] = static_cast<float>(ref_ptr[ref_row + x]);
        }
    }
}

Result<size_t> VideoCodecEngine::encode_frame(
    VideoHeader header,
    FrameType type,
    ReadOnlyBuffer curr_frame,
    VideoRingBuffer& ref_buffer,
    core::BitStreamWriter& writer) noexcept
{
    if (curr_frame.data() == nullptr || curr_frame.size() < static_cast<size_t>(header.width) * header.height) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    if (header.width % image::kBlockSize != 0 || header.height % image::kBlockSize != 0 || header.width == 0 || header.height == 0) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    // Write frame header descriptor byte
    auto res_hdr = writer.write_bits(static_cast<uint64_t>(type), 8);
    if (!res_hdr) return std::unexpected(res_hdr.error());

    if (type == FrameType::IFrame || !ref_buffer.has_reference()) {
        const image::ImageHeader img_hdr{
            .width = header.width,
            .height = header.height,
            .format = image::PixelFormat::Grayscale,
            .quality_factor = header.quality_factor
        };

        const size_t start_bits = writer.bits_written();
        auto enc_res = m_image_pipeline.encode_grayscale_frame(img_hdr, curr_frame, writer);
        if (!enc_res) return std::unexpected(enc_res.error());

        auto ref_mut = ref_buffer.current_reference();
        std::copy_n(curr_frame.data(), curr_frame.size(), ref_mut.data());
        ref_buffer.set_reference_valid(true);

        return (writer.bits_written() - start_bits) / 8;
    }

    // Encode P-Frame (Motion Vectors + DCT Quantized Residuals)
    const image::DctQuantEngine dct_engine(header.quality_factor);
    core::ContextModel model(2);
    core::ArithmeticEncoder encoder(&writer);

    alignas(64) std::array<float, image::kBlockElements> pred_block{};
    alignas(64) std::array<float, image::kBlockElements> residual_block{};
    alignas(64) std::array<float, image::kBlockElements> dct_coeffs{};
    alignas(64) std::array<int16_t, image::kBlockElements> quant_coeffs{};
    alignas(64) std::array<int16_t, image::kBlockElements> zigzag_coeffs{};

    MotionVector prev_mv{0, 0};
    int16_t prev_dc = 0;

    const ReadOnlyBuffer ref_frame = ref_buffer.current_reference();
    const uint8_t* curr_ptr = curr_frame.data();

    const size_t start_bits = writer.bits_written();

    for (uint16_t by = 0; by < header.height; by += image::kBlockSize) {
        for (uint16_t bx = 0; bx < header.width; bx += image::kBlockSize) {
            // 1. Motion Vector Estimation
            const MotionVector mv = MotionEstimator::find_best_motion_vector(
                curr_frame, ref_frame, header.width, header.height, bx, by, header.search_window);

            const int8_t mv_dx_delta = static_cast<int8_t>(mv.dx - prev_mv.dx);
            const int8_t mv_dy_delta = static_cast<int8_t>(mv.dy - prev_mv.dy);
            prev_mv = mv;

            const uint16_t u_dx = map_int8_to_uint16(mv_dx_delta);
            const uint16_t u_dy = map_int8_to_uint16(mv_dy_delta);

            auto r_mv1 = encoder.encode_symbol(static_cast<uint8_t>(u_dx & 0xFF), model);
            if (!r_mv1) return std::unexpected(r_mv1.error());
            auto r_mv2 = encoder.encode_symbol(static_cast<uint8_t>(u_dy & 0xFF), model);
            if (!r_mv2) return std::unexpected(r_mv2.error());

            // 2. Motion Compensation & Residual Extraction
            MotionEstimator::compensate_block(ref_frame, header.width, bx, by, mv, pred_block);

            for (size_t y = 0; y < image::kBlockSize; ++y) {
                const size_t row_offset = (static_cast<size_t>(by) + y) * header.width + bx;
                for (size_t x = 0; x < image::kBlockSize; ++x) {
                    const float curr_val = static_cast<float>(curr_ptr[row_offset + x]);
                    residual_block[y * image::kBlockSize + x] = curr_val - pred_block[y * image::kBlockSize + x];
                }
            }

            // 3. DCT + Quantization + ZigZag Serialization
            auto res_dct = dct_engine.forward_dct(residual_block, dct_coeffs);
            if (!res_dct) return std::unexpected(res_dct.error());

            auto res_q = dct_engine.quantize(dct_coeffs, quant_coeffs);
            if (!res_q) return std::unexpected(res_q.error());

            auto res_zz = dct_engine.zigzag_serialize(quant_coeffs, zigzag_coeffs);
            if (!res_zz) return std::unexpected(res_zz.error());

            // 4. Encode DC Residual Delta using full 16-bit resolution
            const int16_t current_dc = zigzag_coeffs[0];
            const int16_t dc_delta = static_cast<int16_t>(current_dc - prev_dc);
            prev_dc = current_dc;

            const uint16_t u_dc = map_int16_to_uint16(dc_delta);
            auto r1 = encoder.encode_symbol(static_cast<uint8_t>((u_dc >> 8) & 0xFF), model);
            if (!r1) return std::unexpected(r1.error());
            auto r2 = encoder.encode_symbol(static_cast<uint8_t>(u_dc & 0xFF), model);
            if (!r2) return std::unexpected(r2.error());

            // 5. Encode AC Residual Stream using full 16-bit resolution
            size_t last_nonzero = 0;
            for (size_t i = image::kBlockElements - 1; i >= 1; --i) {
                if (zigzag_coeffs[i] != 0) {
                    last_nonzero = i;
                    break;
                }
            }

            if (last_nonzero > 0) {
                uint8_t zero_run = 0;
                for (size_t i = 1; i <= last_nonzero; ++i) {
                    const int16_t ac_val = zigzag_coeffs[i];
                    if (ac_val == 0) {
                        if (zero_run == 254) {
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

            auto reob = encoder.encode_symbol(0xFF, model);
            if (!reob) return std::unexpected(reob.error());
        }
    }

    auto res_flush = encoder.flush();
    if (!res_flush) return std::unexpected(res_flush.error());

    auto ref_mut = ref_buffer.current_reference();
    std::copy_n(curr_frame.data(), curr_frame.size(), ref_mut.data());
    ref_buffer.set_reference_valid(true);

    return (writer.bits_written() - start_bits) / 8;
}

Result<size_t> VideoCodecEngine::decode_frame(
    VideoHeader header,
    core::BitStreamReader& reader,
    VideoRingBuffer& ref_buffer,
    MutableBuffer output_frame) noexcept
{
    if (output_frame.data() == nullptr || output_frame.size() < static_cast<size_t>(header.width) * header.height) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    auto frame_type_bits = reader.read_bits(8);
    if (!frame_type_bits) return std::unexpected(frame_type_bits.error());

    const auto type = static_cast<FrameType>(*frame_type_bits);

    if (type == FrameType::IFrame || !ref_buffer.has_reference()) {
        const image::ImageHeader img_hdr{
            .width = header.width,
            .height = header.height,
            .format = image::PixelFormat::Grayscale,
            .quality_factor = header.quality_factor
        };

        auto dec_res = m_image_pipeline.decode_grayscale_frame(img_hdr, reader, output_frame);
        if (!dec_res) return std::unexpected(dec_res.error());

        auto ref_mut = ref_buffer.current_reference();
        std::copy_n(output_frame.data(), output_frame.size(), ref_mut.data());
        ref_buffer.set_reference_valid(true);

        return output_frame.size();
    }

    // Decode P-Frame
    const image::DctQuantEngine dct_engine(header.quality_factor);
    core::ContextModel model(2);
    core::ArithmeticDecoder decoder;

    auto res_init = decoder.set_reader(&reader);
    if (!res_init) return std::unexpected(res_init.error());

    alignas(64) std::array<int16_t, image::kBlockElements> zigzag_coeffs{};
    alignas(64) std::array<int16_t, image::kBlockElements> quant_coeffs{};
    alignas(64) std::array<float, image::kBlockElements> dequant_coeffs{};
    alignas(64) std::array<float, image::kBlockElements> residual_reconstructed{};
    alignas(64) std::array<float, image::kBlockElements> pred_block{};

    MotionVector prev_mv{0, 0};
    int16_t prev_dc = 0;

    const ReadOnlyBuffer ref_frame = ref_buffer.current_reference();
    uint8_t* out_ptr = output_frame.data();

    for (uint16_t by = 0; by < header.height; by += image::kBlockSize) {
        for (uint16_t bx = 0; bx < header.width; bx += image::kBlockSize) {
            zigzag_coeffs.fill(0);

            // 1. Decode Motion Vector Delta
            auto dx_sym = decoder.decode_symbol(model);
            if (!dx_sym) return std::unexpected(dx_sym.error());
            auto dy_sym = decoder.decode_symbol(model);
            if (!dy_sym) return std::unexpected(dy_sym.error());

            const int8_t dx_delta = unmap_uint16_to_int8(*dx_sym);
            const int8_t dy_delta = unmap_uint16_to_int8(*dy_sym);

            const MotionVector mv{
                static_cast<int8_t>(prev_mv.dx + dx_delta),
                static_cast<int8_t>(prev_mv.dy + dy_delta)
            };
            prev_mv = mv;

            // 2. Decode DC Residual Delta across full 16-bit range
            auto dc_hi = decoder.decode_symbol(model);
            if (!dc_hi) return std::unexpected(dc_hi.error());
            auto dc_lo = decoder.decode_symbol(model);
            if (!dc_lo) return std::unexpected(dc_lo.error());

            const uint16_t u_dc = static_cast<uint16_t>((static_cast<uint16_t>(*dc_hi) << 8) | static_cast<uint16_t>(*dc_lo));
            const int16_t dc_delta = unmap_uint16_to_int16(u_dc);
            const int16_t current_dc = static_cast<int16_t>(prev_dc + dc_delta);
            zigzag_coeffs[0] = current_dc;
            prev_dc = current_dc;

            // 3. Decode AC Residual Stream across full 16-bit range
            size_t ac_idx = 1;
            while (ac_idx < image::kBlockElements) {
                auto run_sym = decoder.decode_symbol(model);
                if (!run_sym) return std::unexpected(run_sym.error());

                const uint8_t run = *run_sym;
                if (run == 0xFF) break;

                ac_idx += run;
                if (ac_idx >= image::kBlockElements) break;

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

            // 4. Residual Inverse Transformation
            auto res_des = dct_engine.zigzag_deserialize(zigzag_coeffs, quant_coeffs);
            if (!res_des) return std::unexpected(res_des.error());

            auto res_dq = dct_engine.dequantize(quant_coeffs, dequant_coeffs);
            if (!res_dq) return std::unexpected(res_dq.error());

            auto res_idct = dct_engine.inverse_dct(dequant_coeffs, residual_reconstructed);
            if (!res_idct) return std::unexpected(res_idct.error());

            // 5. Synthesize Motion Predicted Block with Residual
            MotionEstimator::compensate_block(ref_frame, header.width, bx, by, mv, pred_block);

            for (size_t y = 0; y < image::kBlockSize; ++y) {
                const size_t row_offset = (static_cast<size_t>(by) + y) * header.width + bx;
                for (size_t x = 0; x < image::kBlockSize; ++x) {
                    const float final_val = pred_block[y * image::kBlockSize + x] + residual_reconstructed[y * image::kBlockSize + x];
                    out_ptr[row_offset + x] = static_cast<uint8_t>(std::clamp(std::round(final_val), 0.0f, 255.0f));
                }
            }
        }
    }

    auto ref_mut = ref_buffer.current_reference();
    std::copy_n(output_frame.data(), output_frame.size(), ref_mut.data());
    ref_buffer.set_reference_valid(true);

    return output_frame.size();
}

} // namespace khcomp::video