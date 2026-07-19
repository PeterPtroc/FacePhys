#include "facephys/config.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <map>
#include <sstream>
#include <utility>

namespace facephys {
namespace {

struct Json {
  enum class Kind { kNull, kBool, kNumber, kString, kObject };
  Kind kind = Kind::kNull;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  std::map<std::string, Json> object;
};

class JsonParser {
 public:
  explicit JsonParser(std::string text) : text_(std::move(text)) {}
  bool parse(Json* result, std::string* error) {
    skip();
    if (!value(result, error)) return false;
    skip();
    if (pos_ != text_.size()) return fail("trailing JSON data", error);
    return true;
  }
 private:
  bool value(Json* result, std::string* error) {
    skip();
    if (pos_ == text_.size()) return fail("unexpected end of JSON", error);
    if (text_[pos_] == '{') return object(result, error);
    if (text_[pos_] == '"') {
      result->kind = Json::Kind::kString;
      return string(&result->string, error);
    }
    if (text_.compare(pos_, 4, "true") == 0) { pos_ += 4; result->kind = Json::Kind::kBool; result->boolean = true; return true; }
    if (text_.compare(pos_, 5, "false") == 0) { pos_ += 5; result->kind = Json::Kind::kBool; result->boolean = false; return true; }
    if (text_.compare(pos_, 4, "null") == 0) { pos_ += 4; result->kind = Json::Kind::kNull; return true; }
    char* end = nullptr;
    const double number = std::strtod(text_.c_str() + pos_, &end);
    if (end == text_.c_str() + pos_) return fail("invalid JSON value", error);
    result->kind = Json::Kind::kNumber;
    result->number = number;
    pos_ = static_cast<std::size_t>(end - text_.c_str());
    return true;
  }
  bool object(Json* result, std::string* error) {
    ++pos_;
    result->kind = Json::Kind::kObject;
    skip();
    if (consume('}')) return true;
    while (true) {
      std::string key;
      if (!string(&key, error)) return false;
      skip();
      if (!consume(':')) return fail("expected ':' in object", error);
      Json member;
      if (!value(&member, error)) return false;
      result->object.emplace(std::move(key), std::move(member));
      skip();
      if (consume('}')) return true;
      if (!consume(',')) return fail("expected ',' in object", error);
      skip();
    }
  }
  bool string(std::string* result, std::string* error) {
    if (!consume('"')) return fail("expected JSON string", error);
    result->clear();
    while (pos_ < text_.size() && text_[pos_] != '"') {
      if (text_[pos_] == '\\') {
        ++pos_;
        if (pos_ == text_.size()) return fail("invalid string escape", error);
        const char escaped = text_[pos_++];
        switch (escaped) {
          case '"': result->push_back('"'); break;
          case '\\': result->push_back('\\'); break;
          case '/': result->push_back('/'); break;
          case 'b': result->push_back('\b'); break;
          case 'f': result->push_back('\f'); break;
          case 'n': result->push_back('\n'); break;
          case 'r': result->push_back('\r'); break;
          case 't': result->push_back('\t'); break;
          default: return fail("unsupported Unicode JSON escape", error);
        }
      } else {
        result->push_back(text_[pos_++]);
      }
    }
    if (!consume('"')) return fail("unterminated JSON string", error);
    return true;
  }
  bool consume(char expected) {
    if (pos_ < text_.size() && text_[pos_] == expected) { ++pos_; return true; }
    return false;
  }
  void skip() { while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_; }
  bool fail(const std::string& message, std::string* error) const {
    if (error) *error = message + " at JSON byte " + std::to_string(pos_);
    return false;
  }
  std::string text_;
  std::size_t pos_ = 0U;
};

const Json* member(const Json& object, const char* key) {
  if (object.kind != Json::Kind::kObject) return nullptr;
  const auto found = object.object.find(key);
  return found == object.object.end() ? nullptr : &found->second;
}
bool get_string(const Json& object, const char* key, std::string* output, bool required, std::string* error) {
  const Json* value = member(object, key);
  if (!value) {
    if (required && error) *error = std::string("missing config key: ") + key;
    return !required;
  }
  if (value->kind != Json::Kind::kString) { if (error) *error = std::string("config key is not a string: ") + key; return false; }
  *output = value->string;
  return true;
}
bool get_int(const Json& object, const char* key, int* output, std::string* error) {
  const Json* value = member(object, key);
  if (!value) return true;
  if (value->kind != Json::Kind::kNumber) { if (error) *error = std::string("config key is not a number: ") + key; return false; }
  *output = static_cast<int>(value->number);
  return true;
}
bool get_bool(const Json& object, const char* key, bool* output, std::string* error) {
  const Json* value = member(object, key);
  if (!value) return true;
  if (value->kind != Json::Kind::kBool) { if (error) *error = std::string("config key is not boolean: ") + key; return false; }
  *output = value->boolean;
  return true;
}
bool object_required(const Json& root, const char* key, const Json** output, std::string* error) {
  *output = member(root, key);
  if (!*output || (*output)->kind != Json::Kind::kObject) { if (error) *error = std::string("missing config object: ") + key; return false; }
  return true;
}

}  // namespace

bool load_config_file(const std::string& path, AppConfig* config, std::string* error) {
  std::ifstream input(path);
  if (!input) { if (error) *error = "cannot read configuration: " + path; return false; }
  std::stringstream contents;
  contents << input.rdbuf();
  Json root;
  if (!JsonParser(contents.str()).parse(&root, error) || root.kind != Json::Kind::kObject) {
    if (error && error->empty()) *error = "configuration root is not an object";
    return false;
  }
  const Json *models = nullptr, *camera = nullptr, *inference = nullptr, *signal = nullptr, *display = nullptr;
  if (!object_required(root, "models", &models, error) || !object_required(root, "camera", &camera, error) ||
      !object_required(root, "inference", &inference, error) || !object_required(root, "signal", &signal, error) ||
      !object_required(root, "display", &display, error)) return false;
  if (!get_string(*models, "main", &config->models.main_model, true, error) ||
      !get_string(*models, "state", &config->models.state, true, error) ||
      !get_string(*models, "sqi", &config->models.sqi, true, error) ||
      !get_string(*models, "psd", &config->models.psd, true, error) ||
      !get_string(*models, "face_detector", &config->models.face_detector, false, error)) return false;
  if (!get_string(*camera, "backend", &config->camera.backend, false, error) ||
      !get_string(*camera, "device", &config->camera.device, false, error) || !get_int(*camera, "width", &config->camera.width, error) ||
      !get_int(*camera, "height", &config->camera.height, error) || !get_int(*camera, "fps", &config->camera.fps, error) ||
      !get_string(*camera, "pixel_format", &config->camera.pixel_format, false, error) ||
      !get_int(*camera, "serial_timeout_ms", &config->camera.serial_timeout_ms, error) ||
      !get_int(*camera, "max_frame_bytes", &config->camera.max_frame_bytes, error)) return false;
  if (const Json* roi = member(*camera, "fixed_roi"); roi && roi->kind != Json::Kind::kNull) {
    if (roi->kind != Json::Kind::kObject) { if (error) *error = "camera.fixed_roi must be an object or null"; return false; }
    RectF rectangle;
    const Json* x = member(*roi, "x"); const Json* y = member(*roi, "y");
    const Json* w = member(*roi, "width"); const Json* h = member(*roi, "height");
    if (!x || !y || !w || !h || x->kind != Json::Kind::kNumber || y->kind != Json::Kind::kNumber ||
        w->kind != Json::Kind::kNumber || h->kind != Json::Kind::kNumber) { if (error) *error = "fixed_roi requires numeric x, y, width, height"; return false; }
    rectangle = {static_cast<float>(x->number), static_cast<float>(y->number), static_cast<float>(w->number), static_cast<float>(h->number)};
    if (!rectangle.valid()) { if (error) *error = "fixed_roi dimensions are invalid"; return false; }
    config->camera.fixed_roi = rectangle;
  }
  if (!get_int(*inference, "main_threads", &config->inference.main_threads, error) || !get_bool(*inference, "enable_xnnpack", &config->inference.enable_xnnpack, error) ||
      !get_int(*inference, "target_fps", &config->inference.target_fps, error) || !get_int(*inference, "face_detection_interval_ms", &config->inference.face_detection_interval_ms, error) ||
      !get_int(*signal, "window_samples", &config->signal.window_samples, error) || !get_int(*signal, "sqi_interval_ms", &config->signal.sqi_interval_ms, error) ||
      !get_int(*signal, "psd_interval_ms", &config->signal.psd_interval_ms, error) || !get_int(*signal, "psd_threads", &config->signal.psd_threads, error) ||
      !get_int(*signal, "sqi_threads", &config->signal.sqi_threads, error)) return false;
  if (!get_bool(*display, "enabled", &config->display.enabled, error) || !get_string(*display, "driver", &config->display.driver, false, error) ||
      !get_string(*display, "spi_device", &config->display.spi_device, false, error) || !get_int(*display, "spi_speed_hz", &config->display.spi_speed_hz, error) ||
      !get_int(*display, "width", &config->display.width, error) || !get_int(*display, "height", &config->display.height, error) ||
      !get_int(*display, "rotation", &config->display.rotation, error) || !get_int(*display, "x_offset", &config->display.x_offset, error) ||
      !get_int(*display, "y_offset", &config->display.y_offset, error) || !get_bool(*display, "bgr", &config->display.bgr, error) ||
      !get_bool(*display, "invert", &config->display.invert, error) || !get_string(*display, "tab", &config->display.tab, false, error) ||
      !get_string(*display, "dc_gpiochip", &config->display.dc_gpiochip, false, error) || !get_int(*display, "dc_gpio_line", &config->display.dc_gpio_line, error) ||
      !get_int(*display, "dc_gpio_number", &config->display.dc_gpio_number, error) || !get_string(*display, "reset_gpiochip", &config->display.reset_gpiochip, false, error) ||
      !get_int(*display, "reset_gpio_line", &config->display.reset_gpio_line, error) || !get_int(*display, "reset_gpio_number", &config->display.reset_gpio_number, error) ||
      !get_int(*display, "display_interval_ms", &config->display.display_interval_ms, error)) return false;
  if ((config->camera.backend != "v4l2" && config->camera.backend != "openmv_serial") ||
      config->camera.width <= 0 || config->camera.height <= 0 || config->camera.fps <= 0 ||
      config->camera.serial_timeout_ms <= 0 || config->camera.max_frame_bytes < 4096 || config->inference.main_threads <= 0 ||
      config->signal.window_samples != 450 || config->display.width <= 0 || config->display.height <= 0) {
    if (error) *error = "unsupported non-positive dimensions/threads or signal.window_samples != 450";
    return false;
  }
  const auto parent = std::filesystem::absolute(std::filesystem::path(path)).parent_path();
  const auto resolve = [&parent](std::string* value) {
    const std::filesystem::path candidate(*value);
    if (candidate.is_relative()) *value = (parent / candidate).lexically_normal().string();
  };
  resolve(&config->models.main_model); resolve(&config->models.state); resolve(&config->models.sqi);
  resolve(&config->models.psd); if (!config->models.face_detector.empty()) resolve(&config->models.face_detector);
  return true;
}

}  // namespace facephys
