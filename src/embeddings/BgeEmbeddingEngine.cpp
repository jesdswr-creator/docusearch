// ============================================================
// BgeEmbeddingEngine.cpp - ONNX Runtime inference
// ============================================================

#include "BgeEmbeddingEngine.h"
#include "../core/Logger.h"

#include <QFile>
#include <QFileInfo>
#include <QThread>
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
        // Scale inference with the user's hardware: the old fixed 2
        // intra-op threads made a 5000-document embedding backlog take
        // effectively days. (cores - 1, floor 4) turns that into tens of
        // minutes on any modern CPU while still leaving one core for the
        // UI. Inter-op stays 1 — the BGE graph is sequential anyway.
        const int inferenceThreads = qMax(4, QThread::idealThreadCount() - 1);
        opts->SetIntraOpNumThreads(inferenceThreads);
        opts->SetInterOpNumThreads(1);
        opts->SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL);
        DS_INFO("BGE", QString("ONNX inference threads: %1")
                           .arg(inferenceThreads));
        m_sessionOptions = new std::unique_ptr<Ort::SessionOptions>(std::move(opts));

        // Create Session.
        // On Windows, the Ort::Session constructor that takes a path requires
        // a const wchar_t* (UTF-16). On Linux/macOS it takes const char*.
        // We use the wide-string overload on Windows and the narrow overload
        // elsewhere.
        const auto& envRef = **static_cast<std::unique_ptr<Ort::Env>*>(m_env);
        const auto& optsRef = **static_cast<std::unique_ptr<Ort::SessionOptions>*>(m_sessionOptions);
#ifdef _WIN32
        // Convert UTF-8 path to UTF-16 wide string.
        const std::wstring widePath = modelPath.toStdWString();
        auto sess = std::make_unique<Ort::Session>(envRef, widePath.c_str(), optsRef);
#else
        auto sess = std::make_unique<Ort::Session>(envRef, m_modelPath.c_str(), optsRef);
#endif
        m_session = new std::unique_ptr<Ort::Session>(std::move(sess));

        // Validate
        const size_t numInputs =
            (*static_cast<std::unique_ptr<Ort::Session>*>(m_session))->GetInputCount();
        if (numInputs == 0) {
            DS_WARN("BGE", "Model has no inputs — invalid.");
            return false;
        }

        // CRITICAL: Load vocab.txt — without it, the tokenizer returns
        // empty output and semantic search produces garbage embeddings.
        // The vocab.txt sits next to model.onnx in the same folder.
        const QFileInfo modelFi(modelPath);
        const QString vocabPath = modelFi.absolutePath() + "/vocab.txt";
        if (!m_tokenizer->loadVocabulary(vocabPath)) {
            DS_WARN("BGE", "Failed to load vocab.txt at: " + vocabPath);
            DS_WARN("BGE", "Semantic search will be DISABLED — vocab is required "
                           "for meaningful embeddings.");
            return false;
        }
        DS_INFO("BGE", "Tokenizer vocabulary loaded from: " + vocabPath);

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

        // CRITICAL: If tokenizer returned empty (vocab not loaded),
        // we cannot produce a meaningful embedding. Return false so
        // the caller knows to skip semantic scoring.
        if (tokens.inputIds.empty()) {
            DS_WARN("BGE", "Tokenizer returned empty — vocab not loaded.");
            return false;
        }

        auto& session = **static_cast<std::unique_ptr<Ort::Session>*>(m_session);

        // 2. Run inference at the EXACT token count (the BGE ONNX
        //    export has a dynamic sequence axis). This replaces the old
        //    fixed 128-token shape which (a) truncated ~1000-char chunks
        //    to roughly their first half and (b) padded short queries
        //    out to 128, making every query inference 4-8x slower than
        //    needed. Tensors are taken BY VALUE so the underlying
        //    buffers stay alive for the duration of Run().
        auto runOnce = [&](std::vector<int64_t> ids,
                           std::vector<int64_t> mask,
                           std::vector<int64_t> types)
            -> std::vector<Ort::Value> {
            std::array<int64_t, 2> shape = {
                1, static_cast<int64_t>(ids.size())};
            Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(
                OrtArenaAllocator, OrtMemTypeDefault);

            std::vector<Ort::Value> inputTensors;
            inputTensors.reserve(3);
            inputTensors.push_back(Ort::Value::CreateTensor<int64_t>(
                memInfo, ids.data(), ids.size(), shape.data(), shape.size()));
            inputTensors.push_back(Ort::Value::CreateTensor<int64_t>(
                memInfo, mask.data(), mask.size(), shape.data(), shape.size()));
            inputTensors.push_back(Ort::Value::CreateTensor<int64_t>(
                memInfo, types.data(), types.size(), shape.data(), shape.size()));

            // BGE expects: input_ids, attention_mask, token_type_ids.
            // Output: last_hidden_state (or sentence_embedding).
            const char* inputNames[]  = {"input_ids", "attention_mask",
                                         "token_type_ids"};
            const char* outputNames[] = {"last_hidden_state"};
            const char* altOutputNames[] = {"sentence_embedding"};

            try {
                return session.Run(
                    Ort::RunOptions{nullptr},
                    inputNames, inputTensors.data(), inputTensors.size(),
                    outputNames, 1);
            } catch (const Ort::Exception&) {
                // Retry with the alternative output name.
                return session.Run(
                    Ort::RunOptions{nullptr},
                    inputNames, inputTensors.data(), inputTensors.size(),
                    altOutputNames, 1);
            }
        };

        std::vector<Ort::Value> outputTensors;
        try {
            // Exact-length inference (dynamic sequence axis).
            outputTensors = runOnce(tokens.inputIds, tokens.attentionMask,
                                    tokens.tokenTypeIds);
        } catch (const Ort::Exception& e) {
            // Fallback: some model.onnx exports were built with a FIXED
            // 128-position sequence axis. Retry padded/truncated to 128
            // — the legacy shape every earlier DocuSearch build used —
            // so those installs keep working instead of losing semantic
            // search entirely.
            DS_WARN("BGE", QString(
                        "Dynamic-length inference failed (%1); retrying "
                        "with legacy fixed 128-token shape.")
                        .arg(e.what()));
            auto padTo128 = [](std::vector<int64_t> v, int64_t fill) {
                constexpr int kLegacySeq = 128;
                if (static_cast<int>(v.size()) > kLegacySeq) {
                    v.resize(kLegacySeq);
                }
                v.resize(kLegacySeq, fill);
                return v;
            };
            outputTensors = runOnce(
                padTo128(tokens.inputIds, BgeTokenizer::PAD_TOKEN_ID),
                padTo128(tokens.attentionMask, 0),
                padTo128(tokens.tokenTypeIds, 0));
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
