#include <cassert>
#include <cstring>
#include <umbf/ext/image/image.hpp>

namespace
{
    void check_target(const acul::shared_ptr<umbf::Block> &block, const umbf::Target &expected)
    {
        assert(block && block->signature() == umbf::sign_block::target);
        const auto target = acul::static_pointer_cast<umbf::Target>(block);
        assert(target->url == expected.url && target->tag == expected.tag);
        assert(target->header.vendor_sign == expected.header.vendor_sign);
        assert(target->header.vendor_version == expected.header.vendor_version);
        assert(target->header.spec_version == expected.header.spec_version);
        assert(target->header.type_sign == expected.header.type_sign);
        assert(target->checksum == expected.checksum);
    }
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 2);
    umbf::registry::HashResolver resolver;
    umbf::insert_default_block_streams(resolver);
    umbf::insert_default_segment_codecs(resolver);
    umbf::insert_image_streams(resolver);
    umbf::insert_image_codecs(resolver);
    umbf::registry::resolver = &resolver;

    umbf::Target origin;
    constexpr u32 origin_tag = 0xabcdef01u;
    origin.tag = origin_tag;
    origin.url = "assets://scene_presets/car/decal.umia";
    origin.header.vendor_sign = UMBF_VENDOR_ID;
    origin.header.vendor_version = 17u;
    origin.header.spec_version = 3u;
    origin.header.type_sign = umbf::sign_block::format::image;
    origin.checksum = 0x87654321u;
    umbf::Target cached = origin;
    constexpr u32 cached_tag = 0x12345678u;
    cached.tag = cached_tag;
    cached.url = "data/cache/images/decal.umia";
    constexpr u32 custom_tag = 0xf1234567u;
    constexpr u32 unknown_signature = 0x11223344u;
    unsigned char pixels[3u * 4u * 4u];
    for (size_t i = 0; i < sizeof(pixels); ++i) pixels[i] = static_cast<unsigned char>(i * 17u);
    umbf::Image2D image;
    image.width = 3u;
    image.height = 4u;
    image.channels = {"R", "G", "B", "A"};
    image.format = {umbf::ImageFormat::Type::uint, 1u};
    image.pixels = pixels;
    image.tag = custom_tag;

    for (const u32 codec : {UMBF_SEGMENT_RAW, UMBF_SEGMENT_COMPRESSED, UMBF_SEGMENT_COMPRESSED_IMAGE})
    {
        umbf::WriteDescriptor writer;
        umbf::Header header;
        header.type_sign = umbf::sign_block::format::image;
        assert(umbf::create_write_descriptor(argv[1], header, writer).success());
        auto *segment = umbf::add_segment(writer, codec);
        umbf::add_block(writer, &cached, segment);
        umbf::add_block(writer, &image, segment);
        umbf::add_raw_block(writer, pixels, sizeof(pixels), segment, custom_tag);
        umbf::add_block(writer, &origin, segment);
        umbf::Target untagged = origin;
        untagged.tag = 0u;
        umbf::add_block(writer, &untagged, segment);
        assert(umbf::finalize_file(writer));
        umbf::close_write_descriptor(writer);

        umbf::ReadDescriptor reader;
        assert(umbf::create_read_descriptor(argv[1], reader).success());
        auto it = reader.begin();
        assert(it != reader.end());
        assert(it->tag == cached_tag);
        check_target(umbf::get_block(it), cached);
        ++it;
        assert(it != reader.end() && it->signature == image.signature() && it->tag == custom_tag);
        auto restored_block = umbf::get_block(it);
        assert(restored_block && restored_block->signature() == image.signature());
        const auto restored = acul::static_pointer_cast<umbf::Image2D>(restored_block);
        assert(restored->tag == custom_tag);
        assert(restored->size() == sizeof(pixels));
        assert(std::memcmp(restored->pixels, pixels, sizeof(pixels)) == 0);
        acul::release(static_cast<char *>(restored->pixels));
        restored->pixels = nullptr;
        ++it;
        assert(it != reader.end() && it->signature == umbf::sign_block::raw && it->tag == custom_tag);
        assert(it->size == sizeof(pixels) && std::memcmp(it->data, pixels, sizeof(pixels)) == 0);
        assert(!umbf::get_block(it)); // Unknown types must not shift or hide subsequent blocks.
        ++it;
        assert(it != reader.end());
        assert(it->tag == origin_tag);
        check_target(umbf::get_block(it), origin);
        ++it;
        assert(it != reader.end() && it->tag == 0u);
        check_target(umbf::get_block(it), untagged);
        ++it;
        assert(it == reader.end() && !it.failed());
    }

    // Optional block streams preserve tags and skip unknown blocks without losing alignment.
    acul::bin_stream stream;
    assert(umbf::write_block_to_stream(stream, cached));
    stream.write(u64{9u}).write(unknown_signature).write(custom_tag).write(u8{42u});
    stream.write(u64{0u});
    origin.tag = custom_tag;
    assert(umbf::write_block_to_stream(stream, origin));
    umbf::Target untagged = origin;
    untagged.tag = 0u;
    assert(umbf::write_block_to_stream(stream, untagged));
    stream.pos(0u);
    acul::shared_ptr<umbf::Block> restored;
    assert(umbf::read_block_from_stream(stream, restored));
    check_target(restored, cached);
    // Unknown and empty records produce no Block, so there is no tag to inspect.
    assert(umbf::read_block_from_stream(stream, restored) && !restored);
    assert(umbf::read_block_from_stream(stream, restored) && !restored);
    assert(umbf::read_block_from_stream(stream, restored));
    check_target(restored, origin);
    assert(umbf::read_block_from_stream(stream, restored));
    check_target(restored, untagged);
    assert(stream.pos() == stream.size());
    umbf::registry::resolver = nullptr;
}
