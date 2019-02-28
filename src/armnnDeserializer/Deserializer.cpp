//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "Deserializer.hpp"

#include <armnn/ArmNN.hpp>
#include <armnn/Exceptions.hpp>

#include <ParserHelper.hpp>
#include <Permute.hpp>
#include <VerificationHelpers.hpp>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/assert.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

// The generated code based on the Serialize schema:
#include <ArmnnSchema_generated.h>

#include <fstream>
#include <algorithm>
#include <limits>
#include <numeric>

using armnn::ParseException;
using namespace armnn;
using namespace armnnSerializer;

namespace armnnDeserializer
{

namespace
{

const uint32_t VIRTUAL_LAYER_ID = std::numeric_limits<uint32_t>::max();

 void CheckGraph(const Deserializer::GraphPtr& graph,
                 unsigned int layersIndex,
                 const CheckLocation& location)
{
    if (graph->layers() == nullptr)
    {
        throw ParseException(
                boost::str(
                        boost::format("%1% was called with invalid (null) graph. "
                                      "Possible reason is that the graph is not yet loaded and Unpack(ed). "
                                      "layers:%2% at %3%") %
                        location.m_Function %
                        layersIndex %
                        location.FileLine()));
    }
    else if (layersIndex >= graph->layers()->size())
    {
        throw ParseException(
                boost::str(
                        boost::format("%1% was called with an invalid layers index. "
                                      "layers:%2% at %3%") %
                        location.m_Function %
                        layersIndex %
                        location.FileLine()));
    }
}

void CheckLayers(const Deserializer::GraphPtr& graph,
                 unsigned int layersIndex,
                 unsigned int layerIndex,
                 const CheckLocation& location)
{
    if (graph->layers() == nullptr)
    {
        throw ParseException(
            boost::str(
                boost::format("%1% was called with invalid (null) graph. "
                              "Possible reason is that the graph is not yet loaded and Unpack(ed). "
                              "layers:%2% at %3%") %
                location.m_Function %
                layersIndex %
                location.FileLine()));
    }
    else if (layersIndex >= graph->layers()->size())
    {
        throw ParseException(
            boost::str(
                boost::format("%1% was called with an invalid layers index. "
                              "layers:%2% at %3%") %
                location.m_Function %
                layersIndex %
                location.FileLine()));
    }
    else if (layerIndex >= graph->layers()[layersIndex].size()
            && layerIndex != VIRTUAL_LAYER_ID)
    {
        throw ParseException(
            boost::str(
                boost::format("%1% was called with an invalid layer index. "
                              "layers:%2% layer:%3% at %4%") %
                location.m_Function %
                layersIndex %
                layerIndex %
                location.FileLine()));
    }
}

void CheckTensorPtr(Deserializer::TensorRawPtr rawPtr,
                    const CheckLocation& location)
{
    if (rawPtr == nullptr)
    {
        throw ParseException(
            boost::str(
                boost::format("%1% was called with a null tensor pointer. "
                              "at %2%") %
                location.m_Function %
                location.FileLine()));

    }
}

void CheckConstTensorPtr(Deserializer::ConstTensorRawPtr rawPtr,
                         const CheckLocation& location)
{
    if (rawPtr == nullptr)
    {
        throw ParseException(boost::str(boost::format("%1% was called with a null const tensor pointer. at %2%") %
                                        location.m_Function %
                                        location.FileLine()));
    }
}

void CheckConstTensorSize(const unsigned int constTensorSize,
                          const unsigned int tensorSize,
                          const CheckLocation& location)
{
    if (constTensorSize != tensorSize)
    {
        throw ParseException(boost::str(boost::format("%1% wrong number of components supplied to tensor. at:%2%") %
                                                      location.m_Function %
                                                      location.FileLine()));
    }
}

#define CHECK_TENSOR_PTR(TENSOR_PTR) \
    CheckTensorPtr(TENSOR_PTR, CHECK_LOCATION())

#define CHECK_CONST_TENSOR_SIZE(CONST_TENSOR_SIZE, TENSOR_SIZE) \
    CheckConstTensorSize(CONST_TENSOR_SIZE, TENSOR_SIZE, CHECK_LOCATION())

#define CHECK_CONST_TENSOR_PTR(TENSOR_PTR) \
    CheckConstTensorPtr(TENSOR_PTR, CHECK_LOCATION())

#define CHECK_LAYERS(GRAPH, LAYERS_INDEX, LAYER_INDEX) \
    CheckLayers(GRAPH, LAYERS_INDEX, LAYER_INDEX, CHECK_LOCATION())

#define CHECK_GRAPH(GRAPH, LAYERS_INDEX) \
    CheckGraph(GRAPH, LAYERS_INDEX, CHECK_LOCATION())
}

bool CheckShape(const armnn::TensorShape& actual, const std::vector<uint32_t>& expected)
{
    const unsigned int actualSize = actual.GetNumDimensions();
    if (actualSize != expected.size())
    {
        return false;
    }

    for (unsigned int i = 0u; i < actualSize; i++)
    {
        if (actual[i] != static_cast<unsigned int>(expected[i]))
        {
            return false;
        }
    }

    return true;
}

Deserializer::Deserializer()
: m_Network(nullptr, nullptr),
//May require LayerType_Max to be included
m_ParserFunctions(Layer_MAX+1, &Deserializer::ParseUnsupportedLayer)
{
    // register supported layers
    m_ParserFunctions[Layer_ActivationLayer]             = &Deserializer::ParseActivation;
    m_ParserFunctions[Layer_AdditionLayer]               = &Deserializer::ParseAdd;
    m_ParserFunctions[Layer_BatchToSpaceNdLayer]         = &Deserializer::ParseBatchToSpaceNd;
    m_ParserFunctions[Layer_BatchNormalizationLayer]     = &Deserializer::ParseBatchNormalization;
    m_ParserFunctions[Layer_ConstantLayer]               = &Deserializer::ParseConstant;
    m_ParserFunctions[Layer_Convolution2dLayer]          = &Deserializer::ParseConvolution2d;
    m_ParserFunctions[Layer_DepthwiseConvolution2dLayer] = &Deserializer::ParseDepthwiseConvolution2d;
    m_ParserFunctions[Layer_DivisionLayer]               = &Deserializer::ParseDivision;
    m_ParserFunctions[Layer_EqualLayer]                  = &Deserializer::ParseEqual;
    m_ParserFunctions[Layer_FullyConnectedLayer]         = &Deserializer::ParseFullyConnected;
    m_ParserFunctions[Layer_FloorLayer]                  = &Deserializer::ParseFloor;
    m_ParserFunctions[Layer_GatherLayer]                 = &Deserializer::ParseGather;
    m_ParserFunctions[Layer_GreaterLayer]                = &Deserializer::ParseGreater;
    m_ParserFunctions[Layer_MaximumLayer]                = &Deserializer::ParseMaximum;
    m_ParserFunctions[Layer_MeanLayer]                   = &Deserializer::ParseMean;
    m_ParserFunctions[Layer_MinimumLayer]                = &Deserializer::ParseMinimum;
    m_ParserFunctions[Layer_MergerLayer]                 = &Deserializer::ParseMerger;
    m_ParserFunctions[Layer_MultiplicationLayer]         = &Deserializer::ParseMultiplication;
    m_ParserFunctions[Layer_NormalizationLayer]          = &Deserializer::ParseNormalization;
    m_ParserFunctions[Layer_PadLayer]                    = &Deserializer::ParsePad;
    m_ParserFunctions[Layer_PermuteLayer]                = &Deserializer::ParsePermute;
    m_ParserFunctions[Layer_Pooling2dLayer]              = &Deserializer::ParsePooling2d;
    m_ParserFunctions[Layer_ReshapeLayer]                = &Deserializer::ParseReshape;
    m_ParserFunctions[Layer_ResizeBilinearLayer]         = &Deserializer::ParseResizeBilinear;
    m_ParserFunctions[Layer_RsqrtLayer]                  = &Deserializer::ParseRsqrt;
    m_ParserFunctions[Layer_SoftmaxLayer]                = &Deserializer::ParseSoftmax;
    m_ParserFunctions[Layer_SpaceToBatchNdLayer]         = &Deserializer::ParseSpaceToBatchNd;
    m_ParserFunctions[Layer_StridedSliceLayer]           = &Deserializer::ParseStridedSlice;
    m_ParserFunctions[Layer_SubtractionLayer]            = &Deserializer::ParseSubtraction;
}

Deserializer::LayerBaseRawPtr Deserializer::GetBaseLayer(const GraphPtr& graphPtr, unsigned int layerIndex)
{
    auto layerType = graphPtr->layers()->Get(layerIndex)->layer_type();

    switch(layerType)
    {
        case Layer::Layer_ActivationLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_ActivationLayer()->base();
        case Layer::Layer_AdditionLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_AdditionLayer()->base();
        case Layer::Layer_BatchToSpaceNdLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_BatchToSpaceNdLayer()->base();
        case Layer::Layer_BatchNormalizationLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_BatchNormalizationLayer()->base();
        case Layer::Layer_ConstantLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_ConstantLayer()->base();
        case Layer::Layer_Convolution2dLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_Convolution2dLayer()->base();
        case Layer::Layer_DepthwiseConvolution2dLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_DepthwiseConvolution2dLayer()->base();
        case Layer::Layer_DivisionLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_DivisionLayer()->base();
        case Layer::Layer_EqualLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_EqualLayer()->base();
        case Layer::Layer_FullyConnectedLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_FullyConnectedLayer()->base();
        case Layer::Layer_FloorLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_FloorLayer()->base();
        case Layer::Layer_GatherLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_GatherLayer()->base();
        case Layer::Layer_GreaterLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_GreaterLayer()->base();
        case Layer::Layer_InputLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_InputLayer()->base()->base();
        case Layer::Layer_MeanLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_MeanLayer()->base();
        case Layer::Layer_MinimumLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_MinimumLayer()->base();
        case Layer::Layer_MaximumLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_MaximumLayer()->base();
        case Layer::Layer_MergerLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_MergerLayer()->base();
        case Layer::Layer_MultiplicationLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_MultiplicationLayer()->base();
        case Layer::Layer_NormalizationLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_NormalizationLayer()->base();
        case Layer::Layer_OutputLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_OutputLayer()->base()->base();
        case Layer::Layer_PadLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_PadLayer()->base();
        case Layer::Layer_PermuteLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_PermuteLayer()->base();
        case Layer::Layer_Pooling2dLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_Pooling2dLayer()->base();
        case Layer::Layer_ReshapeLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_ReshapeLayer()->base();
        case Layer::Layer_ResizeBilinearLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_ResizeBilinearLayer()->base();
        case Layer::Layer_RsqrtLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_RsqrtLayer()->base();
        case Layer::Layer_SoftmaxLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_SoftmaxLayer()->base();
        case Layer::Layer_SpaceToBatchNdLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_SpaceToBatchNdLayer()->base();
        case Layer::Layer_StridedSliceLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_StridedSliceLayer()->base();
        case Layer::Layer_SubtractionLayer:
            return graphPtr->layers()->Get(layerIndex)->layer_as_SubtractionLayer()->base();
        case Layer::Layer_NONE:
        default:
            throw ParseException(boost::str(
                  boost::format("Layer must have a type %1%") %
                  Layer::Layer_NONE));
    }
}

std::string Deserializer::GetLayerName(const GraphPtr& graph, unsigned int index)
{
    auto layer = GetBaseLayer(graph, index);
    assert(layer);
    return layer->layerName()->str();
}

int32_t Deserializer::GetBindingLayerInfo(const GraphPtr& graphPtr, unsigned int layerIndex)
{
    auto layerType = graphPtr->layers()->Get(layerIndex)->layer_type();

    if (layerType == Layer::Layer_InputLayer)
    {
        return graphPtr->layers()->Get(layerIndex)->layer_as_InputLayer()->base()->layerBindingId();
    }
    else if ( layerType == Layer::Layer_OutputLayer )
    {
        return graphPtr->layers()->Get(layerIndex)->layer_as_OutputLayer()->base()->layerBindingId();
    }
    return 0;
}

armnn::DataLayout ToDataLayout(armnnSerializer::DataLayout dataLayout)
{
    switch (dataLayout)
    {
        case armnnSerializer::DataLayout::DataLayout_NHWC:
            return armnn::DataLayout::NHWC;
        case armnnSerializer::DataLayout::DataLayout_NCHW:
        default:
            return armnn::DataLayout::NCHW;
    }
}

armnn::ActivationFunction ToActivationFunction(armnnSerializer::ActivationFunction function)
{
    switch (function)
    {
        case armnnSerializer::ActivationFunction_Sigmoid:
            return armnn::ActivationFunction::Sigmoid;
        case armnnSerializer::ActivationFunction_TanH:
            return armnn::ActivationFunction::TanH;
        case armnnSerializer::ActivationFunction_Linear:
            return armnn::ActivationFunction::Linear;
        case armnnSerializer::ActivationFunction_ReLu:
            return armnn::ActivationFunction::ReLu;
        case armnnSerializer::ActivationFunction_BoundedReLu:
            return armnn::ActivationFunction::BoundedReLu;
        case armnnSerializer::ActivationFunction_LeakyReLu:
            return armnn::ActivationFunction::LeakyReLu;
        case armnnSerializer::ActivationFunction_Abs:
            return armnn::ActivationFunction::Abs;
        case armnnSerializer::ActivationFunction_Sqrt:
            return armnn::ActivationFunction::Sqrt;
        case armnnSerializer::ActivationFunction_Square:
            return armnn::ActivationFunction::Square;
        default:
            return armnn::ActivationFunction::Sigmoid;
    }
}

armnn::TensorInfo ToTensorInfo(Deserializer::TensorRawPtr tensorPtr)
{
    armnn::DataType type;
    CHECK_TENSOR_PTR(tensorPtr);

    switch (tensorPtr->dataType())
    {
        case DataType_QuantisedAsymm8:
            type = armnn::DataType::QuantisedAsymm8;
            break;
        case DataType_Signed32:
            type = armnn::DataType::Signed32;
            break;
        case DataType_Float32:
            type = armnn::DataType::Float32;
            break;
        case DataType_Float16:
            type = armnn::DataType::Float16;
            break;
        case DataType_Boolean:
            type = armnn::DataType::Boolean;
            break;
        default:
        {
            CheckLocation location = CHECK_LOCATION();
            throw ParseException(
                    boost::str(
                            boost::format("Unsupported data type %1% = %2%. %3%") %
                            tensorPtr->dataType() %
                            EnumNameDataType(tensorPtr->dataType()) %
                            location.AsString()));
        }
    }
    float quantizationScale = tensorPtr->quantizationScale();
    int32_t quantizationOffset = tensorPtr->quantizationOffset();

    auto dimensions = tensorPtr->dimensions();
    unsigned int size = dimensions->size();
    std::vector<unsigned int> outputDims(dimensions->begin(), dimensions->begin() + size);

    // two statements (on purpose) for easier debugging:
    armnn::TensorInfo result(size,
                             outputDims.data(),
                             type,
                             quantizationScale,
                             quantizationOffset);
    return result;
}

armnn::ConstTensor ToConstTensor(Deserializer::ConstTensorRawPtr constTensorPtr)
{
    CHECK_CONST_TENSOR_PTR(constTensorPtr);
    armnn::TensorInfo tensorInfo = ToTensorInfo(constTensorPtr->info());

    switch (constTensorPtr->data_type())
    {
        case ConstTensorData_ByteData:
        {
            auto byteData = constTensorPtr->data_as_ByteData()->data();
            CHECK_CONST_TENSOR_SIZE(byteData->size(), tensorInfo.GetNumElements());
            return armnn::ConstTensor(tensorInfo, byteData->data());
        }
        case ConstTensorData_ShortData:
        {
            auto shortData = constTensorPtr->data_as_ShortData()->data();
            CHECK_CONST_TENSOR_SIZE(shortData->size(), tensorInfo.GetNumElements());
            return armnn::ConstTensor(tensorInfo, shortData->data());
        }
        case ConstTensorData_IntData:
        {
            auto intData = constTensorPtr->data_as_IntData()->data();
            CHECK_CONST_TENSOR_SIZE(intData->size(), tensorInfo.GetNumElements());
            return armnn::ConstTensor(tensorInfo, intData->data());
        }
        case ConstTensorData_LongData:
        {
            auto longData = constTensorPtr->data_as_LongData()->data();
            CHECK_CONST_TENSOR_SIZE(longData->size(), tensorInfo.GetNumElements());
            return armnn::ConstTensor(tensorInfo, longData->data());
        }
        default:
        {
            CheckLocation location = CHECK_LOCATION();
            throw ParseException(
                    boost::str(boost::format("Unsupported data type %1% = %2%. %3%") %
                               constTensorPtr->data_type() %
                               EnumNameConstTensorData(constTensorPtr->data_type()) %
                               location.AsString()));
        }
    }
}

Deserializer::LayerBaseRawPtrVector Deserializer::GetGraphInputs(const GraphPtr& graphPtr)
{

    CHECK_GRAPH(graphPtr, 0);
    const auto& numInputs = graphPtr->inputIds()->size();

    LayerBaseRawPtrVector result(numInputs);

    for (unsigned int i=0; i<numInputs; ++i)
    {
        uint32_t inputId = graphPtr->inputIds()->Get(i);
        result[i] = GetBaseLayer(graphPtr, static_cast<uint32_t>(inputId));
    }
    return result;
}

Deserializer::LayerBaseRawPtrVector Deserializer::GetGraphOutputs(const GraphPtr& graphPtr)
{
    CHECK_GRAPH(graphPtr, 0);
    const auto& numOutputs = graphPtr->outputIds()->size();
    LayerBaseRawPtrVector result(numOutputs);

    for (unsigned int i=0; i<numOutputs; ++i)
    {
        uint32_t outputId = graphPtr->outputIds()->Get(i);

        result[i] = GetBaseLayer(graphPtr, static_cast<uint32_t>(outputId));
    }
    return result;
}

Deserializer::TensorRawPtrVector Deserializer::GetInputs(const GraphPtr& graphPtr,
                                                         unsigned int layerIndex)
{
    CHECK_LAYERS(graphPtr, 0, layerIndex);
    auto layer = GetBaseLayer(graphPtr, layerIndex);
    const auto& numInputs = layer->inputSlots()->size();

    TensorRawPtrVector result(numInputs);

   for (unsigned int i=0; i<numInputs; ++i)
   {
       auto inputId = CHECKED_NON_NEGATIVE(static_cast<int32_t>
                                          (layer->inputSlots()->Get(i)->connection()->sourceLayerIndex()));
       result[i] = GetBaseLayer(graphPtr, inputId)->outputSlots()->Get(0)->tensorInfo();
   }
   return result;
}

Deserializer::TensorRawPtrVector Deserializer::GetOutputs(const GraphPtr& graphPtr,
                                                                    unsigned int layerIndex)
{
    CHECK_LAYERS(graphPtr, 0, layerIndex);
    auto layer = GetBaseLayer(graphPtr, layerIndex);
    const auto& numOutputs = layer->outputSlots()->size();

    TensorRawPtrVector result(numOutputs);

    for (unsigned int i=0; i<numOutputs; ++i)
    {
        result[i] = layer->outputSlots()->Get(i)->tensorInfo();
    }
    return result;
}

void Deserializer::ParseUnsupportedLayer(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    const auto layerName = GetBaseLayer(graph, layerIndex)->layerName()->c_str();
    throw ParseException(
        boost::str(
            boost::format("Layer not supported. "
                          "layerIndex: %1% "
                          "layerName: %2% / %3%") %
            layerIndex %
            layerName %
            CHECK_LOCATION().AsString()));
}

void Deserializer::ResetParser()
{
    m_Network = armnn::INetworkPtr(nullptr, nullptr);
    m_InputBindings.clear();
    m_OutputBindings.clear();
}

IDeserializer* IDeserializer::CreateRaw()
{
    return new Deserializer();
}

IDeserializerPtr IDeserializer::Create()
{
    return IDeserializerPtr(CreateRaw(), &IDeserializer::Destroy);
}

void IDeserializer::Destroy(IDeserializer* parser)
{
    delete parser;
}

INetworkPtr Deserializer::CreateNetworkFromBinary(const std::vector<uint8_t>& binaryContent)
{
     ResetParser();
     GraphPtr graph = LoadGraphFromBinary(binaryContent.data(), binaryContent.size());
     return CreateNetworkFromGraph(graph);
}

armnn::INetworkPtr Deserializer::CreateNetworkFromBinary(std::istream& binaryContent)
{
    ResetParser();
    std::vector<uint8_t> content((std::istreambuf_iterator<char>(binaryContent)), std::istreambuf_iterator<char>());
    GraphPtr graph = LoadGraphFromBinary(content.data(), content.size());
    return CreateNetworkFromGraph(graph);
}

Deserializer::GraphPtr Deserializer::LoadGraphFromBinary(const uint8_t* binaryContent, size_t len)
{
    if (binaryContent == nullptr)
    {
        throw InvalidArgumentException(boost::str(boost::format("Invalid (null) binary content %1%") %
                                                  CHECK_LOCATION().AsString()));
    }
    flatbuffers::Verifier verifier(binaryContent, len);
    if (verifier.VerifyBuffer<SerializedGraph>() == false)
    {
        throw ParseException(
                boost::str(boost::format("Buffer doesn't conform to the expected Armnn "
                                         "flatbuffers format. size:%1% %2%") %
                           len %
                           CHECK_LOCATION().AsString()));
    }
    return GetSerializedGraph(binaryContent);
}

INetworkPtr Deserializer::CreateNetworkFromGraph(GraphPtr graph)
{
    m_Network = INetwork::Create();
    BOOST_ASSERT(graph != nullptr);
    unsigned int layerIndex = 0;
    m_GraphConnections.emplace_back(graph->layers()->size());
    for (AnyLayer const* layer : *graph->layers())
    {
        if (layer->layer_type() != Layer_InputLayer &&
            layer->layer_type() != Layer_OutputLayer)
        {
            // lookup and call the parser function
            auto& parserFunction = m_ParserFunctions[layer->layer_type()];
            (this->*parserFunction)(graph, layerIndex);
        }
        ++layerIndex;
    }

    SetupInputLayers(graph);
    SetupOutputLayers(graph);

    // establish the connections from the layer outputs to the inputs of the subsequent layers
    for (size_t connectionsIndex = 0; connectionsIndex < m_GraphConnections[0].size(); ++connectionsIndex)
    {
        SlotsMap& slotsMap = m_GraphConnections[0][connectionsIndex];
        for (unsigned int outputSlotIndex = 0; outputSlotIndex < slotsMap.outputSlots.size(); outputSlotIndex++)
        {
            if (slotsMap.inputSlots.find(outputSlotIndex) != slotsMap.inputSlots.end())
            {
                for (armnn::IInputSlot* inputSlot : slotsMap.inputSlots[outputSlotIndex])
                {
                    slotsMap.outputSlots[outputSlotIndex]->Connect(*inputSlot);
                }
            }
        }
    }

    return std::move(m_Network);
}

BindingPointInfo Deserializer::GetNetworkInputBindingInfo(unsigned int layerIndex,
                                                          const std::string& name) const
{
    for (auto inputBinding : m_InputBindings)
    {
        if (inputBinding.first == name)
        {
            return inputBinding.second;
        }
    }
    throw ParseException(
            boost::str(
                    boost::format("No input binding found for layer:%1% / %2%") %
                    name %
                    CHECK_LOCATION().AsString()));
}

BindingPointInfo Deserializer::GetNetworkOutputBindingInfo(unsigned int layerIndex,
                                                                const std::string& name) const
{
    for (auto outputBinding : m_OutputBindings)
    {
        if (outputBinding.first == name)
        {
            return outputBinding.second;
        }
    }
    throw ParseException(
        boost::str(
            boost::format("No output binding found for layer:%1% / %2%") %
            name %
            CHECK_LOCATION().AsString()));
}

void Deserializer::SetupInputLayers(GraphPtr graph)
{
    CHECK_GRAPH(graph, 0);
    auto inputs = GetGraphInputs(graph);
    m_InputBindings.clear();
    m_InputBindings.reserve(inputs.size());
    for (auto const& input : inputs)
    {
        LayerBindingId bindingId = GetBindingLayerInfo(graph, input->index());
        IConnectableLayer* layer =
            m_Network->AddInputLayer(bindingId, input->layerName()->c_str());

        auto tensorInfo = ToTensorInfo(input->outputSlots()->Get(0)->tensorInfo());
        layer->GetOutputSlot(0).SetTensorInfo(tensorInfo);

        RegisterOutputSlots(graph, input->index(), layer);

        BOOST_ASSERT_MSG(input->layerName()->c_str(), "Input has no name.");
        BindingPointInfo bindingInfo = {bindingId, tensorInfo};
        m_InputBindings.push_back(std::make_pair(input->layerName()->c_str(), bindingInfo));
    }
}

void Deserializer::SetupOutputLayers(GraphPtr graph)
{
    CHECK_GRAPH(graph, 0);
    auto outputs = GetGraphOutputs(graph);
    m_OutputBindings.clear();
    m_OutputBindings.reserve(outputs.size());
    for (auto const& output : outputs)
    {
        LayerBindingId bindingId = GetBindingLayerInfo(graph, output->index());
        IConnectableLayer* layer =
            m_Network->AddOutputLayer(bindingId, output->layerName()->c_str());

        RegisterInputSlots(graph, output->index(), layer);

        auto baseLayer = GetBaseLayer(graph, output->index());
        auto sourceLayerIndex = baseLayer->inputSlots()->Get(0)->connection()->sourceLayerIndex();
        auto sourceLayer = GetBaseLayer(graph, sourceLayerIndex);
        auto tensorInfo = ToTensorInfo(sourceLayer->outputSlots()->Get(0)->tensorInfo());

        BOOST_ASSERT_MSG(output->layerName()->c_str(), "Output has no name.");
        BindingPointInfo bindingInfo = {bindingId, tensorInfo};
        m_OutputBindings.push_back(std::make_pair(output->layerName()->c_str(), bindingInfo));
    }
}

void Deserializer::RegisterOutputSlots(GraphPtr graph,
                                       uint32_t layerIndex,
                                       IConnectableLayer* layer)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    BOOST_ASSERT(layer != nullptr);
    auto parsedLayer = GetBaseLayer(graph, layerIndex);
    if (parsedLayer->outputSlots()->size() != layer->GetNumOutputSlots())
    {
        throw ParseException(
            boost::str(boost::format("The number of outputslots (%1%) does not match the number expected (%2%)"
                                     " for layer index: %3% %4%") %
                       parsedLayer->outputSlots()->size() %
                       layer->GetNumOutputSlots() %
                       layerIndex %
                       CHECK_LOCATION().AsString()));
    }

    for (unsigned int slotIndex = 0; slotIndex < layer->GetNumOutputSlots(); ++slotIndex)
    {
        armnn::IOutputSlot* slot = &(layer->GetOutputSlot(slotIndex));
        RegisterOutputSlotOfConnection(layerIndex, slot);
    }
}

void Deserializer::RegisterInputSlots(GraphPtr graph,
                                      uint32_t layerIndex,
                                      armnn::IConnectableLayer* layer)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    BOOST_ASSERT(layer != nullptr);
    auto parsedLayer = GetBaseLayer(graph, layerIndex);
    if (parsedLayer->inputSlots()->size() != layer->GetNumInputSlots())
    {
        throw ParseException(
            boost::str(boost::format("The number of inputslots (%1%) does not match the number expected (%2%)"
                                     " for layer index:%3% %4%") %
                       parsedLayer->inputSlots()->size() %
                       layer->GetNumInputSlots() %
                       layerIndex %
                       CHECK_LOCATION().AsString()));
    }

    for (unsigned int slotIndex = 0; slotIndex < layer->GetNumInputSlots(); ++slotIndex)
    {
        auto fbConnection = parsedLayer->inputSlots()->Get(slotIndex)->connection();
        armnn::IInputSlot* slot = &(layer->GetInputSlot(slotIndex));

        RegisterInputSlotOfConnection(fbConnection->sourceLayerIndex(), fbConnection->outputSlotIndex(), slot);
    }
}

void Deserializer::RegisterInputSlotOfConnection(uint32_t sourceLayerIndex,
                                                 uint32_t outputSlotIndex,
                                                 armnn::IInputSlot* slot)
{
    BOOST_ASSERT(m_GraphConnections[0].size() > sourceLayerIndex);

    SlotsMap& slotsMap = m_GraphConnections[0][sourceLayerIndex];
    if (slotsMap.inputSlots.find(outputSlotIndex) == slotsMap.inputSlots.end())
    {
        slotsMap.inputSlots[outputSlotIndex] = {slot};
    }
    else
    {
        slotsMap.inputSlots[outputSlotIndex].push_back(slot);
    }
}

void Deserializer::RegisterOutputSlotOfConnection(uint32_t sourceLayerIndex,
                                                  armnn::IOutputSlot* slot)
{
    BOOST_ASSERT(m_GraphConnections[0].size() > sourceLayerIndex);
    m_GraphConnections[0][sourceLayerIndex].outputSlots.push_back(slot);
}

void Deserializer::ParseActivation(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    auto inputs = GetInputs(graph, layerIndex);
    CHECK_LOCATION();
    CHECK_VALID_SIZE(inputs.size(), 1);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto serializerLayer = graph->layers()->Get(layerIndex)->layer_as_ActivationLayer();
    auto layerName = GetLayerName(graph, layerIndex);
    auto serializerDescriptor = serializerLayer->descriptor();

    armnn::ActivationDescriptor descriptor;
    descriptor.m_Function = ToActivationFunction(serializerDescriptor->function());
    descriptor.m_A = serializerDescriptor->a();
    descriptor.m_B = serializerDescriptor->b();

    IConnectableLayer* layer = m_Network->AddActivationLayer(descriptor,
                                                             layerName.c_str());
    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseAdd(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    auto inputs = GetInputs(graph, layerIndex);
    CHECK_LOCATION();
    CHECK_VALID_SIZE(inputs.size(), 2);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddAdditionLayer(layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseBatchToSpaceNd(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);

    Deserializer::TensorRawPtrVector inputs = GetInputs(graph, layerIndex);
    CHECK_VALID_SIZE(inputs.size(), 1);

    Deserializer::TensorRawPtrVector outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto flatBufferDescriptor = graph->layers()->Get(layerIndex)->layer_as_BatchToSpaceNdLayer()->descriptor();
    auto flatBufferCrops = flatBufferDescriptor->crops();
    auto flatBufferBlockShape = flatBufferDescriptor->blockShape();

    if (flatBufferCrops->Length() % 2 != 0)
    {
        throw ParseException(boost::str(
            boost::format("The size of crops must be divisible by 2 %1%") % CHECK_LOCATION().AsString()));
    }

    std::vector<std::pair<unsigned int, unsigned int>> crops;
    crops.reserve(flatBufferCrops->Length() / 2);
    for (unsigned int i = 0; i < flatBufferCrops->Length() - 1; i += 2)
    {
        crops.emplace_back(flatBufferCrops->Get(i), flatBufferCrops->Get(i+1));
    }

    armnn::BatchToSpaceNdDescriptor descriptor;
    descriptor.m_DataLayout = ToDataLayout(flatBufferDescriptor->dataLayout());
    descriptor.m_BlockShape =
        std::vector<unsigned int>(flatBufferBlockShape->begin(), flatBufferBlockShape->end());
    descriptor.m_Crops = crops;

    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddBatchToSpaceNdLayer(descriptor, layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseBatchNormalization(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);

    auto inputs = GetInputs(graph, layerIndex);
    CHECK_VALID_SIZE(inputs.size(), 1);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);
    auto outputInfo = ToTensorInfo(outputs[0]);

    auto layerName = GetLayerName(graph, layerIndex);

    auto serializerLayer = graph->layers()->Get(layerIndex)->layer_as_BatchNormalizationLayer();
    auto serializerDescriptor = serializerLayer->descriptor();

    armnn::BatchNormalizationDescriptor descriptor;
    descriptor.m_Eps = serializerDescriptor->eps();
    descriptor.m_DataLayout = ToDataLayout(serializerDescriptor->dataLayout());

    armnn::ConstTensor mean     = ToConstTensor(serializerLayer->mean());
    armnn::ConstTensor variance = ToConstTensor(serializerLayer->variance());
    armnn::ConstTensor beta     = ToConstTensor(serializerLayer->beta());
    armnn::ConstTensor gamma    = ToConstTensor(serializerLayer->gamma());

    IConnectableLayer* layer = m_Network->AddBatchNormalizationLayer(descriptor,
                                                                     mean,
                                                                     variance,
                                                                     beta,
                                                                     gamma,
                                                                     layerName.c_str());
    layer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseConstant(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    CHECK_LOCATION();

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto layerName = GetLayerName(graph, layerIndex);

    auto serializerLayer = graph->layers()->Get(layerIndex)->layer_as_ConstantLayer();
    auto serializerInput = serializerLayer->input();

    armnn::ConstTensor input = ToConstTensor(serializerInput);

    IConnectableLayer* layer = m_Network->AddConstantLayer(input, layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseConvolution2d(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    auto inputs = GetInputs(graph, layerIndex);
    CHECK_LOCATION();
    CHECK_VALID_SIZE(inputs.size(), 1);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto serializerLayer = graph->layers()->Get(layerIndex)->layer_as_Convolution2dLayer();
    auto layerName = GetLayerName(graph, layerIndex);
    auto serializerDescriptor = serializerLayer->descriptor();

    armnn::Convolution2dDescriptor descriptor;
    descriptor.m_PadLeft = serializerDescriptor->padLeft();
    descriptor.m_PadRight = serializerDescriptor->padRight();
    descriptor.m_PadTop = serializerDescriptor->padTop();
    descriptor.m_PadBottom = serializerDescriptor->padBottom();
    descriptor.m_StrideX = serializerDescriptor->strideX();
    descriptor.m_StrideY = serializerDescriptor->strideY();;
    descriptor.m_BiasEnabled = serializerDescriptor->biasEnabled();;
    descriptor.m_DataLayout = ToDataLayout(serializerDescriptor->dataLayout());

    armnn::ConstTensor weights = ToConstTensor(serializerLayer->weights());
    armnn::ConstTensor biases;

    if (descriptor.m_BiasEnabled)
    {
        biases = ToConstTensor(serializerLayer->biases());
    }
    IConnectableLayer* layer = m_Network->AddConvolution2dLayer(descriptor,
                                                                weights,
                                                                biases,
                                                                layerName.c_str());
    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseDepthwiseConvolution2d(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    auto inputs = GetInputs(graph, layerIndex);
    CHECK_LOCATION();
    CHECK_VALID_SIZE(inputs.size(), 1);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto serializerLayer = graph->layers()->Get(layerIndex)->layer_as_DepthwiseConvolution2dLayer();
    auto layerName = GetLayerName(graph, layerIndex);
    auto serializerDescriptor = serializerLayer->descriptor();

    armnn::DepthwiseConvolution2dDescriptor descriptor;
    descriptor.m_PadLeft     = serializerDescriptor->padLeft();
    descriptor.m_PadRight    = serializerDescriptor->padRight();
    descriptor.m_PadTop      = serializerDescriptor->padTop();
    descriptor.m_PadBottom   = serializerDescriptor->padBottom();
    descriptor.m_StrideX     = serializerDescriptor->strideX();
    descriptor.m_StrideY     = serializerDescriptor->strideY();;
    descriptor.m_BiasEnabled = serializerDescriptor->biasEnabled();;
    descriptor.m_DataLayout  = ToDataLayout(serializerDescriptor->dataLayout());

    armnn::ConstTensor weights = ToConstTensor(serializerLayer->weights());
    armnn::ConstTensor biases;

    if (descriptor.m_BiasEnabled)
    {
        biases = ToConstTensor(serializerLayer->biases());
    }
    IConnectableLayer* layer = m_Network->AddDepthwiseConvolution2dLayer(descriptor,
                                                                         weights,
                                                                         biases,
                                                                         layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseDivision(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    auto inputs = GetInputs(graph, layerIndex);
    CHECK_LOCATION();
    CHECK_VALID_SIZE(inputs.size(), 2);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddDivisionLayer(layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseEqual(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    auto inputs = GetInputs(graph, layerIndex);
    CHECK_LOCATION();
    CHECK_VALID_SIZE(inputs.size(), 2);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddEqualLayer(layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseGreater(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    auto inputs = GetInputs(graph, layerIndex);
    CHECK_LOCATION();
    CHECK_VALID_SIZE(inputs.size(), 2);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddGreaterLayer(layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseMinimum(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    auto inputs = GetInputs(graph, layerIndex);
    CHECK_LOCATION();
    CHECK_VALID_SIZE(inputs.size(), 2);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddMinimumLayer(layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseMaximum(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    auto inputs = GetInputs(graph, layerIndex);
    CHECK_LOCATION();
    CHECK_VALID_SIZE(inputs.size(), 2);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddMaximumLayer(layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseMerger(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    CHECK_LOCATION();

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto mergerLayer = graph->layers()->Get(layerIndex)->layer_as_MergerLayer();
    auto layerName = GetLayerName(graph, layerIndex);
    auto mergerDescriptor = mergerLayer->descriptor();
    unsigned int numViews = mergerDescriptor->numViews();
    unsigned int numDimensions = mergerDescriptor->numDimensions();

    // can now check the number of inputs == number of views
    auto inputs = GetInputs(graph, layerIndex);
    CHECK_VALID_SIZE(inputs.size(), numViews);

    armnn::OriginsDescriptor descriptor(numViews, numDimensions);
    auto originsPtr = mergerDescriptor->viewOrigins();
    for (unsigned int v = 0; v < numViews; ++v)
    {
        auto originPtr = originsPtr->Get(v);
        for (unsigned int d = 0; d < numDimensions; ++d)
        {
            uint32_t value = originPtr->data()->Get(d);
            descriptor.SetViewOriginCoord(v, d, value);
        }
    }
    descriptor.SetConcatAxis(mergerDescriptor->concatAxis());

    IConnectableLayer* layer = m_Network->AddMergerLayer(descriptor, layerName.c_str());
    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseMultiplication(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    auto inputs = GetInputs(graph, layerIndex);
    CHECK_LOCATION();
    CHECK_VALID_SIZE(inputs.size(), 2);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddMultiplicationLayer(layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseFloor(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    CHECK_LOCATION();

    auto inputs = GetInputs(graph, layerIndex);
    CHECK_VALID_SIZE(inputs.size(), 1);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto layerName = GetLayerName(graph, layerIndex);

    armnn::IConnectableLayer* layer;

    layer = m_Network->AddFloorLayer(layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseFullyConnected(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    auto inputs = GetInputs(graph, layerIndex);
    CHECK_LOCATION();
    CHECK_VALID_SIZE(inputs.size(), 1);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto flatBufferLayer = graph->layers()->Get(layerIndex)->layer_as_FullyConnectedLayer();
    auto layerName = GetLayerName(graph, layerIndex);
    auto flatBufferDescriptor = flatBufferLayer->descriptor();

    armnn::FullyConnectedDescriptor fullyConnectedDescriptor;
    fullyConnectedDescriptor.m_BiasEnabled = flatBufferDescriptor->biasEnabled();
    fullyConnectedDescriptor.m_TransposeWeightMatrix = flatBufferDescriptor->transposeWeightsMatrix();

    armnn::ConstTensor weightsTensor = ToConstTensor(flatBufferLayer->weights());

    armnn::IConnectableLayer* layer;
    if (flatBufferDescriptor->biasEnabled())
    {
        armnn::ConstTensor biasTensorData = ToConstTensor(flatBufferLayer->biases());
        layer = m_Network->AddFullyConnectedLayer(fullyConnectedDescriptor,
                                                  weightsTensor,
                                                  biasTensorData,
                                                  layerName.c_str());
    }
    else
    {
        layer = m_Network->AddFullyConnectedLayer(fullyConnectedDescriptor,
                                                  weightsTensor,
                                                  layerName.c_str());
    }

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParsePad(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);

    Deserializer::TensorRawPtrVector inputs = GetInputs(graph, layerIndex);
    CHECK_VALID_SIZE(inputs.size(), 1);

    Deserializer::TensorRawPtrVector outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto flatBufferDescriptor = graph->layers()->Get(layerIndex)->layer_as_PadLayer()->descriptor();
    auto flatBufferPadList = flatBufferDescriptor->padList();

    if (flatBufferPadList->Length() % 2 != 0)
    {
        throw ParseException(boost::str(
            boost::format("The size of the pad list must be divisible by 2 %1%") % CHECK_LOCATION().AsString()));
    }

    std::vector<std::pair<unsigned int, unsigned int>> padList;
    padList.reserve(flatBufferPadList->Length() / 2);
    for (unsigned int i = 0; i < flatBufferPadList->Length() - 1; i += 2)
    {
        padList.emplace_back(flatBufferPadList->Get(i), flatBufferPadList->Get(i+1));
    }

    armnn::PadDescriptor descriptor(padList);

    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddPadLayer(descriptor, layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParsePermute(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);

    auto dimsMapping =
    graph->layers()->Get(layerIndex)->layer_as_PermuteLayer()->descriptor()->dimMappings();

    auto inputs = GetInputs(graph, layerIndex);
    CHECK_VALID_SIZE(inputs.size(), 1);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);
    auto outputInfo = ToTensorInfo(outputs[0]);

    auto layerName = GetLayerName(graph, layerIndex);
    const armnn::PermuteDescriptor descriptor(armnn::PermutationVector(dimsMapping->data(), dimsMapping->Length()));

    IConnectableLayer* layer = m_Network->AddPermuteLayer(descriptor, layerName.c_str());
    layer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

armnn::Pooling2dDescriptor Deserializer::GetPoolingDescriptor(Deserializer::PoolingDescriptor pooling2dDesc,
                                                              unsigned int layerIndex)
{
    armnn::Pooling2dDescriptor desc;

    switch (pooling2dDesc->poolType())
    {
        case PoolingAlgorithm_Average:
        {
            desc.m_PoolType = armnn::PoolingAlgorithm::Average;
            break;
        }
        case PoolingAlgorithm_Max:
        {
            desc.m_PoolType = armnn::PoolingAlgorithm::Max;
            break;
        }
        default:
        {
            BOOST_ASSERT_MSG(false, "Unsupported pooling algorithm");
        }
    }

    switch (pooling2dDesc->outputShapeRounding())
    {
        case OutputShapeRounding_Floor:
        {
            desc.m_OutputShapeRounding = armnn::OutputShapeRounding::Floor;
            break;
        }
        case OutputShapeRounding_Ceiling:
        {
            desc.m_OutputShapeRounding = armnn::OutputShapeRounding::Ceiling;
            break;
        }
        default:
        {
            BOOST_ASSERT_MSG(false, "Unsupported output shape rounding");
        }
    }

    switch (pooling2dDesc->paddingMethod())
    {
        case PaddingMethod_Exclude:
        {
            desc.m_PaddingMethod = armnn::PaddingMethod::Exclude;
            break;
        }
        case PaddingMethod_IgnoreValue:
        {
            desc.m_PaddingMethod = armnn::PaddingMethod::IgnoreValue;
            break;
        }
        default:
        {
            BOOST_ASSERT_MSG(false, "Unsupported padding method");
        }
    }

    switch (pooling2dDesc->dataLayout())
    {
        case DataLayout_NCHW:
        {
            desc.m_DataLayout = armnn::DataLayout::NCHW;
            break;
        }
        case DataLayout_NHWC:
        {
            desc.m_DataLayout = armnn::DataLayout::NHWC;
            break;
        }
        default:
        {
            BOOST_ASSERT_MSG(false, "Unsupported data layout");
        }
    }

    desc.m_PadRight   = pooling2dDesc->padRight();
    desc.m_PadLeft    = pooling2dDesc->padLeft();
    desc.m_PadBottom  = pooling2dDesc->padBottom();
    desc.m_PadTop     = pooling2dDesc->padTop();
    desc.m_StrideX    = pooling2dDesc->strideX();
    desc.m_StrideY    = pooling2dDesc->strideY();
    desc.m_PoolWidth  = pooling2dDesc->poolWidth();
    desc.m_PoolHeight = pooling2dDesc->poolHeight();

    return desc;
}

void Deserializer::ParsePooling2d(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);

    auto pooling2dDes = graph->layers()->Get(layerIndex)->layer_as_Pooling2dLayer()->descriptor();
    auto inputs = GetInputs(graph, layerIndex);
    CHECK_VALID_SIZE(inputs.size(), 1);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);
    auto outputInfo = ToTensorInfo(outputs[0]);

    auto pooling2dDescriptor = GetPoolingDescriptor(pooling2dDes, layerIndex);
    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddPooling2dLayer(pooling2dDescriptor, layerName.c_str());
    layer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

armnn::TensorInfo Deserializer::OutputShapeOfReshape(const armnn::TensorInfo& inputTensorInfo,
                                                          const std::vector<uint32_t>& targetDimsIn)
{
    std::vector<unsigned int> outputDims(targetDimsIn.begin(), targetDimsIn.end());
    const auto stretchDim = std::find(targetDimsIn.begin(), targetDimsIn.end(), -1);

    if (stretchDim != targetDimsIn.end())
    {
        if (std::find(std::next(stretchDim), targetDimsIn.end(), -1) != targetDimsIn.end())
        {
            throw ParseException(boost::str(
                boost::format("At most one component of shape can be -1 %1%") % CHECK_LOCATION().AsString()));
        }

        auto targetNumElements =
           boost::numeric_cast<unsigned int>(
               std::accumulate(targetDimsIn.begin(), targetDimsIn.end(), -1, std::multiplies<int32_t>()));

        auto stretchIndex = static_cast<size_t>(std::distance(targetDimsIn.begin(), stretchDim));
        outputDims[stretchIndex] = inputTensorInfo.GetNumElements() / targetNumElements;
    }

    TensorShape outputShape = TensorShape(static_cast<unsigned int>(outputDims.size()), outputDims.data());

    armnn::TensorInfo reshapeInfo = inputTensorInfo;
    reshapeInfo.SetShape(outputShape);

    return reshapeInfo;
}

void Deserializer::ParseReshape(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    auto inputs = GetInputs(graph, layerIndex);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    armnn::TensorInfo inputTensorInfo = ToTensorInfo(inputs[0]);
    armnn::TensorInfo actualOutputTensorInfo = ToTensorInfo(outputs[0]);

    const auto targetDims = graph->layers()->Get(layerIndex)->layer_as_ReshapeLayer()->descriptor()->targetShape();
    std::vector<uint32_t> outputDims(targetDims->begin(), targetDims->begin() + targetDims->size());

    armnn::TensorInfo reshapeOutputTensorInfo = Deserializer::OutputShapeOfReshape(inputTensorInfo, outputDims);
    const armnn::TensorShape& reshapeOutputTensorShape = reshapeOutputTensorInfo.GetShape();

    const std::vector<uint32_t> expectedDims(outputs[0]->dimensions()->begin(),
                                             outputs[0]->dimensions()->begin() + outputs[0]->dimensions()->size());

    if (inputs.size() > 1 && !CheckShape(reshapeOutputTensorShape, expectedDims))
    {
        std::stringstream ss;
        ss << "New shape defined in reshape parameters "
           << reshapeOutputTensorShape
           << " does not equal output shape "
           << actualOutputTensorInfo.GetShape()
           << ": "
           << CHECK_LOCATION().AsString();
        throw ParseException(ss.str());
    }

    armnn::ReshapeDescriptor reshapeDesc;
    reshapeDesc.m_TargetShape = reshapeOutputTensorShape;

    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddReshapeLayer(reshapeDesc, layerName.c_str());
    layer->GetOutputSlot(0).SetTensorInfo(reshapeOutputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseResizeBilinear(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);

    Deserializer::TensorRawPtrVector inputs = GetInputs(graph, layerIndex);
    CHECK_VALID_SIZE(inputs.size(), 1);

    Deserializer::TensorRawPtrVector outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto flatBufferDescriptor = graph->layers()->Get(layerIndex)->layer_as_ResizeBilinearLayer()->descriptor();

    armnn::ResizeBilinearDescriptor descriptor;
    descriptor.m_TargetWidth = flatBufferDescriptor->targetWidth();
    descriptor.m_TargetHeight = flatBufferDescriptor->targetHeight();
    descriptor.m_DataLayout = ToDataLayout(flatBufferDescriptor->dataLayout());

    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddResizeBilinearLayer(descriptor, layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseSoftmax(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);

    Deserializer::TensorRawPtrVector inputs = GetInputs(graph, layerIndex);
    CHECK_VALID_SIZE(inputs.size(), 1);

    Deserializer::TensorRawPtrVector outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    armnn::SoftmaxDescriptor descriptor;
    descriptor.m_Beta = graph->layers()->Get(layerIndex)->layer_as_SoftmaxLayer()->descriptor()->beta();
    auto layerName = GetLayerName(graph, layerIndex);

    IConnectableLayer* layer = m_Network->AddSoftmaxLayer(descriptor, layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseSpaceToBatchNd(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);

    Deserializer::TensorRawPtrVector inputs = GetInputs(graph, layerIndex);
    CHECK_VALID_SIZE(inputs.size(), 1);

    Deserializer::TensorRawPtrVector outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto flatBufferDescriptor = graph->layers()->Get(layerIndex)->layer_as_SpaceToBatchNdLayer()->descriptor();
    auto flatBufferPadList = flatBufferDescriptor->padList();
    auto flatBufferBlockShape = flatBufferDescriptor->blockShape();

    if (flatBufferPadList->Length() % 2 != 0)
    {
        throw ParseException(boost::str(
            boost::format("The size of the pad list must be divisible by 2 %1%") % CHECK_LOCATION().AsString()));
    }

    std::vector<std::pair<unsigned int, unsigned int>> padList;
    padList.reserve(flatBufferPadList->Length() / 2);
    for (unsigned int i = 0; i < flatBufferPadList->Length() - 1; i += 2)
    {
        padList.emplace_back(flatBufferPadList->Get(i), flatBufferPadList->Get(i+1));
    }

    armnn::SpaceToBatchNdDescriptor descriptor;
    descriptor.m_DataLayout = ToDataLayout(flatBufferDescriptor->dataLayout());
    descriptor.m_BlockShape =
        std::vector<unsigned int>(flatBufferBlockShape->begin(), flatBufferBlockShape->end());
    descriptor.m_PadList = padList;

    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddSpaceToBatchNdLayer(descriptor, layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

armnn::NormalizationDescriptor Deserializer::GetNormalizationDescriptor(
    Deserializer::NormalizationDescriptorPtr normalizationDescriptor,
    unsigned int layerIndex)
{
    armnn::NormalizationDescriptor desc;

    switch (normalizationDescriptor->normChannelType())
    {
        case NormalizationAlgorithmChannel_Across:
        {
            desc.m_NormChannelType = armnn::NormalizationAlgorithmChannel::Across;
            break;
        }
        case NormalizationAlgorithmChannel_Within:
        {
            desc.m_NormChannelType = armnn::NormalizationAlgorithmChannel::Within;
            break;
        }
        default:
        {
            BOOST_ASSERT_MSG(false, "Unsupported normalization channel type");
        }
    }

    switch (normalizationDescriptor->normMethodType())
    {
        case NormalizationAlgorithmMethod_LocalBrightness:
        {
            desc.m_NormMethodType = armnn::NormalizationAlgorithmMethod::LocalBrightness;
            break;
        }
        case NormalizationAlgorithmMethod_LocalContrast:
        {
            desc.m_NormMethodType = armnn::NormalizationAlgorithmMethod::LocalContrast;
            break;
        }
        default:
        {
            BOOST_ASSERT_MSG(false, "Unsupported normalization method type");
        }
    }

    switch (normalizationDescriptor->dataLayout())
    {
        case DataLayout_NCHW:
        {
            desc.m_DataLayout = armnn::DataLayout::NCHW;
            break;
        }
        case DataLayout_NHWC:
        {
            desc.m_DataLayout = armnn::DataLayout::NHWC;
            break;
        }
        default:
        {
            BOOST_ASSERT_MSG(false, "Unsupported data layout");
        }
    }

    desc.m_Alpha    = normalizationDescriptor->alpha();
    desc.m_Beta     = normalizationDescriptor->beta();
    desc.m_K        = normalizationDescriptor->k();
    desc.m_NormSize = normalizationDescriptor->normSize();

    return desc;
}

void Deserializer::ParseNormalization(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);

    auto normalizationDes = graph->layers()->Get(layerIndex)->layer_as_NormalizationLayer()->descriptor();

    Deserializer::TensorRawPtrVector inputs = GetInputs(graph, layerIndex);
    CHECK_VALID_SIZE(inputs.size(), 1);

    Deserializer::TensorRawPtrVector outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto outputInfo = ToTensorInfo(outputs[0]);

    auto normalizationDescriptor = GetNormalizationDescriptor(normalizationDes, layerIndex);
    auto layerName = GetLayerName(graph, layerIndex);

    IConnectableLayer* layer = m_Network->AddNormalizationLayer(normalizationDescriptor, layerName.c_str());
    layer->GetOutputSlot(0).SetTensorInfo(outputInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseRsqrt(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    auto inputs = GetInputs(graph, layerIndex);
    CHECK_LOCATION();
    CHECK_VALID_SIZE(inputs.size(), 1);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddRsqrtLayer(layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseStridedSlice(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);

    Deserializer::TensorRawPtrVector inputs = GetInputs(graph, layerIndex);
    CHECK_VALID_SIZE(inputs.size(), 1);

    Deserializer::TensorRawPtrVector outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto flatBufferDescriptor = graph->layers()->Get(layerIndex)->layer_as_StridedSliceLayer()->descriptor();

    auto flatBufferBegin = flatBufferDescriptor->begin();
    auto flatBufferEnd = flatBufferDescriptor->end();
    auto flatBufferStride = flatBufferDescriptor->stride();

    if (!(flatBufferBegin->Length() == flatBufferEnd->Length() &&
          flatBufferBegin->Length() == flatBufferStride->Length()))
    {
        throw ParseException(boost::str(
            boost::format("The size of the begin, end, and stride must be equal %1%") % CHECK_LOCATION().AsString()));
    }

    std::vector<int> begin(flatBufferBegin->begin(), flatBufferBegin->end());
    std::vector<int> end(flatBufferEnd->begin(), flatBufferEnd->end());
    std::vector<int> stride(flatBufferStride->begin(), flatBufferStride->end());

    armnn::StridedSliceDescriptor descriptor(begin, end, stride);
    descriptor.m_BeginMask = flatBufferDescriptor->beginMask();
    descriptor.m_EndMask = flatBufferDescriptor->endMask();
    descriptor.m_ShrinkAxisMask = flatBufferDescriptor->shrinkAxisMask();
    descriptor.m_EllipsisMask = flatBufferDescriptor->ellipsisMask();
    descriptor.m_NewAxisMask = flatBufferDescriptor->newAxisMask();
    descriptor.m_DataLayout = ToDataLayout(flatBufferDescriptor->dataLayout());

    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddStridedSliceLayer(descriptor, layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseSubtraction(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);
    auto inputs = GetInputs(graph, layerIndex);
    CHECK_LOCATION();
    CHECK_VALID_SIZE(inputs.size(), 2);

    auto outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddSubtractionLayer(layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseGather(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);

    Deserializer::TensorRawPtrVector inputs = GetInputs(graph, layerIndex);
    CHECK_VALID_SIZE(inputs.size(), 2);

    Deserializer::TensorRawPtrVector outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddGatherLayer(layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

void Deserializer::ParseMean(GraphPtr graph, unsigned int layerIndex)
{
    CHECK_LAYERS(graph, 0, layerIndex);

    Deserializer::TensorRawPtrVector inputs = GetInputs(graph, layerIndex);
    CHECK_VALID_SIZE(inputs.size(), 1);

    Deserializer::TensorRawPtrVector outputs = GetOutputs(graph, layerIndex);
    CHECK_VALID_SIZE(outputs.size(), 1);

    auto flatBufferDescriptor = graph->layers()->Get(layerIndex)->layer_as_MeanLayer()->descriptor();
    auto flatBufferAxis = flatBufferDescriptor->axis();
    auto flatBufferKeepDims = flatBufferDescriptor->keepDims();

    armnn::MeanDescriptor descriptor;
    descriptor.m_Axis = std::vector<unsigned int>(flatBufferAxis->begin(), flatBufferAxis->end());
    descriptor.m_KeepDims = flatBufferKeepDims;

    auto layerName = GetLayerName(graph, layerIndex);
    IConnectableLayer* layer = m_Network->AddMeanLayer(descriptor, layerName.c_str());

    armnn::TensorInfo outputTensorInfo = ToTensorInfo(outputs[0]);
    layer->GetOutputSlot(0).SetTensorInfo(outputTensorInfo);

    RegisterInputSlots(graph, layerIndex, layer);
    RegisterOutputSlots(graph, layerIndex, layer);
}

} // namespace armnnDeserializer
