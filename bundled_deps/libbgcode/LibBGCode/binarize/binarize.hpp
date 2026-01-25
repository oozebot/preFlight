#ifndef BGCODE_BINARIZE_HPP
#define BGCODE_BINARIZE_HPP

#include "LibBGCode/binarize/export.h"
#include "LibBGCode/core/core.hpp"

namespace bgcode { namespace binarize {

struct BGCODE_BINARIZE_EXPORT BaseMetadataBlock
{
    // type of data encoding
    uint16_t encoding_type{ 0 };
    // data in key/value form
    std::vector<std::pair<std::string, std::string>> raw_data;

    // read block data in encoded format
    core::EResult read_data(FILE& file, const core::BlockHeader& block_header);
};

struct BGCODE_BINARIZE_EXPORT FileMetadataBlock : public BaseMetadataBlock
{
    // write block header and data
    core::EResult write(FILE& file, core::ECompressionType compression_type, core::EChecksumType checksum_type) const;
    // read block data
    core::EResult read_data(FILE& file, const core::FileHeader& file_header, const core::BlockHeader& block_header);
};

struct BGCODE_BINARIZE_EXPORT PrintMetadataBlock : public BaseMetadataBlock
{
    // write block header and data
    core::EResult write(FILE& file, core::ECompressionType compression_type, core::EChecksumType checksum_type) const;
    // read block data
    core::EResult read_data(FILE& file, const core::FileHeader& file_header, const core::BlockHeader& block_header);
};

struct BGCODE_BINARIZE_EXPORT PrinterMetadataBlock : public BaseMetadataBlock
{
    // write block header and data
    core::EResult write(FILE& file, core::ECompressionType compression_type, core::EChecksumType checksum_type) const;
    // read block data
    core::EResult read_data(FILE& file, const core::FileHeader& file_header, const core::BlockHeader& block_header);
};

struct BGCODE_BINARIZE_EXPORT ThumbnailBlock
{
    core::ThumbnailParams params;
    std::vector<std::byte> data;

    // write block header and data
    core::EResult write(FILE& file, core::EChecksumType checksum_type);
    // read block data
    core::EResult read_data(FILE& file, const core::FileHeader& file_header, const core::BlockHeader& block_header);
};

struct BGCODE_BINARIZE_EXPORT GCodeBlock
{
    uint16_t encoding_type{ 0 };
    std::string raw_data;

    // write block header and data
    core::EResult write(FILE& file, core::ECompressionType compression_type, core::EChecksumType checksum_type) const;
    // read block data
    core::EResult read_data(FILE& file, const core::FileHeader& file_header, const core::BlockHeader& block_header);
};

struct BGCODE_BINARIZE_EXPORT SlicerMetadataBlock : public BaseMetadataBlock
{
    // write block header and data
    core::EResult write(FILE& file, core::ECompressionType compression_type, core::EChecksumType checksum_type) const;
    // read block data
    core::EResult read_data(FILE& file, const core::FileHeader& file_header, const core::BlockHeader& block_header);
};

struct BinarizerConfig
{
    struct Compression
    {
        core::ECompressionType file_metadata{ core::ECompressionType::None };
        core::ECompressionType printer_metadata{ core::ECompressionType::None };
        core::ECompressionType print_metadata{ core::ECompressionType::None };
        core::ECompressionType slicer_metadata{ core::ECompressionType::None };
        core::ECompressionType gcode{ core::ECompressionType::None };
    };
    Compression compression;
    core::EGCodeEncodingType gcode_encoding{ core::EGCodeEncodingType::None };
    core::EMetadataEncodingType metadata_encoding{ core::EMetadataEncodingType::INI };
    core::EChecksumType checksum{ core::EChecksumType::CRC32 };
};

struct BGCODE_BINARIZE_EXPORT BinaryData
{
    FileMetadataBlock file_metadata;
    PrinterMetadataBlock printer_metadata;
    std::vector<ThumbnailBlock> thumbnails;
    SlicerMetadataBlock slicer_metadata;
    PrintMetadataBlock print_metadata;
};

class BGCODE_BINARIZE_EXPORT Binarizer
{
public:
    bool is_enabled() const;
    void set_enabled(bool enable);

    BinaryData& get_binary_data();
    const BinaryData& get_binary_data() const;

    size_t get_max_gcode_cache_size() const;
    void set_max_gcode_cache_size(size_t size);

    core::EResult initialize(FILE& file, const BinarizerConfig& config);
    core::EResult append_gcode(const std::string& gcode);
    core::EResult finalize();

private:
    FILE* m_file{ nullptr };
    bool m_enabled{ false };
    BinarizerConfig m_config;
    BinaryData m_binary_data;
    std::string m_gcode_cache;
    size_t m_gcode_cache_size{ 65536 };
};

} // namespace binarize
} // namespace bgcode

#endif // BGCODE_BINARIZE_HPP_
