#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <grpcpp/grpcpp.h>
#include "ocr.pb.h"
#include "ocr.grpc.pb.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <semaphore>
#include <filesystem>
#include <memory>
#include <algorithm>

using namespace std;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::ServerReader;

using ocrservice::image;
using ocrservice::imageList;
using ocrservice::response;
using ocrservice::OCRService;

namespace fs = filesystem;

struct result {
    int id = 0;
    string filename;
    string extractedText;
};

int num_threads = 2;
atomic <bool> fin = false;
atomic<int> global_id{ 1 };
mutex queueMutex;
mutex resultMutex;
counting_semaphore<> sem(0);
counting_semaphore<> resSem(0);
queue<image> imageQueue;
queue<result> resultQueue;
vector<result> ocrResult;
void queueFeed(vector<image>& images);
void workerThread(int id);

class OCRChannel final : public OCRService::Service {

private:
    vector<thread> threads;
    condition_variable waitForJobs;

public:
    OCRChannel() {
        fin = false;
        threads.reserve(num_threads);
        for (int i = 0; i < num_threads; i++) {
            threads.emplace_back(workerThread, i);
        }
    }

    ~OCRChannel() {
        // Signal threads to stop
        fin = true;
        waitForJobs.notify_all();
        for (int i = 0; i < num_threads; i++) {
            sem.release();
        }
        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }
    }
    grpc::Status OCRRequest(grpc::ServerContext* context, grpc::ServerReaderWriter<response, imageList>* stream) override {
        imageList imgBatch;

        while (stream->Read(&imgBatch)) {
            try {
                vector<image> images;

                for (const auto& img : imgBatch.images()) {
                    images.push_back(img);
                }

                if (images.empty()) {
                    std::cerr << "Request denied. No images were received." << std::endl;
                    return grpc::Status(grpc::StatusCode::CANCELLED,
                        "Request denied. No images were received.");
                }

                std::cout << "Images loaded and sent for processing..." << endl;
                queueFeed(images);

                size_t jobSize = images.size();

                while (jobSize > 0) {
                    resSem.acquire();

                    result temp;
                    {
                        lock_guard<mutex> lock(resultMutex);
                        temp = resultQueue.front();
                        resultQueue.pop();
                    }

                    response res;
                    res.set_filename(temp.filename);
                    //res.set_extractedtext(temp.extractedText);
                    std::string cleaned_text = temp.extractedText;
                    std::replace(cleaned_text.begin(), cleaned_text.end(), '\n', ' '); // replace \n with space
                    res.set_extractedtext(cleaned_text);

                    if (!stream->Write(res)) {
                        std::cerr << "Client Disconnected. Request terminated." << std::endl;
                        return grpc::Status(grpc::StatusCode::CANCELLED,
                            "Client disconnected during stream");
                    }
                    jobSize--;
                }

                std::cout << "Results Sent." << endl;
            }
            catch (const std::exception& e) {
                std::cerr << "Exception in Server: " << e.what() << std::endl;
                return grpc::Status(grpc::StatusCode::INTERNAL, "Internal server error");
            }
        }
        return grpc::Status::OK;
    }
};

void workerThread(int id) {
    tesseract::TessBaseAPI* ocr = new tesseract::TessBaseAPI();

    if (ocr->Init(NULL, "eng", tesseract::OEM_LSTM_ONLY)) {
        std::cerr << "Could not initialize Tesseract for Thread " << id << endl;
        delete ocr;
        return;
    }

    ocr->SetPageSegMode(tesseract::PSM_AUTO);
    ocr->SetVariable("preserve_interword_spaces", "1");

    while (true) {
        try {
            sem.acquire();
            ocrservice::image img;
            char* output = nullptr;

            {
                lock_guard<mutex> lock(queueMutex);
                if (imageQueue.empty()) {
                    if (fin) {
                        cout << "Image Queue empty. Exiting Thread " << id << "..." << endl;
                        break;
                    }
                    else {
                        continue;
                    }
                }

                img = imageQueue.front();
                imageQueue.pop();
            }

            const std::string& imgData = img.imgdata();
            if (imgData.size() < 10) {
                std::cout << "WARN: image data too small (" << imgData.size()
                    << ") for " << img.filename() << "\n";
                continue;
            }

            const l_uint8* dataPtr = reinterpret_cast<const l_uint8*>(imgData.data());
            size_t dataSize = imgData.size();

            Pix* image = nullptr;
            Pix* scaled = nullptr;
            Pix* gray = nullptr;
            Pix* gamma = nullptr;

            image = pixReadMem(dataPtr, dataSize);
            if (!image) {
                std::cout << "ERROR: pixReadMem failed for " << img.filename() << "\n";
                continue;
            }

            int width = pixGetWidth(image);
            int height = pixGetHeight(image);

            if (width < 1000 || height < 1000) {
                scaled = pixScale(image, 1.5, 1.5);
            }
            else {
                scaled = pixClone(image);
            }

            if (!scaled) {
                std::cout << "ERROR: Scaling/clone failed for " << img.filename() << "\n";
                pixDestroy(&image);
                continue;
            }

            gray = pixConvertRGBToGray(scaled, 0.0f, 0.0f, 0.0f);
            if (!gray) {
                std::cout << "ERROR: Grayscale conversion failed for " << img.filename() << "\n";
                pixDestroy(&image);
                pixDestroy(&scaled);
                continue;
            }

            gamma = pixGammaTRC(nullptr, gray, 1.2f, 0, 255);
            if (!gamma) {
                std::cout << "ERROR: Gamma correction failed for " << img.filename() << "\n";
                pixDestroy(&image);
                pixDestroy(&scaled);
                pixDestroy(&gray);
                continue;
            }

            ocr->SetImage(gamma);
            ocr->SetSourceResolution(300);

            output = ocr->GetUTF8Text();

            result temp;
            temp.id = global_id++;
            temp.filename = img.filename();
            if (output) {
                temp.extractedText = std::string(output);
                delete[] output;
                output = nullptr;
            }
            else {
                temp.extractedText = "OCR Failure";
            }

            {
                lock_guard<mutex> lock(resultMutex);
                resultQueue.push(temp);
            }

            resSem.release();

            if (image) { pixDestroy(&image);  image = nullptr; }
            if (scaled) { pixDestroy(&scaled); scaled = nullptr; }
            if (gray) { pixDestroy(&gray);   gray = nullptr; }
            if (gamma) { pixDestroy(&gamma);  gamma = nullptr; }
        }
        catch (const std::exception& e) {
            std::cerr << "Exception in Worker Thread " << id << ": " << e.what() << std::endl;
        }
    }

    ocr->End();
    delete ocr;
    ocr = nullptr;
}

void queueFeed(vector<image>& images) {

    for (const auto& img : images) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            imageQueue.push(img);
        }
        sem.release();
    }
}

int main() {
    std::string server_address("0.0.0.0:50051");
    OCRChannel service;

    ServerBuilder builder;
    builder.SetMaxReceiveMessageSize(32 * 1024 * 1024); // 32MB
    builder.SetMaxSendMessageSize(32 * 1024 * 1024);
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    server->Wait();
    return 0;
}