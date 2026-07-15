// ============================================================
// ocr_helper_main.cpp - Standalone OCR helper executable
// ============================================================
//
// This is a SEPARATE executable that links RapidOCR and runs OCR
// on a single image file. The main DocuSearch app calls it via
// QProcess — if the OCR engine crashes, only this helper crashes,
// not the main app.
//
// Usage: docusearch_ocr_helper.exe <image_path>
// Output: recognized text to stdout (empty if no text or error)
//
// The helper loads ONNX models from <exeDir>/models/
// ============================================================

#include <iostream>
#include <string>
#include <filesystem>

// RapidOCR headers
#include "OcrLiteAPI.h"
#include "OcrStructAPI.h"

#include <opencv2/opencv.hpp>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <image_path>" << std::endl;
        return 1;
    }

    std::string imagePath = argv[1];

    // Find models directory (next to this exe)
    namespace fs = std::filesystem;
    fs::path exePath = fs::current_path();
    // Try: ./models/, ../models/
    std::string modelsDir;
    for (const auto& candidate : {"models", "../models"}) {
        if (fs::exists(fs::path(candidate) / "ch_PP-OCRv4_det_infer.onnx")) {
            modelsDir = candidate;
            break;
        }
    }
    if (modelsDir.empty()) {
        std::cerr << "Models not found" << std::endl;
        return 1;
    }

    try {
        OcrLite ocr;
        ocr.setProvider("OnnxRuntime");
        ocr.setNumThread(2);
        ocr.initLogger(false, false, false);

        std::string detPath  = modelsDir + "/ch_PP-OCRv4_det_infer.onnx";
        std::string clsPath  = modelsDir + "/ch_ppocr_mobile_v2.0_cls_infer.onnx";
        std::string recPath  = modelsDir + "/ch_PP-OCRv4_rec_infer.onnx";
        std::string keysPath = modelsDir + "/ppocr_keys_v1.txt";

        if (!ocr.initModels(detPath, clsPath, recPath, keysPath)) {
            std::cerr << "Failed to load models" << std::endl;
            return 1;
        }

        // Run OCR on the image file
        fs::path p(imagePath);
        std::string imgName = p.filename().string();

        OcrResult result = ocr.detect(imagePath.c_str(), imgName.c_str(),
            50,    // padding
            1024,  // maxSideLen
            0.5f,  // boxScoreThresh
            0.3f,  // boxThresh
            1.6f,  // unClipRatio
            true,  // doAngle
            true   // mostAngle
        );

        // Print recognized text to stdout
        for (const auto& block : result.textBlocks) {
            if (!block.text.empty()) {
                std::cout << block.text << std::endl;
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "OCR error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown OCR error" << std::endl;
        return 1;
    }
}
