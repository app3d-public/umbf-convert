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
    size_t object_count = 0u;
    auto list_entry = scene.begin();
    for (; list_entry != scene.end(); ++list_entry)
    {
        if (list_entry->signature != umbf::sign_block::format::scene_objects_list) continue;
        umbf::ReadDescriptor objects;
        assert(umbf::create_read_descriptor(scene, list_entry, objects).success());
        auto object_entry = objects.begin();
        for (; object_entry != objects.end(); ++object_entry)
        {
            assert(object_entry->signature == umbf::sign_block::format::scene_object);
            umbf::ReadDescriptor object;
            assert(umbf::create_read_descriptor(objects, object_entry, object).success());
            assert(object.file->sg_table.size() == 1u);
            const auto &segment = object.file->sg_table.front();
            assert(segment.signature == codec && segment.block_count >= 2u);
            bool found_info = false;
            bool found_mesh = false;
            size_t block_count = 0u;
            auto entry = object.begin();
            for (; entry != object.end(); ++entry, ++block_count)
            {
                if (entry->signature == umbf::sign_block::object_info)
                {
                    auto block = umbf::get_block(entry);
                    assert(block && block->signature() == umbf::sign_block::object_info);
                    const auto info = acul::static_pointer_cast<umbf::ObjectInfo>(block);
                    assert(!info->name.empty());
                    found_info = true;
                }
                else if (entry->signature == umbf::sign_block::mesh)
                {
                    auto block = umbf::get_block(entry);
                    assert(block && block->signature() == umbf::sign_block::mesh);
                    const auto mesh = acul::static_pointer_cast<umbf::mesh::Mesh>(block);
                    assert(!mesh->geometry.vertices.empty() && !mesh->geometry.faces.empty());
                    found_mesh = true;
                }
            }
            assert(!entry.failed() && block_count == segment.block_count && found_info && found_mesh);
            ++object_count;
            umbf::close_map_descriptor(object);
        }
        assert(!object_entry.failed());
        umbf::close_map_descriptor(objects);
    }
    assert(!list_entry.failed() && object_count > 0u);
    umbf::close_map_descriptor(scene);
    umbf::registry::resolver = nullptr;
}
