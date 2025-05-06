#pragma once
#include <acul/string/string.hpp>
#include <umbf/umbf.hpp>

bool convert_raw(const acul::string &input, bool compressed, umbf::File &file);

u32 convert_image(const acul::string &input, bool compressed, umbf::File &file);

u32 convert_scene(const acul::string &input, const acul::string &output, bool compressed);

u32 convert_json(const acul::string &input, const acul::string &output, bool compressed);