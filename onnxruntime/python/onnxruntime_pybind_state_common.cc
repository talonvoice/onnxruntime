#include "onnxruntime_pybind_exceptions.h"
#include "onnxruntime_pybind_state_common.h"

#include "core/framework/arena_extend_strategy.h"

namespace onnxruntime {
namespace python {
namespace py = pybind11;

const std::string onnxruntime::python::SessionObjectInitializer::default_logger_id = "Default";

#ifdef USE_OPENVINO
// TODO remove deprecated global config
std::string openvino_device_type;
#endif

#ifdef USE_NUPHAR
// TODO remove deprecated global config
std::string nuphar_settings;
#endif


// TODO remove deprecated global config
OrtDevice::DeviceId cuda_device_id = 0;
// TODO remove deprecated global config
size_t gpu_mem_limit = std::numeric_limits<size_t>::max();

#if defined(USE_CUDA) || defined(USE_ROCM)
#ifdef USE_CUDA
// TODO remove deprecated global config
OrtCudnnConvAlgoSearch cudnn_conv_algo_search = OrtCudnnConvAlgoSearch::EXHAUSTIVE;
// TODO remove deprecated global config
bool do_copy_in_default_stream = true;
onnxruntime::CUDAExecutionProviderExternalAllocatorInfo external_allocator_info{};
#endif

#ifdef USE_ROCM
#include "core/providers/rocm/rocm_execution_provider.h"
#include "core/providers/rocm/rocm_allocator.h"
onnxruntime::ROCMExecutionProviderExternalAllocatorInfo external_allocator_info{};
#endif

// TODO remove deprecated global config
onnxruntime::ArenaExtendStrategy arena_extend_strategy = onnxruntime::ArenaExtendStrategy::kNextPowerOfTwo;
#endif

void PySparseTensor::Init(std::unique_ptr<SparseTensor>&& instance) {
  auto sparse_tensor(std::move(instance));
  auto ml_type = DataTypeImpl::GetType<SparseTensor>();
  ort_value_.Init(sparse_tensor.get(), ml_type, ml_type->GetDeleteFunc());
  sparse_tensor.release();
}

PySparseTensor::~PySparseTensor() {
  // pybind11 will deref and potentially destroy its objects
  // that we use to hold a reference and it may throw python errors
  // so we want to do it in a controlled manner
  auto None = py::none();
  for (auto& obj : backing_storage_) {
    try {
      obj = None;
    } catch (py::error_already_set& ex) {
      // we need it mutable to properly log and discard it
      ex.discard_as_unraisable(__func__);
    }
  }
}

}  // namespace python
}  // namespace onnxruntime
