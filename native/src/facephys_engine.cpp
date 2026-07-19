#include "facephys/facephys_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#include <zlib.h>

#include "facephys/state_mapping.h"
#include "facephys/tflite_model.h"
#include "tensorflow/lite/c/common.h"

namespace facephys {
namespace {

class StateJsonParser {
 public:
  explicit StateJsonParser(const std::string& input) : input_(input) {}

  bool parse(std::unordered_map<std::string, std::vector<float>>* states,
             std::string* error) {
    skip_space();
    if (!consume('{')) return fail("state root is not a JSON object", error);
    skip_space();
    while (!consume('}')) {
      std::string key;
      if (!parse_string(&key, error)) return false;
      skip_space();
      if (!consume(':')) return fail("missing ':' after state key", error);
      std::vector<float> values;
      if (!parse_array(&values, error)) return false;
      states->emplace(std::move(key), std::move(values));
      skip_space();
      if (consume('}')) break;
      if (!consume(',')) return fail("missing ',' between states", error);
      skip_space();
    }
    skip_space();
    return position_ == input_.size() || fail("trailing state JSON data", error);
  }

 private:
  bool parse_string(std::string* value, std::string* error) {
    if (!consume('"')) return fail("expected JSON string", error);
    value->clear();
    while (position_ < input_.size() && input_[position_] != '"') {
      if (input_[position_] == '\\') return fail("escaped state JSON strings unsupported", error);
      value->push_back(input_[position_++]);
    }
    if (!consume('"')) return fail("unterminated JSON string", error);
    return true;
  }

  bool parse_array(std::vector<float>* values, std::string* error) {
    skip_space();
    if (!consume('[')) return fail("expected state JSON array", error);
    skip_space();
    while (!consume(']')) {
      if (position_ >= input_.size()) return fail("unterminated state array", error);
      if (input_[position_] == '[') {
        if (!parse_array(values, error)) return false;
      } else {
        char* end = nullptr;
        const float number = std::strtof(input_.c_str() + position_, &end);
        if (end == input_.c_str() + position_ || !std::isfinite(number)) {
          return fail("invalid numeric value in state JSON", error);
        }
        values->push_back(number);
        position_ = static_cast<std::size_t>(end - input_.c_str());
      }
      skip_space();
      if (consume(']')) break;
      if (!consume(',')) return fail("missing ',' in state array", error);
      skip_space();
    }
    return true;
  }

  bool consume(char expected) {
    if (position_ < input_.size() && input_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }
  void skip_space() {
    while (position_ < input_.size() &&
           (input_[position_] == ' ' || input_[position_] == '\n' ||
            input_[position_] == '\r' || input_[position_] == '\t')) {
      ++position_;
    }
  }
  bool fail(const std::string& message, std::string* error) const {
    if (error) *error = message + " at byte " + std::to_string(position_);
    return false;
  }

  const std::string& input_;
  std::size_t position_ = 0U;
};

bool read_gzip_file(const std::string& path, std::string* text, std::string* error) {
  gzFile file = gzopen(path.c_str(), "rb");
  if (file == nullptr) {
    if (error) *error = "cannot open gzip state file: " + path;
    return false;
  }
  text->clear();
  char buffer[32768];
  int read_count = 0;
  while ((read_count = gzread(file, buffer, sizeof(buffer))) > 0) {
    text->append(buffer, static_cast<std::size_t>(read_count));
  }
  if (read_count < 0) {
    int zlib_error = Z_OK;
    const char* message = gzerror(file, &zlib_error);
    if (error) {
      *error = "cannot decompress state file " + path + ": " +
               std::string(message ? message : "zlib error") +
               " (code " + std::to_string(zlib_error) + ")";
    }
    gzclose(file);
    return false;
  }
  gzclose(file);
  return true;
}

int input_position_named(const tflite::Interpreter& interpreter, const char* name) {
  const auto& inputs = interpreter.inputs();
  for (int position = 0; position < static_cast<int>(inputs.size()); ++position) {
    const TfLiteTensor* tensor = interpreter.tensor(inputs[position]);
    if (tensor != nullptr && tensor->name != nullptr && std::strcmp(tensor->name, name) == 0) {
      return inputs[position];
    }
  }
  return -1;
}

bool tensor_is_float(const TfLiteTensor* tensor, std::size_t bytes) {
  return tensor != nullptr && tensor->type == kTfLiteFloat32 && tensor->bytes == bytes;
}

bool all_finite(const TfLiteTensor& tensor) {
  if (tensor.type != kTfLiteFloat32 || tensor.data.f == nullptr) return false;
  const std::size_t count = tensor.bytes / sizeof(float);
  return std::all_of(tensor.data.f, tensor.data.f + count,
                     [](float value) { return std::isfinite(value); });
}

}  // namespace

FacePhysEngine::FacePhysEngine() = default;
FacePhysEngine::~FacePhysEngine() = default;

bool FacePhysEngine::initialize(const FacePhysEngineOptions& options, std::string* error) {
  options_ = options;
  runner_ = std::make_unique<TfliteRunner>();
  if (!runner_->load(options.model_path, {options.threads, options.enable_xnnpack}, error)) {
    runner_.reset();
    return false;
  }
  auto* interpreter = runner_->interpreter();
  image_input_tensor_ = input_position_named(*interpreter, "input");
  dt_input_tensor_ = input_position_named(*interpreter, "dt");
  if (!validate_contract(error) || !load_initial_state(error)) {
    runner_.reset();
    return false;
  }
  step_ = 0;
  return true;
}

bool FacePhysEngine::initialized() const { return runner_ != nullptr && runner_->interpreter() != nullptr; }

void FacePhysEngine::describe_io() const {
  if (runner_) runner_->describe_io("FacePhys main model");
}

bool FacePhysEngine::validate_contract(std::string* error) const {
  if (!initialized()) {
    if (error) *error = "FacePhys engine is not initialized";
    return false;
  }
  auto* interpreter = runner_->interpreter();
  if (interpreter->inputs().size() != 48U || interpreter->outputs().size() != 47U) {
    if (error) *error = "unexpected main-model I/O count (expected 48 inputs / 47 outputs)";
    return false;
  }
  if (image_input_tensor_ < 0 || dt_input_tensor_ < 0) {
    if (error) *error = "main model is missing named input and/or dt tensors";
    return false;
  }
  if (!tensor_is_float(interpreter->tensor(image_input_tensor_), 36U * 36U * 3U * sizeof(float)) ||
      !tensor_is_float(interpreter->tensor(dt_input_tensor_), sizeof(float))) {
    if (error) *error = "main model input/dt tensor contract does not match FacePhys";
    return false;
  }
  for (const auto entry : kNativeTfliteStateMap) {
    const int input_index = interpreter->inputs()[entry.input_position];
    const int output_index = interpreter->outputs()[entry.output_position];
    const TfLiteTensor* input = interpreter->tensor(input_index);
    const TfLiteTensor* output = interpreter->tensor(output_index);
    const std::string expected_input = "state_in_" + std::to_string(entry.input_position - 2);
    const std::string expected_output = "Identity_" + std::to_string(entry.output_position);
    if (input == nullptr || output == nullptr || input->type != kTfLiteFloat32 ||
        output->type != kTfLiteFloat32 || input->bytes != output->bytes ||
        input->name == nullptr || output->name == nullptr ||
        expected_input != input->name || expected_output != output->name) {
      if (error) {
        *error = "state-map tensor name/type/size mismatch at input position " +
                 std::to_string(entry.input_position) + " / output position " +
                 std::to_string(entry.output_position);
      }
      return false;
    }
  }
  return true;
}

bool FacePhysEngine::load_initial_state(std::string* error) {
  std::string json;
  if (!read_gzip_file(options_.state_path, &json, error)) return false;
  std::unordered_map<std::string, std::vector<float>> states;
  StateJsonParser parser(json);
  if (!parser.parse(&states, error)) return false;

  auto* interpreter = runner_->interpreter();
  for (int position = 2; position < static_cast<int>(interpreter->inputs().size()); ++position) {
    TfLiteTensor* tensor = interpreter->tensor(interpreter->inputs()[position]);
    if (tensor == nullptr || tensor->name == nullptr || tensor->type != kTfLiteFloat32) {
      if (error) *error = "invalid state tensor at main input position " + std::to_string(position);
      return false;
    }
    const auto found = states.find(tensor->name);
    const std::size_t count = tensor->bytes / sizeof(float);
    if (found == states.end() || found->second.size() != count) {
      if (error) *error = "state file entry mismatch for " + std::string(tensor->name) +
                          ": expected " + std::to_string(count) + " floats";
      return false;
    }
    std::memcpy(tensor->data.f, found->second.data(), tensor->bytes);
  }
  return true;
}

bool FacePhysEngine::reset(std::string* error) {
  if (!initialized()) {
    if (error) *error = "cannot reset uninitialized FacePhys engine";
    return false;
  }
  if (!load_initial_state(error)) return false;
  step_ = 0;
  return true;
}

bool FacePhysEngine::invoke(const std::array<float, 36U * 36U * 3U>& rgb,
                            float dt_seconds, FacePhysInference* result,
                            std::string* error) {
  if (!initialized()) {
    if (error) *error = "cannot invoke uninitialized FacePhys engine";
    return false;
  }
  if (!std::isfinite(dt_seconds) || dt_seconds <= 0.0F || dt_seconds > 2.0F) {
    if (error) *error = "invalid inter-frame dt (seconds): " + std::to_string(dt_seconds);
    return false;
  }
  auto* interpreter = runner_->interpreter();
  std::memcpy(interpreter->tensor(image_input_tensor_)->data.f, rgb.data(), sizeof(rgb));
  interpreter->tensor(dt_input_tensor_)->data.f[0] = dt_seconds;
  const auto start = std::chrono::steady_clock::now();
  if (interpreter->Invoke() != kTfLiteOk) {
    if (error) *error = "FacePhys main-model Invoke failed";
    return false;
  }
  const auto end = std::chrono::steady_clock::now();
  const TfLiteTensor* bvp_tensor = interpreter->tensor(interpreter->outputs()[0]);
  if (!tensor_is_float(bvp_tensor, sizeof(float)) || !std::isfinite(bvp_tensor->data.f[0])) {
    if (error) *error = "FacePhys BVP output is missing, NaN, or Inf";
    return false;
  }
  for (const auto entry : kNativeTfliteStateMap) {
    const TfLiteTensor* output = interpreter->tensor(interpreter->outputs()[entry.output_position]);
    if (output == nullptr || !all_finite(*output)) {
      if (error) *error = "FacePhys recurrent state contains NaN/Inf at output position " +
                          std::to_string(entry.output_position);
      return false;
    }
  }
  for (const auto entry : kNativeTfliteStateMap) {
    TfLiteTensor* input = interpreter->tensor(interpreter->inputs()[entry.input_position]);
    const TfLiteTensor* output = interpreter->tensor(interpreter->outputs()[entry.output_position]);
    std::memmove(input->data.raw, output->data.raw, input->bytes);
  }
  ++step_;
  if (result) {
    result->bvp = bvp_tensor->data.f[0];
    result->inference_ms = std::chrono::duration<double, std::milli>(end - start).count();
    result->step = step_;
  }
  return true;
}

}  // namespace facephys
