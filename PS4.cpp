//PS3 base file

#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

#include <filesystem>
#include <thread>
#include <mutex>
#include <semaphore>
#include <queue>
#include <atomic>
#include <fstream>
#include <chrono>
#include <iostream>
#include <algorithm>
#include <limits>

namespace fs = std::filesystem;

std::queue<std::string> imageQueue;
std::mutex queueMutex;
std::counting_semaphore<std::numeric_limits<int>::max()> itemSem{ 0 };
std::atomic<bool> producerDone{ false };
std::mutex csvMutex;
std::ofstream csvFile;
std::atomic<int> nextId{ 1 };

bool has_extension_case_insensitive(const fs::path& p, const std::string& ext) {
    if (!p.has_extension()) return false;
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
    std::string lower_ext = ext;
    std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return e == lower_ext;
}

bool is_image_file(const fs::path& p) {
    if (!p.has_extension()) return false;
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tif" || ext == ".tiff");
}

std::string csv_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    out.push_back('"');
    for (char c : s) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

void producer(const std::string& inputDir, unsigned int numWorkers) {
    try {
        for (const auto& entry : fs::directory_iterator(inputDir)) {
            if (!entry.is_regular_file()) continue;
            if (!is_image_file(entry.path())) continue;

            {
                std::lock_guard<std::mutex> lock(queueMutex);
                imageQueue.push(entry.path().string());
            }
            itemSem.release();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Producer exception: " << e.what() << "\n";
    }

    producerDone.store(true);

    for (unsigned int i = 0; i < numWorkers; ++i)
        itemSem.release();
}

void worker(unsigned int id, const std::string& outputDir) {
    tesseract::TessBaseAPI tess;
    bool tessReady = (tess.Init(nullptr, "eng") == 0);
    if (!tessReady) {
        std::cerr << "[Worker " << id << "] Warning: Tesseract Init failed. Worker will run preprocessing only.\n";
    }

    while (true) {
        itemSem.acquire();

        std::string path;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (!imageQueue.empty()) {
                path = imageQueue.front();
                imageQueue.pop();
            }
            else if (producerDone.load()) {
                break;
            }
            else {
                continue;
            }
        }

        auto start = std::chrono::high_resolution_clock::now();

        cv::Mat img = cv::imread(path, cv::IMREAD_COLOR);
        if (img.empty()) {
            std::cerr << "[Worker " << id << "] Failed to read image: " << path << "\n";
            continue;
        }

        cv::Mat gray, blur, binary;
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
        cv::GaussianBlur(gray, blur, cv::Size(3, 3), 0);
        cv::adaptiveThreshold(blur, binary, 255,
            cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY_INV,
            31, 10);
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2));
        cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel);
        cv::morphologyEx(binary, binary, cv::MORPH_OPEN, kernel);

        fs::path fname = fs::path(path).filename();
        fs::path outPath = fs::path(outputDir) / fname;
        try {
            if (!cv::imwrite(outPath.string(), binary)) {
                std::cerr << "[Worker " << id << "] Failed to write preprocessed image: " << outPath << "\n";
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[Worker " << id << "] imwrite exception: " << e.what() << "\n";
        }

        std::string text;
        if (tessReady) {
            cv::Mat continuous = binary;
            if (!binary.isContinuous()) continuous = binary.clone();

            tess.SetImage(continuous.data, continuous.cols, continuous.rows, 1, static_cast<int>(continuous.step));
            if (tess.Recognize(0) == 0) {
                char* out = tess.GetUTF8Text();
                if (out) {
                    text = out;
                    delete[] out;
                }
            }
            else {
                std::cerr << "[Worker " << id << "] Recognize() failed for " << fname.string() << "\n";
            }

            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        }

        auto end = std::chrono::high_resolution_clock::now();
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        {
            std::lock_guard<std::mutex> lock(csvMutex);
            int myId = nextId++;
            csvFile << myId << ","
                << csv_escape(fname.string()) << ","
                << csv_escape(text) << ","
                << ms << "\n";
        }

        std::cout << "[Worker " << id << "] " << fname.string() << " done (" << ms << " ms)\n";
    }

    if (tessReady) tess.End();
    std::cout << "[Worker " << id << "] exiting\n";
}

int main() {
    std::ios::sync_with_stdio(false);

    std::string inputDir;
    std::cout << "Enter image directory: ";
    std::getline(std::cin, inputDir);

    if (inputDir.empty() || !fs::exists(inputDir) || !fs::is_directory(inputDir)) {
        std::cerr << "Invalid directory.\n";
        return 1;
    }

    std::string outputDir = (fs::path(inputDir) / "output").string();
    try { fs::create_directories(outputDir); }
    catch (...) {
        std::cerr << "Failed to create output directory: " << outputDir << "\n";
        return 1;
    }

    fs::path csvPath = fs::path(inputDir) / "result.csv";
    csvFile.open(csvPath, std::ios::out | std::ios::trunc);
    if (!csvFile.is_open()) {
        std::cerr << "Failed to open CSV file: " << csvPath << "\n";
        return 1;
    }
    csvFile << "ID,Filename,ExtractedText,ProcessingTime(ms)\n";

    unsigned int hw = std::thread::hardware_concurrency();
    unsigned int numWorkers = std::max(2u, hw ? hw : 2u);
    std::cout << "Using " << numWorkers << " worker threads\n";

    std::vector<std::thread> workers;
    for (unsigned int i = 0; i < numWorkers; ++i)
        workers.emplace_back(worker, i + 1, outputDir);

    std::thread prod(producer, inputDir, numWorkers);

    prod.join();
    for (auto& t : workers) t.join();

    csvFile.flush();
    csvFile.close();

    std::cout << "\nAll done!\nOutput images: " << outputDir
        << "\nCSV report: " << csvPath << "\n";
    return 0;
}
