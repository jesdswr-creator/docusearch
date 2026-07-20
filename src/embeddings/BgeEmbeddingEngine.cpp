// ============================================================
// BgeEmbeddingEngine.cpp - ONNX Runtime inference
// ============================================================

#include "BgeEmbeddingEngine.h"
#include "../core/Logger.h"

#include <QFile>
#include <cmath>
#include <cstring>
#include <array>

#ifdef DOCUSEARCH_HAS_ONNXRUNTIME
#  include <onnxruntime_cxx_api.h>
#endif

namespace DocuSearch {

BgeEmbeddingEngine::BgeEmbeddingEngine()
    : m_tokenizer(std::make_unique<BgeTokenizer>()) {
}

BgeEmbeddingEngine::~BgeEmbeddingEngine() {
#ifdef DOCUSEARCH_HAS_ONNXRUNTIME
    // Cast back and delete — needed because we stored as void* to keep
    // the header clean.
    if (m_session) {
        delete static_cast<std::unique_ptr<Ort::Session>*>(m_session);
        m_session = nullptr;
    }
    if (m_sessionOptions) {
        delete static_cast<std::unique_ptr<Ort::SessionOptions>*>(m_sessionOptions);
        m_sessionOptions = nullptr;
    }
    if (m_env) {
        delete static_cast<std::unique_ptr<Ort::Env>*>(m_env);
        m_env = nullptr;
    }
#endif
    m_initialized = false;
}

bool BgeEmbeddingEngine::initialize(const QString& modelPath) {
#ifdef DOCUSEARCH_HAS_ONNXRUNTIME
    try {
        if (!QFile::exists(modelPath)) {
            DS_WARN("BGE", "Model file not found: " + modelPath);
            return false;
        }
        m_modelPath = modelPath.toStdString();

        // Create Ort::Env
        auto env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "BgeEngine");
        m_env = new std::unique_ptr<Ort::Env>(std::move(env));

        // Create SessionOptions
        auto opts = std::make_unique<Ort::SessionOptions>();
        opts->SetIntraOpNumThreads(2);
        opts->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        m_sessionOptions = new std::unique_ptr<Ort::SessionOptions>(std::move(opts));

        // Create Session
        auto sess = std::make_unique<Ort::Session>(
            **static_cast<std::unique_ptr<Ort::Env>*>(m_env),
            m_modelPath.c_str(),
            **static_cast<std::unique_ptr<Ort::SessionOptions>*>(m_sessionOptions));
        m_session = new std::unique_ptr<Ort::Session>(std::move(sess));

        // Validate
        const size_t numInputs =
            (*static_cast<std::unique_ptr<Ort::Session>*>(m_session))->GetInputCount();
        if (numInputs == 0) {
            DS_WARN("BGE", "Model has no inputs — invalid.");
            return false;
        }

        m_initialized = true;
        DS_INFO("BGE", "BGE engine initialized. Inputs: " + QString::number(numInputs));
        return true;
    } catch (const Ort::Exception& e) {
        DS_WARN("BGE", QString("ONNX exception during init: %1").arg(e.what()));
        m_initialized = false;
        return false;
    } catch (const std::bad_alloc& e) {
        DS_WARN("BGE", QString("OOM during init: %1").arg(e.what()));
        m_initialized = false;
        return false;
    } catch (const std::exception& e) {
        DS_WARN("BGE", QString("Exception during init: %1").arg(e.what()));
        m_initialized = false;
        return false;
    } catch (...) {
        DS_WARN("BGE", "Unknown exception during init.");
        m_initialized = false;
        return false;
    }
#else
    DS_WARN("BGE", "DocuSearch was built without ONNX Runtime support — "
                   "semantic search is unavailable.");
    (void)modelPath;
    return false;
#endif
}

bool BgeEmbeddingEngine::embed(const QString& text, std::vector<float>& outEmbedding) {
    outEmbedding.clear();

    if (!m_initialized) return false;

    // Empty/whitespace text → return zero embedding (still 384 floats).
    if (text.trimmed().isEmpty()) {
        outEmbedding.assign(EMBEDDING_DIM, 0.0f);
        return true;
    }

#ifdef DOCUSEARCH_HAS_ONNXRUNTIME
    try {
        // 1. Tokenize
        BgeTokenizer::TokenizerOutput tokens = m_tokenizer->encode(text);

        // 2. Create input tensors
        Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator, OrtMemTypeDefault);

        std::array<int64_t, 2> shape = {1, BgeTokenizer::MAX_SEQ_LENGTH};

        auto inputIdsTensor = Ort::Value::CreateTensor<int64_t>(
            memInfo, tokens.inputIds.data(), tokens.inputIds.size(),
            shape.data(), shape.size());
        auto attnTensor = Ort::Value::CreateTensor<int64_t>(
            memInfo, tokens.attentionMask.data(), tokens.attentionMask.size(),
            shape.data(), shape.size());
        auto typeIdsTensor = Ort::Value::CreateTensor<int64_t>(
            memInfo, tokens.tokenTypeIds.data(), tokens.tokenTypeIds.size(),
            shape.data(), shape.size());

        // 3. Build input/output name arrays. BGE expects:
        //    input_ids, attention_mask, token_type_ids
        //    Output: last_hidden_state  (or  sentence_embedding)
        const char* inputNames[]  = {"input_ids", "attention_mask", "token_type_ids"};
        const char* outputNames[] = {"last_hidden_state"};

        // 4. Run inference
        std::vector<Ort::Value> inputTensors;
        inputTensors.reserve(3);
        inputTensors.push_back(std::move(inputIdsTensor));
        inputTensors.push_back(std::move(attnTensor));
        inputTensors.push_back(std::move(typeIdsTensor));

        auto& session = **static_cast<std::unique_ptr<Ort::Session>*>(m_session);

        std::vector<Ort::Value> outputTensors;
        try {
            outputTensors = session.Run(
                Ort::RunOptions{nullptr},
                inputNames, inputTensors.data(), inputTensors.size(),
                outputNames, 1);
        } catch (const Ort::Exception& e) {
            // Retry with alternative output name "sentence_embedding"
            const char* altOutputNames[] = {"sentence_embedding"};
            outputTensors = session.Run(
                Ort::RunOptions{nullptr},
                inputNames, inputTensors.data(), inputTensors.size(),
                altOutputNames, 1);
            (void)e;
        }

        if (outputTensors.empty()) {
            DS_WARN("BGE", "ONNX Run returned no outputs.");
            return false;
        }

        // 5. Extract embedding (CLS token = first 384 floats).
        float* data = outputTensors[0].GetTensorMutableData<float>();
        if (!data) {
            DS_WARN("BGE", "ONNX output tensor is null.");
            return false;
        }
        outEmbedding.assign(data, data + EMBEDDING_DIM);

        // 6. L2 normalize
        float norm = 0.0f;
        for (float v : outEmbedding) norm += v * v;
        norm = std::sqrt(norm);
        if (norm > 1e-9f) {
            for (float& v : outEmbedding) v /= norm;
        }

        return true;
    } catch (const Ort::Exception& e) {
        DS_WARN("BGE", QString("ONNX exception during embed: %1").arg(e.what()));
        outEmbedding.clear();
        return false;
    } catch (const std::bad_alloc& e) {
        DS_WARN("BGE", QString("OOM during embed: %1").arg(e.what()));
        outEmbedding.clear();
        return false;
    } catch (const std::exception& e) {
        DS_WARN("BGE", QString("Exception during embed: %1").arg(e.what()));
        outEmbedding.clear();
        return false;
    } catch (...) {
        DS_WARN("BGE", "Unknown exception during embed.");
        outEmbedding.clear();
        return false;
    }
#else
    return false;
#endif
}

} // namespace DocuSearch
