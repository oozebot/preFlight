///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2022 Lukáš Hejl @hejllukas, Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
///|/ Provisional module: thumbnails belong with gcode by subject, and sit in config because the option catalogue includes this header.
///|/
#pragma once

#include <LibBGCode/binarize/binarize.hpp>
#include <boost/beast/core/detail/base64.hpp>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <boost/beast/core.hpp>
#include <boost/format.hpp>
#include <LibBGCode/core/core.hpp>
#include <vector>
#include <memory>
#include <string_view>
#include <algorithm>
#include <string>
#include <utility>

#include "luminary/geometry/primitives/Point.hpp"
#include "luminary/config/catalog/PrintConfig.hpp"
#include "ThumbnailData.hpp"
#include "luminary/core/enum_bitmask.hpp"

namespace Luminary
{
class ConfigBase;

enum class ThumbnailError : int
{
    InvalidVal,
    OutOfRange,
    InvalidExt
};
using ThumbnailErrors = enum_bitmask<ThumbnailError>;
ENABLE_ENUM_BITMASK_OPERATORS(ThumbnailError);
} // namespace Luminary

namespace Luminary::GCodeThumbnails
{

struct CompressedImageBuffer
{
    void *data{nullptr};
    size_t size{0};
    virtual ~CompressedImageBuffer() {}
    virtual std::string_view tag() const = 0;
};

std::unique_ptr<CompressedImageBuffer> compress_thumbnail(const ThumbnailData &data, GCodeThumbnailsFormat format);

// Helpers used by the BTT_TFT size header (defined in Thumbnails.cpp).
std::string get_hex(const unsigned int input);
std::string rjust(std::string input, unsigned int width, char fill_char);

typedef std::vector<std::pair<GCodeThumbnailsFormat, Vec2d>> GCodeThumbnailDefinitionsList;

using namespace std::literals;
std::pair<GCodeThumbnailDefinitionsList, ThumbnailErrors> make_and_check_thumbnail_list(
    const std::string &thumbnails_string, const std::string_view def_ext = "PNG"sv);
std::pair<GCodeThumbnailDefinitionsList, ThumbnailErrors> make_and_check_thumbnail_list(const ConfigBase &config);

std::string get_error_string(const ThumbnailErrors &errors);

template<typename WriteToOutput, typename ThrowIfCanceledCallback>
inline void export_thumbnails_to_file(ThumbnailsGeneratorCallback &thumbnail_cb,
                                      const std::vector<std::pair<GCodeThumbnailsFormat, Vec2d>> &thumbnails_list,
                                      WriteToOutput output, ThrowIfCanceledCallback throw_if_canceled)
{
    // Write thumbnails using base64 encoding
    if (thumbnail_cb != nullptr)
    {
        // Neptune-series firmware expects the first COLPIC preview under ;gimage: and the rest under ;simage:.
        bool first_colpic = true;
        size_t entry_index = 0;
        for (const auto &[format, size] : thumbnails_list)
        {
            static constexpr const size_t max_row_length = 78;
            const bool last_entry = (entry_index + 1 == thumbnails_list.size());
            ThumbnailsList thumbnails = thumbnail_cb(ThumbnailsParams{{size}, true, true, true, true});
            for (const ThumbnailData &data : thumbnails)
                if (data.is_valid())
                {
                    auto compressed = compress_thumbnail(data, format);
                    if (compressed->data && compressed->size)
                    {
                        if (format == GCodeThumbnailsFormat::COLPIC)
                        {
                            // COLPIC payload is already a self-terminated ASCII string, not base64.
                            const char *colpic = reinterpret_cast<const char *>(compressed->data);
                            output((boost::format("\n\n;%s:%s\n\n") % (first_colpic ? "gimage" : "simage") % colpic)
                                       .str()
                                       .c_str());
                            first_colpic = false;
                        }
                        else if (format == GCodeThumbnailsFormat::BTT_TFT)
                        {
                            // BTT_TFT: ";WWWWHHHH" size header, then the RGB565 hex-text body, then a single
                            // end marker once the last list entry has been written.
                            output((";" + rjust(get_hex(data.width), 4, '0') + rjust(get_hex(data.height), 4, '0') +
                                    "\r\n")
                                       .c_str());
                            output(reinterpret_cast<const char *>(compressed->data));
                            if (last_entry)
                                output("; bigtree thumbnail end\r\n\r\n");
                        }
                        else
                        {
                            std::string encoded;
                            encoded.resize(boost::beast::detail::base64::encoded_size(compressed->size));
                            encoded.resize(boost::beast::detail::base64::encode((void *) encoded.data(),
                                                                                (const void *) compressed->data,
                                                                                compressed->size));

                            output((boost::format("\n;\n; %s begin %dx%d %d\n") % compressed->tag() % data.width %
                                    data.height % encoded.size())
                                       .str()
                                       .c_str());

                            while (encoded.size() > max_row_length)
                            {
                                output((boost::format("; %s\n") % encoded.substr(0, max_row_length)).str().c_str());
                                encoded = encoded.substr(max_row_length);
                            }

                            if (encoded.size() > 0)
                                output((boost::format("; %s\n") % encoded).str().c_str());

                            output((boost::format("; %s end\n;\n") % compressed->tag()).str().c_str());
                        }
                    }
                    throw_if_canceled();
                }
            ++entry_index;
        }
    }
}

template<typename ThrowIfCanceledCallback>
inline void generate_binary_thumbnails(ThumbnailsGeneratorCallback &thumbnail_cb,
                                       std::vector<bgcode::binarize::ThumbnailBlock> &out_thumbnails,
                                       const std::vector<std::pair<GCodeThumbnailsFormat, Vec2d>> &thumbnails_list,
                                       ThrowIfCanceledCallback throw_if_canceled)
{
    using namespace bgcode::core;
    using namespace bgcode::binarize;
    out_thumbnails.clear();
    assert(thumbnail_cb != nullptr);
    if (thumbnail_cb != nullptr)
    {
        for (const auto &[format, size] : thumbnails_list)
        {
            // The binary G-code thumbnail container only encodes PNG/JPG/QOI. BTT_TFT and COLPIC are ASCII
            // previews for screens whose firmware uses ASCII G-code, so they have no place in the binary block.
            if (format == GCodeThumbnailsFormat::BTT_TFT || format == GCodeThumbnailsFormat::COLPIC)
                continue;
            ThumbnailsList thumbnails = thumbnail_cb(ThumbnailsParams{{size}, true, true, true, true});
            for (const ThumbnailData &data : thumbnails)
                if (data.is_valid())
                {
                    auto compressed = compress_thumbnail(data, format);
                    if (compressed->data != nullptr && compressed->size > 0)
                    {
                        ThumbnailBlock &block = out_thumbnails.emplace_back(ThumbnailBlock());
                        block.params.width = (uint16_t) data.width;
                        block.params.height = (uint16_t) data.height;
                        switch (format)
                        {
                        case GCodeThumbnailsFormat::PNG:
                        {
                            block.params.format = (uint16_t) EThumbnailFormat::PNG;
                            break;
                        }
                        case GCodeThumbnailsFormat::JPG:
                        {
                            block.params.format = (uint16_t) EThumbnailFormat::JPG;
                            break;
                        }
                        case GCodeThumbnailsFormat::QOI:
                        {
                            block.params.format = (uint16_t) EThumbnailFormat::QOI;
                            break;
                        }
                        default:
                            break; // BTT_TFT and COLPIC are filtered out above; nothing else reaches here.
                        }
                        block.data.resize(compressed->size);
                        memcpy(block.data.data(), compressed->data, compressed->size);
                    }
                }
        }
    }
}

} // namespace Luminary::GCodeThumbnails
