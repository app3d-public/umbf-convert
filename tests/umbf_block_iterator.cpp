#include <acul/hash/utils.hpp>
#include <cassert>
#include <type_traits>
#include <umbf/umbf.hpp>

static_assert(!std::is_copy_constructible_v<umbf::WriteDescriptor>);
static_assert(std::is_nothrow_move_constructible_v<umbf::WriteDescriptor>);
static_assert(!std::is_constructible_v<umbf::BlockIterator, const umbf::WriteDescriptor &>);
static_assert(std::is_constructible_v<umbf::BlockIterator, const umbf::ReadDescriptor &>);
static_assert(!std::is_polymorphic_v<umbf::FileDescriptorBase>);

namespace
{
    constexpr u32 value_signature = 0x31545354u;

    struct ValueBlock final : umbf::Block
    {
        u32 value = 0u;

        u32 signature() const noexcept override { return value_signature; }
    };

    void *read_value(acul::bin_stream &stream)
    {
        auto *value = acul::alloc<ValueBlock>();
        stream.read(value->value);
        return value;
    }

    void write_value(acul::bin_stream &stream, void *value) { stream.write(static_cast<ValueBlock *>(value)->value); }

    const umbf::registry::BlockStream value_stream{read_value, write_value};
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 2);

    umbf::registry::HashResolver resolver;
    umbf::insert_default_block_streams(resolver);
    resolver.block_streams[value_signature] = &value_stream;
    umbf::registry::resolver = &resolver;

    umbf::WriteDescriptor builder;
    umbf::Header header;
    header.vendor_sign = UMBF_VENDOR_ID;
    header.type_sign = umbf::sign_block::format::raw;
    assert(umbf::create_write_descriptor(argv[1], header, builder).success());
    assert(std::fflush(builder.fd) == 0);
    acul::vector<char> initial_header;
    assert(acul::fs::read_binary(argv[1], initial_header));
    assert(initial_header.size() == sizeof(u32) * 5u);

    ValueBlock values[3];
    values[0].value = 11u;
    values[1].value = 22u;
    values[2].value = 33u;
    umbf::add_block(builder, &values[0]);
    auto *group = umbf::add_segment(builder);
    umbf::add_block(builder, &values[1], group);
    umbf::add_block(builder, &values[2], group);
    FILE *original_fd = builder.fd;
    umbf::WriteDescriptor moved(std::move(builder));
    assert(!builder.fd && !builder.file && moved.fd == original_fd);
    // Move assignment must close its old resource before taking over the active writer.
    assert(umbf::create_write_descriptor(header, builder).success());
    builder = std::move(moved);
    assert(!moved.fd && !moved.file && builder.fd == original_fd);
    assert(umbf::finalize_file(builder));
    const auto final_position = acul::fs::ftell(builder.fd);
    assert(umbf::finalize_file(builder));
    assert(acul::fs::ftell(builder.fd) == final_position);
    const auto checksum = builder.file->checksum;
    umbf::close_map_descriptor(builder);

    umbf::ReadDescriptor descriptor;
    assert(umbf::create_read_descriptor(argv[1], descriptor).success());
    assert(descriptor.file->sg_table.size() == 2u);
    assert(descriptor.file->sg_table[0].block_count == 1u);
    assert(descriptor.file->sg_table[1].block_count == 2u);
    assert(descriptor.file->sg_table[0].offset == 0u);
    assert(descriptor.file->sg_table[1].offset == descriptor.file->sg_table[0].size);

    FILE *source = std::fopen(argv[1], "rb");
    assert(source);
    assert(std::fseek(source, 0, SEEK_END) == 0);
    const auto source_size = std::ftell(source);
    assert(source_size > 0);
    constexpr size_t top_level_header_size = sizeof(u32) * 5u;
    constexpr size_t footer_size = sizeof(u64) + sizeof(u32);
    constexpr size_t segment_descriptor_size = sizeof(u64) * 2u + sizeof(u32) * 2u;
    const size_t table_size = descriptor.file->sg_table.size() * segment_descriptor_size;
    const size_t payload_size = static_cast<size_t>(source_size) - top_level_header_size - table_size - footer_size;
    acul::vector<char> final_header(top_level_header_size);
    assert(std::fseek(source, 0, SEEK_SET) == 0);
    assert(std::fread(final_header.data(), 1u, final_header.size(), source) == final_header.size());
    assert(final_header == initial_header);
    char footer[footer_size];
    assert(std::fseek(source, -static_cast<long>(footer_size), SEEK_END) == 0);
    assert(std::fread(footer, 1u, footer_size, source) == footer_size);
    acul::bin_stream footer_stream(footer, footer_size);
    u64 stored_size = 0u;
    u32 stored_checksum = 0u;
    footer_stream.read(stored_size).read(stored_checksum);
    assert(stored_size == payload_size && stored_checksum == checksum);
    acul::vector<char> payload(payload_size);
    assert(std::fseek(source, static_cast<long>(top_level_header_size), SEEK_SET) == 0);
    assert(std::fread(payload.data(), 1u, payload.size(), source) == payload.size());
    std::fclose(source);
    assert(acul::crc32(0u, payload.data(), payload.size()) == checksum);
    assert(descriptor.file->checksum == checksum);

    umbf::BlockIterator block(descriptor);
    auto first = block++;
    assert(first != umbf::BlockIterator{});
    assert(first->signature == value_signature);
    auto first_base = first.read();
    assert(first_base && first_base->signature() == value_signature);
    auto first_value = acul::static_pointer_cast<ValueBlock>(first_base);
    assert(first_value && first_value->value == values[0].value);

    for (size_t index = 1u; index < 3u; ++index)
    {
        assert(block != umbf::BlockIterator{});
        assert(block->signature == value_signature);
        auto value_base = umbf::get_block(block);
        assert(value_base && value_base->signature() == value_signature);
        auto value = acul::static_pointer_cast<ValueBlock>(value_base);
        assert(value && value->value == values[index].value);
        ++block;
    }
    assert(block == umbf::BlockIterator{});

    umbf::close_map_descriptor(descriptor);
    return 0;
}
