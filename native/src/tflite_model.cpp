#include "facephys/tflite_model.h"

#include <iostream>
#include <sstream>

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/delegates/xnnpack/xnnpack_delegate.h"
#include "tensorflow/lite/kernels/register.h"

namespace facephys {
namespace {

std::string shape_string(const TfLiteIntArray* dims) {
  std::ostringstream stream;
  stream << '[';
  if (dims != nullptr) {
    for (int i = 0; i < dims->size; ++i) {
      if (i != 0) stream << ',';
      stream << dims->data[i];
    }
  }
  stream << ']';
  return stream.str();
}

void print_tensor(const char* direction, int position, int tensor_index,
                  const TfLiteTensor* tensor) {
  std::cout << direction << " position=" << position << " index=" << tensor_index
            << " name=" << (tensor != nullptr && tensor->name ? tensor->name : "(unnamed)")
            << " type=" << (tensor != nullptr ? TfLiteTypeGetName(tensor->type) : "(null)")
            << " shape=" << (tensor != nullptr ? shape_string(tensor->dims) : "[]")
            << " bytes=" << (tensor != nullptr ? tensor->bytes : 0U) << '\n';
}

}  // namespace

TfliteRunner::~TfliteRunner() {
  // TFLite may retain delegate-owned kernels; release the interpreter first.
  interpreter_.reset();
  if (xnnpack_delegate_ != nullptr) {
    TfLiteXNNPackDelegateDelete(xnnpack_delegate_);
    xnnpack_delegate_ = nullptr;
  }
}

bool TfliteRunner::load(const std::string& model_path, const TfliteRunnerOptions& options,
                        std::string* error) {
  path_ = model_path;
  model_ = tflite::FlatBufferModel::BuildFromFile(model_path.c_str());
  if (!model_) {
    if (error) *error = "could not load TFLite model: " + model_path;
    return false;
  }
  // This intentionally matches benchmark_model's delegate mechanism.  With
  // --define=tflite_with_xnnpack=true, BuiltinOpResolver wires in the default
  // XNNPACK provider; SetNumThreads below becomes the provider's thread count.
  // The "without" resolver is retained for SQI/PSD and for an explicit OFF
  // configuration, so those models cannot consume the main-model CPU budget.
  std::unique_ptr<tflite::OpResolver> resolver;
  if (options.enable_xnnpack) {
    resolver = std::make_unique<tflite::ops::builtin::BuiltinOpResolver>();
  } else {
    resolver = std::make_unique<tflite::ops::builtin::BuiltinOpResolverWithoutDefaultDelegates>();
  }
  tflite::InterpreterBuilder builder(*model_, *resolver);
  if (builder(&interpreter_) != kTfLiteOk || !interpreter_) {
    if (error) *error = "could not create TFLite interpreter for: " + model_path;
    return false;
  }
  interpreter_->SetNumThreads(options.threads);
  delegated_kernel_count_ = 0;
  if (interpreter_->AllocateTensors() != kTfLiteOk) {
    if (error) *error = "AllocateTensors failed for: " + model_path;
    interpreter_.reset();
    return false;
  }
  for (const int node_index : interpreter_->execution_plan()) {
    const auto* node_and_registration = interpreter_->node_and_registration(node_index);
    if (node_and_registration != nullptr && node_and_registration->first.delegate != nullptr) {
      ++delegated_kernel_count_;
    }
  }
  xnnpack_enabled_ = options.enable_xnnpack && delegated_kernel_count_ > 0;
  if (options.enable_xnnpack && !xnnpack_enabled_) {
    if (error) *error = "XNNPACK was requested but no graph nodes were delegated: " + model_path;
    interpreter_.reset();
    return false;
  }
  return true;
}

void TfliteRunner::describe_io(const std::string& model_label) const {
  if (!interpreter_) return;
  std::cout << "MODEL " << model_label << " path=" << path_ << '\n';
  if (xnnpack_enabled()) {
    std::cout << "DELEGATE XNNPACK kernels=" << delegated_kernel_count_ << '\n';
  }
  const auto& inputs = interpreter_->inputs();
  const auto& outputs = interpreter_->outputs();
  for (int position = 0; position < static_cast<int>(inputs.size()); ++position) {
    const int index = inputs[position];
    print_tensor("INPUT", position, index, interpreter_->tensor(index));
  }
  for (int position = 0; position < static_cast<int>(outputs.size()); ++position) {
    const int index = outputs[position];
    print_tensor("OUTPUT", position, index, interpreter_->tensor(index));
  }
}

}  // namespace facephys
