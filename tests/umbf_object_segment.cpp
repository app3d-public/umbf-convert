#include <umbf/ext/scene/scene.hpp>
#include <cassert>
#include <cstring>

int main(int argc, char **argv)
{
    assert(argc == 3);
    umbf::registry::HashResolver resolver;
    umbf::insert_default_segment_codecs(resolver);
    umbf::insert_scene_streams(resolver);
    umbf::registry::resolver = &resolver;
    const u32 codec = std::strcmp(argv[2], "zstd") == 0 ? UMBF_SEGMENT_COMPRESSED : UMBF_SEGMENT_RAW;

    umbf::ReadDescriptor scene;
    assert(umbf::create_read_descriptor(argv[1], scene).success());

    acul::unique_ptr<umbf::ObjectList> objects;
    for (auto entry = scene.begin(); entry != scene.end(); ++entry)
    {
        if (entry->signature != umbf::sign_block::scene_objects_list) continue;
        auto block = umbf::get_block(entry);
        assert(block && block->signature() == umbf::sign_block::scene_objects_list);
        objects = acul::unique_ptr<umbf::ObjectList>(static_cast<umbf::ObjectList *>(block.release()));
        break;
    }
    assert(objects && !objects->objects.empty());

    size_t object_count = 0u;
    for (const auto &range : objects->objects)
    {
        assert(range.block_count >= 2u);

        u64 first = 0u;
        const umbf::Segment *object_segment = nullptr;
        for (const auto &segment : scene.file->sg_table)
        {
            const u64 end = first + segment.block_count;
            if (range.first_block >= first && range.first_block + range.block_count <= end)
            {
                object_segment = &segment;
                break;
            }
            first = end;
        }
        assert(object_segment && object_segment->signature == codec);

        bool found_info = false;
        bool found_mesh = false;
        umbf::BlockIterator entry(scene, range.first_block);
        for (u32 i = 0u; i < range.block_count; ++i, ++entry)
        {
            assert(entry != scene.end());
            auto block = umbf::get_block(entry);
            assert(block && block->signature() == entry->signature);
            if (entry->signature == umbf::sign_block::object_info)
            {
                const auto *info = static_cast<const umbf::ObjectInfo *>(block.get());
                assert(!info->name.empty());
                found_info = true;
            }
            else if (entry->signature == umbf::sign_block::mesh)
            {
                const auto *mesh = static_cast<const umbf::mesh::Mesh *>(block.get());
                assert(!mesh->geometry.vertices.empty() && !mesh->geometry.faces.empty());
                found_mesh = true;
            }
        }
        assert(!entry.failed() && found_info && found_mesh);
        ++object_count;
    }
    assert(object_count > 0u);
    umbf::close_read_descriptor(scene);
    umbf::registry::resolver = nullptr;
}
