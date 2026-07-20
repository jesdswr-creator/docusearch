#pragma once

// ============================================================
// BgeEmbeddingEngine.h - ONNX Runtime inference for BGE model
// ============================================================
//
// Loads the BGE Small EN v1.5 model (model.onnx) and runs inference
// to produce 384-dim L2-normalized embeddings.
//
// SAFETY:
//   - initialize() returns false if model file is missing
//   - initialize() returns false if ONNX Runtime DLL is missing
//   - embed() never throws — catches all Ort::Exception, std::bad_alloc,
//     std::exception, and ... — returns false on any error
//   - All failures are silent (caller falls back to keyword search)
// ============================================================

#include "BgeTokenizer.h"

#include <QString>
#include <vector>
#include <memory>

namespace DocuSearch {

class BgeEmbeddingEngine {
public:
    BgeEmbeddingEngine();
    ~BgeEmbeddingEngine();

    // Load the ONNX model. Returns true on success, false on any error.
    // Never throws.
    bool initialize(const QString& modelPath);

    // Generate a 384-dim L2-normalized embedding for the input text.
    // Returns true on success, false on any error. Never throws.
    // On success, outEmbedding has exactly 384 floats.
    bool embed(const QString& text, std::vector<float>& outEmbedding);

    bool isReady() const { return m_initialized; }
    int getEmbeddingDim() const { return EMBEDDING_DIM; }

    static constexpr int EMBEDDING_DIM = 384;

private:
    std::unique_ptr<BgeTokenizer> m_tokenizer;

    // ONNX Runtime objects are kept as void* to avoid leaking the
    // onnxruntime_cxx_api.h header into this class's interface.
    // Internally they are cast to:
    //   m_env            → std::unique_ptr<Ort::Env>
    //   m_sessionOptions → std::unique_ptr<Ort::SessionOptions>
    //   m_session        → std::unique_ptr<Ort::Session>
    void* m_env            = nullptr;
    void* m_sessionOptions = nullptr;
    void* m_session        = nullptr;

    bool        m_initialized = false;
    std::string m_modelPath;
};

} // namespace DocuSearch
