#include <acul/io/fs/file.hpp>
#include <cassert>
#include <cstring>
#include <umbf/umbf.hpp>

namespace
{
    constexpr u32 probe_signature = 0x50414d54u;
    const char *expected_source = nullptr;
    u64 expected_size = 0u;
    u32 decode_count = 0u;

    acul::op_result encode_probe(const umbf::SegmentBuffer &data, u32, int, acul::vector<char> &dst)
    {
        dst = data;
        return acul::make_op_success();
    }

    acul::op_result decode_probe(const char *data, u64 size, u32, acul::vector<char> &dst)
    {
        // Verify that the codec receives the actual mapped address, not a staging copy.
        assert(data == expected_source && size == expected_size);
        ++decode_count;
        dst.assign(data, data + static_cast<size_t>(size));
        return acul::make_op_success();
    }

    const umbf::registry::SegmentCodec probe_codec{encode_probe, decode_probe};

    void verify_checksum(const umbf::ReadDescriptor &file)
    {
        u32 checksum = 0u;
        for (u32 i = 0u; i < file.file->sg_table.size(); ++i)
        {
            acul::vector<char> decoded;
            assert(umbf::read_segment(file, i, decoded).success());
            checksum = acul::crc32(checksum, decoded.data(), decoded.size());
        }
        assert(checksum == file.file->checksum);
    }
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 2);
    umbf::registry::HashResolver resolver;
    umbf::insert_default_segment_codecs(resolver);
    resolver.segment_codecs[probe_signature] = &probe_codec;
    umbf::registry::resolver = &resolver;

    umbf::Header header;
    header.type_sign = umbf::sign_block::format::raw;
    const char first[] = "first";
    const char second[] = "second";
    umbf::WriteDescriptor nested_writer;
    assert(umbf::create_write_descriptor(header, nested_writer).success());
    auto *compressed = umbf::add_segment(nested_writer, UMBF_SEGMENT_COMPRESSED);
    umbf::add_raw_block(nested_writer, first, sizeof(first), compressed);
    umbf::add_raw_block(nested_writer, second, sizeof(second), compressed);
    umbf::WriteDescriptor moved_writer(std::move(nested_writer));
    assert(!nested_writer.fd && moved_writer.active_segment.block_count == 2u);
    assert(!moved_writer.active_segment_data.empty());
    nested_writer = std::move(moved_writer);
    assert(!moved_writer.fd && nested_writer.active_segment.block_count == 2u);
    umbf::add_raw_block(nested_writer, nullptr, 0u);

    umbf::WriteDescriptor writer;
    assert(umbf::create_write_descriptor(argv[1], header, writer).success());
    auto *probe = umbf::add_segment(writer, probe_signature);
    umbf::add_raw_block(writer, first, sizeof(first), probe);
    umbf::add_file(writer, nested_writer);
    assert(umbf::finalize_file(writer));
    umbf::close_write_descriptor(nested_writer);
    umbf::close_write_descriptor(writer);

    umbf::ReadDescriptor parent;
    assert(umbf::create_read_descriptor(argv[1], parent).success());
    assert(parent.mapping && parent.mapping->data());
    const auto &segment = parent.file->sg_table.front();
    expected_source = parent.mapping->data() + parent.payload_offset + segment.offset;
    expected_size = segment.size;
    verify_checksum(parent);
    assert(decode_count == 1u);
    auto entry = parent.begin();
    assert(decode_count == 2u);
    assert(entry->size == sizeof(first) && std::memcmp(entry->data, first, sizeof(first)) == 0);
    ++entry;
    assert(decode_count == 2u);
    assert(entry->signature == header.type_sign);
    // A nested file is not a Block instance and therefore has no block tag.
    assert(entry->tag == 0u);
    umbf::ReadDescriptor child;
    assert(umbf::create_read_descriptor(parent, entry, child).success());
    assert(child.mapping.get() == parent.mapping.get());
    entry = {};
    umbf::close_read_descriptor(parent);

    // Child metadata and bytes remain valid after closing the parent descriptor.
    verify_checksum(child);
    auto block = child.begin();
    assert(block->size == sizeof(first) && std::memcmp(block->data, first, sizeof(first)) == 0);
    ++block;
    assert(block->size == sizeof(second) && std::memcmp(block->data, second, sizeof(second)) == 0);
    ++block;
    assert(block->size == 0u);
    ++block;
    assert(block == child.end() && !block.failed());
    umbf::close_read_descriptor(child);

    // Reject invalid table locations/descriptors and truncated input without accessing unmapped bytes.
    acul::vector<char> bytes;
    assert(acul::fs::read_binary(argv[1], bytes));
    const acul::string bad_path = acul::string(argv[1]) + ".invalid";
    const auto original = bytes;
    constexpr size_t footer_size = sizeof(u64) + sizeof(u32);
    const size_t payload_size_field = bytes.size() - footer_size;
    constexpr size_t fixed_header_size = sizeof(u32) * 5u;
    acul::bin_stream offset_stream(bytes.data() + payload_size_field, sizeof(u64));
    u64 table_offset = 0u;
    offset_stream.read(table_offset);
    assert(fixed_header_size + table_offset + sizeof(u32) <= bytes.size());
    std::memset(bytes.data() + payload_size_field, 0xff, sizeof(u64));
    assert(acul::fs::write_binary(bad_path, bytes.data(), bytes.size()));
    assert(!umbf::create_read_descriptor(bad_path, parent).success());
    assert(!parent.mapping && !parent.file);
    bytes = original;
    std::memset(bytes.data() + fixed_header_size + table_offset, 0xff, sizeof(u32));
    assert(acul::fs::write_binary(bad_path, bytes.data(), bytes.size()));
    assert(!umbf::create_read_descriptor(bad_path, parent).success());
    assert(!parent.mapping && !parent.file);
    // Moving the table boundary by one byte leaves an incomplete descriptor.
    bytes = original;
    assert(table_offset > 0u);
    acul::bin_stream malformed_size;
    malformed_size.write(table_offset - 1u);
    std::memcpy(bytes.data() + payload_size_field, malformed_size.data(), sizeof(u64));
    assert(acul::fs::write_binary(bad_path, bytes.data(), bytes.size()));
    assert(!umbf::create_read_descriptor(bad_path, parent).success());
    assert(!parent.mapping && !parent.file);
    // Missing footer and shifted/truncated table must also fail bounded parsing.
    assert(acul::fs::write_binary(bad_path, original.data(), original.size() - footer_size));
    assert(!umbf::create_read_descriptor(bad_path, parent).success());
    assert(acul::fs::write_binary(bad_path, original.data(), original.size() - 1u));
    assert(!umbf::create_read_descriptor(bad_path, parent).success());
    assert(acul::fs::write_binary(bad_path, bytes.data(), 3u));
    assert(!umbf::create_read_descriptor(bad_path, parent).success());
    assert(acul::fs::remove_file(bad_path.c_str()).success());

    // Empty files consist only of the header and footer; no table count is stored.
    assert(umbf::create_write_descriptor(bad_path, header, writer).success());
    assert(umbf::finalize_file(writer));
    umbf::close_write_descriptor(writer);
    assert(umbf::create_read_descriptor(bad_path, parent).success());
    assert(parent.file->sg_table.empty() && parent.file->checksum == 0u);
    assert(parent.begin() == parent.end());
    assert(parent.file_size == fixed_header_size + footer_size);
    umbf::close_read_descriptor(parent);
    assert(acul::fs::remove_file(bad_path.c_str()).success());
    umbf::registry::resolver = nullptr;
}
