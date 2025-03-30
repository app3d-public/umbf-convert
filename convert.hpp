#pragma once
#include <acul/string/string.hpp>
#include <assets/asset.hpp>

bool convertRaw(const acul::string &input, bool compressed, umbf::File &file);

u32 convertImage(const acul::string &input, bool compressed, umbf::File &file);

u32 convertScene(const acul::string &input, const acul::string &output, bool compressed);

u32 convertJson(const acul::string &input, const acul::string &output, bool compressed);