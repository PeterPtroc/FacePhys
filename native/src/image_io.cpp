#include "facephys/image_io.h"

#include <cctype>
#include <exception>
#include <fstream>

namespace facephys {
namespace {
bool token(std::istream& stream, std::string* result) {
  result->clear();
  char character = 0;
  while (stream.get(character)) {
    if (character == '#') { std::string ignored; std::getline(stream, ignored); continue; }
    if (!std::isspace(static_cast<unsigned char>(character))) { result->push_back(character); break; }
  }
  while (stream.get(character) && !std::isspace(static_cast<unsigned char>(character))) result->push_back(character);
  return !result->empty();
}
}
bool read_ppm(const std::string& path, RgbFrame* frame, std::string* error) {
  std::ifstream input(path, std::ios::binary);
  std::string magic, width, height, maximum;
  if (!input || !token(input, &magic) || !token(input, &width) || !token(input, &height) || !token(input, &maximum) ||
      (magic != "P6" && magic != "P3") || maximum != "255") {
    if (error) *error = "expected P6 or P3 PPM with max value 255: " + path;
    return false;
  }
  int w = 0;
  int h = 0;
  try {
    w = std::stoi(width);
    h = std::stoi(height);
  } catch (const std::exception&) {
    if (error) *error = "invalid PPM dimensions: " + path;
    return false;
  }
  if (w <= 0 || h <= 0) { if (error) *error = "invalid PPM dimensions"; return false; }
  frame->resize(w, h);
  if (magic == "P3") {
    std::string component;
    for (auto& value : frame->rgb) {
      if (!token(input, &component)) { if (error) *error = "short P3 PPM image data: " + path; return false; }
      try {
        const int parsed = std::stoi(component);
        if (parsed < 0 || parsed > 255) { if (error) *error = "P3 pixel component outside [0,255]: " + path; return false; }
        value = static_cast<std::uint8_t>(parsed);
      } catch (const std::exception&) {
        if (error) *error = "invalid P3 pixel component: " + path;
        return false;
      }
    }
    return true;
  }
  input.read(reinterpret_cast<char*>(frame->rgb.data()), static_cast<std::streamsize>(frame->rgb.size()));
  if (input.gcount() != static_cast<std::streamsize>(frame->rgb.size())) { if (error) *error = "short PPM image data: " + path; return false; }
  return true;
}
bool write_ppm(const std::string& path, const RgbFrame& frame, std::string* error) {
  if (!frame.valid()) { if (error) *error = "cannot write invalid RGB frame"; return false; }
  std::ofstream output(path, std::ios::binary);
  if (!output) { if (error) *error = "cannot create PPM: " + path; return false; }
  output << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
  output.write(reinterpret_cast<const char*>(frame.rgb.data()), static_cast<std::streamsize>(frame.rgb.size()));
  if (!output) { if (error) *error = "cannot write PPM pixels: " + path; return false; }
  return true;
}
}  // namespace facephys
