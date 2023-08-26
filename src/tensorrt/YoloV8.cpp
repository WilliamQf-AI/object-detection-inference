#include "YoloV8.hpp"

YoloV8::YoloV8(const std::string& model_path, bool use_gpu,
    float confidenceThreshold,
    size_t network_width,
    size_t network_height) : 
    Yolo{model_path, use_gpu, confidenceThreshold,
            network_width,
            network_height}
{
    logger_->info("Initializing YoloV8 TensorRT");
    initializeBuffers(model_path);
}


// Create the TensorRT runtime and deserialize the engine
std::shared_ptr<nvinfer1::ICudaEngine> YoloV8::createRuntimeAndDeserializeEngine(const std::string& engine_path, Logger& logger, nvinfer1::IRuntime*& runtime)
{
    // Create TensorRT runtime
    runtime = nvinfer1::createInferRuntime(logger);

    // Load engine file
    std::ifstream engine_file(engine_path, std::ios::binary);
    if (!engine_file)
    {
        throw std::runtime_error("Failed to open engine file: " + engine_path);
    }
    engine_file.seekg(0, std::ios::end);
    size_t file_size = engine_file.tellg();
    engine_file.seekg(0, std::ios::beg);
    std::vector<char> engine_data(file_size);
    engine_file.read(engine_data.data(), file_size);
    engine_file.close();

    // Deserialize engine
    std::shared_ptr<nvinfer1::ICudaEngine> engine(
        runtime->deserializeCudaEngine(engine_data.data(), file_size),
        [](nvinfer1::ICudaEngine* engine) { engine->destroy(); });

    return engine;
}


void YoloV8::createContextAndAllocateBuffers(nvinfer1::ICudaEngine* engine, nvinfer1::IExecutionContext*& context, std::vector<void*>& buffers, std::vector<nvinfer1::Dims>& output_dims, std::vector<std::vector<float>>& h_outputs)
{
    context = engine->createExecutionContext();
    buffers.resize(engine->getNbBindings());
    for (int i = 0; i < engine->getNbBindings(); ++i)
    {
        nvinfer1::Dims dims = engine->getBindingDimensions(i);
        auto binding_size = getSizeByDim(engine->getBindingDimensions(i)) * sizeof(float);
        cudaMalloc(&buffers[i], binding_size);
        if (engine->bindingIsInput(i))
        {
            const auto input_shape = std::vector{ dims.d[0], dims.d[1], dims.d[2], dims.d[3] };
            input_width_ = dims.d[3];
            input_height_ = dims.d[2];
            channels_ = dims.d[1];
        }
        else
        {
            output_dims.emplace_back(engine->getBindingDimensions(i));
            auto size = getSizeByDim(dims);
            h_outputs.emplace_back(std::vector<float>(size));
        }
    }
}


std::vector<Detection> YoloV8::run_detection(const cv::Mat& image)
{
    // Preprocess the input image
    std::vector<float> h_input_data = preprocess_image(image);
    cudaMemcpy(buffers_[0], h_input_data.data(), sizeof(float)*h_input_data.size(), cudaMemcpyHostToDevice);

    if(!context_->enqueueV2(buffers_.data(), 0, nullptr))
    {
        logger_->error("Forward Error !");
        std::exit(1);
    }
        

    for (size_t i = 0; i < h_outputs_.size(); i++)
        cudaMemcpy(h_outputs_[i].data(), buffers_[i + 1], h_outputs_[i].size() * sizeof(float), cudaMemcpyDeviceToHost);

    const float* output_boxes = h_outputs_[0].data();

    const int* shape_boxes_ptr = reinterpret_cast<const int*>(output_dims_[0].d);
    std::vector<int64_t> shape_boxes(shape_boxes_ptr, shape_boxes_ptr + output_dims_[0].nbDims);
    cv::Size frame_size(image.cols, image.rows);
    return postprocess(output_boxes, shape_boxes, frame_size);   
}  



void YoloV8::initializeBuffers(const std::string& engine_path)
{
    // Create logger
    Logger logger;

    // Create TensorRT runtime and deserialize engine
    engine_ = createRuntimeAndDeserializeEngine(engine_path, logger, runtime_);

    // Create execution context and allocate input/output buffers
    createContextAndAllocateBuffers(engine_.get(), context_, buffers_, output_dims_, h_outputs_);
}

// calculate size of tensor
size_t YoloV8::getSizeByDim(const nvinfer1::Dims& dims)
{
    size_t size = 1;
    for (size_t i = 0; i < dims.nbDims; ++i)
    {
        size *= dims.d[i];
    }
    return size;
}