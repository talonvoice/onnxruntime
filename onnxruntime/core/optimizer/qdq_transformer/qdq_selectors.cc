// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/optimizer/qdq_transformer/qdq_selectors.h"

#if !defined(ORT_MINIMAL_BUILD)

#include "core/graph/graph.h"
#include "core/optimizer/initializer.h"
#include "core/optimizer/qdq_transformer/qdq_util.h"
#include "core/optimizer/utils.h"

namespace onnxruntime {
namespace QDQ {
namespace {
// adjust for an optional input that has an entry but does not exist
int NumActualInputs(const Node& node) {
  const auto& input_defs = node.InputDefs();
  return gsl::narrow_cast<int>(std::count_if(input_defs.cbegin(), input_defs.cend(),
                                             [](const NodeArg* def) { return def && def->Exists(); }));
}
}  // namespace

bool BaseSelector::CheckQDQNodes(const Graph& graph, const Node& node,
                                 const std::vector<const Node*>& dq_nodes,
                                 const std::vector<const Node*>& q_nodes,
                                 int num_dq_inputs) const {
  if (num_dq_inputs == -1) {
    num_dq_inputs = NumActualInputs(node);
  }

  return num_dq_inputs == gsl::narrow_cast<int>(dq_nodes.size()) &&
         node.OutputDefs().size() == q_nodes.size() &&
         optimizer_utils::CheckOutputEdges(graph, node, q_nodes.size());
}

bool BaseSelector::Select(Graph& graph, const Node& node, std::unique_ptr<NodesToOptimize>& selection) const {
  std::vector<const Node*> dq_nodes = graph_utils::FindParentsByType(node, QDQ::DQOpName);
  std::vector<const Node*> q_nodes = graph_utils::FindChildrenByType(node, QDQ::QOpName);

  if (!Check(graph, node, dq_nodes, q_nodes)) {
    return false;
  }

  auto get_mutable_node = [&graph](const Node* node) {
    // we use the non-const GetNode to convert the const Node* to Node*
    return graph.GetNode(node->Index());
  };

  NodesToOptimizeBuilder builder;
  builder.input_nodes.reserve(dq_nodes.size());
  builder.output_nodes.reserve(q_nodes.size());

  for (const Node* dq_node : dq_nodes) {
    builder.input_nodes.push_back(dq_node != nullptr ? get_mutable_node(dq_node) : nullptr);
  }

  builder.target_node = get_mutable_node(&node);

  for (const Node* q_node : q_nodes) {
    builder.output_nodes.push_back(get_mutable_node(q_node));
  }

  UpdateBuilder(builder);

  selection = builder.Build();

  return true;
}

bool DropDQDNodesSelector::Check(const Graph& graph,
                                 const Node& node,
                                 const std::vector<const Node*>& dq_nodes,
                                 const std::vector<const Node*>& q_nodes) const {
  if (!CheckQDQNodes(graph, node, dq_nodes, q_nodes, 1)) {
    return false;
  }

  const Node& dq_node = *dq_nodes.front();
  const Node& q_node = *q_nodes.front();

  return IsQDQPairSupported(graph, q_node, dq_node);
}

bool UnarySelector::Check(const Graph& graph, const Node& node,
                          const std::vector<const Node*>& dq_nodes,
                          const std::vector<const Node*>& q_nodes) const {
  if (!CheckQDQNodes(graph, node, dq_nodes, q_nodes, 1)) {
    return false;
  }

  int32_t dt_input = dq_nodes[0]->InputDefs()[0]->TypeAsProto()->tensor_type().elem_type();
  int32_t dt_output = q_nodes[0]->OutputDefs()[0]->TypeAsProto()->tensor_type().elem_type();

  return ((dt_input == ONNX_NAMESPACE::TensorProto_DataType::TensorProto_DataType_UINT8 ||
           (int8_allowed_ && dt_input == ONNX_NAMESPACE::TensorProto_DataType::TensorProto_DataType_INT8))) &&
         ((dt_output == ONNX_NAMESPACE::TensorProto_DataType::TensorProto_DataType_UINT8 ||
           (int8_allowed_ && dt_output == ONNX_NAMESPACE::TensorProto_DataType::TensorProto_DataType_INT8)));
}

bool BinarySelector::Check(const Graph& graph,
                           const Node& node,
                           const std::vector<const Node*>& dq_nodes,
                           const std::vector<const Node*>& q_nodes) const {
  if (!CheckQDQNodes(graph, node, dq_nodes, q_nodes)) {
    return false;
  }

  // Currently QLinearConv only support activation type uint8_t
  int32_t dt_input_1 = dq_nodes[0]->InputDefs()[0]->TypeAsProto()->tensor_type().elem_type();
  int32_t dt_input_2 = dq_nodes[1]->InputDefs()[0]->TypeAsProto()->tensor_type().elem_type();
  int32_t dt_output = q_nodes[0]->OutputDefs()[0]->TypeAsProto()->tensor_type().elem_type();
  return dt_input_1 == dt_input_2 &&
         dt_input_1 == dt_output;
}

bool VariadicSelector::Check(const Graph& graph,
                             const Node& node,
                             const std::vector<const Node*>& dq_nodes,
                             const std::vector<const Node*>& q_nodes) const {
  if (!CheckQDQNodes(graph, node, dq_nodes, q_nodes)) {
    return false;
  }

  // All DQs' inputs and Q's output should have same data type
  int32_t dt_input = dq_nodes[0]->InputDefs()[0]->TypeAsProto()->tensor_type().elem_type();
  for (size_t dq_idx = 1; dq_idx < dq_nodes.size(); dq_idx++) {
    if (dt_input != dq_nodes[dq_idx]->InputDefs()[0]->TypeAsProto()->tensor_type().elem_type()) {
      return false;
    }
  }

  int32_t dt_output = q_nodes[0]->OutputDefs()[0]->TypeAsProto()->tensor_type().elem_type();
  return dt_input == dt_output;
}

void VariadicSelector::UpdateBuilder(NodesToOptimizeBuilder& builder) const {
  builder.num_input_defs = 1;  // set to 1 as the first input is variadic
}

bool ConvSelector::Check(const Graph& graph,
                         const Node& node,
                         const std::vector<const Node*>& dq_nodes,
                         const std::vector<const Node*>& q_nodes) const {
  if (!CheckQDQNodes(graph, node, dq_nodes, q_nodes)) {
    return false;
  }

  // Currently QLinearConv only support activation type uint8_t and output type uint8_t
  int32_t dt_input = dq_nodes[0]->InputDefs()[0]->TypeAsProto()->tensor_type().elem_type();
  int32_t dt_output = q_nodes[0]->OutputDefs()[0]->TypeAsProto()->tensor_type().elem_type();
  if (dt_input != ONNX_NAMESPACE::TensorProto_DataType::TensorProto_DataType_UINT8 ||
      dt_output != ONNX_NAMESPACE::TensorProto_DataType::TensorProto_DataType_UINT8) {
    return false;
  }

  if (dq_nodes.size() < 3) {  // no bias
    return true;
  }

  int32_t dt_bias = dq_nodes[2]->InputDefs()[0]->TypeAsProto()->tensor_type().elem_type();
  return dt_bias == ONNX_NAMESPACE::TensorProto_DataType::TensorProto_DataType_INT32;
}

void ConvSelector::UpdateBuilder(NodesToOptimizeBuilder& builder) const {
  builder.input_nodes.resize(3);  // add nullptr for bias if missing
}

bool MatMulSelector::Check(const Graph& graph,
                           const Node& node,
                           const std::vector<const Node*>& dq_nodes,
                           const std::vector<const Node*>& q_nodes) const {
  if (dq_nodes.size() != 2) {
    return false;
  }

  // potential match for QLinearMatMul or MatMulIntegerToFloat
  bool qlinear = !q_nodes.empty();

  if (qlinear) {
    // check for QLinearMatMul
    if (!CheckQDQNodes(graph, node, dq_nodes, q_nodes)) {
      return false;
    }

    int32_t dt_output = q_nodes[0]->OutputDefs()[0]->TypeAsProto()->tensor_type().elem_type();
    if (dt_output != ONNX_NAMESPACE::TensorProto_DataType::TensorProto_DataType_UINT8) {
      return false;
    }
  } else {
    // MatMulIntegerToFloat has no additional constraints to check
  }

  // Currently Quant MatMul only support activation type uint8_t
  int32_t dt_input = dq_nodes[0]->InputDefs()[0]->TypeAsProto()->tensor_type().elem_type();
  return (dt_input == ONNX_NAMESPACE::TensorProto_DataType::TensorProto_DataType_UINT8);
}

}  // namespace QDQ
}  // namespace onnxruntime

#endif  // !defined(ORT_MINIMAL_BUILD)