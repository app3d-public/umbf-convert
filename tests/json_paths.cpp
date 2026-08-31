#include "models/umbf.hpp"
#include <cassert>

int main(int argc, char **argv)
{
    assert(argc == 2);
    models::Image image;
    assert(image.deserialize_from_file(argv[1]));
    const auto source = acul::static_pointer_cast<models::IPath>(image.serializer());
    assert(source->path() == "test_image.png");

    models::Mesh mesh;
    rapidjson::Document document;
    document.Parse(R"({"path":"../meshes/cube.obj"})");
    assert(mesh.deserialize_object(document));
    assert(mesh.path() == "../meshes/cube.obj");

    for (const char *url : {"../textures/image.umia", "assets://textures/image.umia"})
    {
        document.Parse(R"({"url":"", "target_type":"image"})");
        document["url"].SetString(url, document.GetAllocator());
        models::Target target;
        assert(target.deserialize_object(document));
        assert(target.url() == url);
    }
}
