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

bool fin = false;
mutex queueMutex;
counting_semaphore<> sem(0);
queue<image> imageQueue;
vector<vector<result>> perThreadResults;
atomic<int> global_id{ 1 };
vector<string> queueFeed(vector<image>& images);

class OCRChannel final : public OCRService::Service {
    grpc::Status OCRRequest(grpc::ServerContext* context, grpc::ServerReaderWriter<response, imageList>* stream) override {
        imageList imgBatch;
        while (stream->Read(&imgBatch)) {
            response res;
            vector<image> images;

            for (const auto& img : imgBatch.images()) {
                images.push_back(img);
            }
            std::cout << "Images loaded and sent for processing..." << endl;
            vector<string>texts = queueFeed(images);
            std::cout << "Processing finished. Returning Result..." << endl;
            for (const auto& txt : texts) {
                res.add_inferences(txt); 
            }
            stream->Write(res);
            std::cout << "Results Sent." << endl;
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

    vector<result> thisThreadResults;

    while (true) {
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
            delete[] output; // match Tesseract allocation
            output = nullptr;
        }
        else {
            temp.extractedText = "OCR Failure";
        }

        thisThreadResults.push_back(move(temp));

        if (image) { pixDestroy(&image);  image = nullptr; }
        if (scaled) { pixDestroy(&scaled); scaled = nullptr; }
        if (gray) { pixDestroy(&gray);   gray = nullptr; }
        if (gamma) { pixDestroy(&gamma);  gamma = nullptr; }
    }
    perThreadResults[id] = move(thisThreadResults);

    ocr->End();
    delete ocr;
    ocr = nullptr;
}

vector<string> queueFeed(vector<image>& images) {
    int num_threads = 2;
    perThreadResults.resize(num_threads);

    vector<thread> threads;
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(workerThread, i);
    }

    for (const auto& img : images) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            imageQueue.push(img);
        }
        sem.release();
    }

    {
        lock_guard<mutex> lock(queueMutex);
        fin = true;
    }

    for (int i = 0; i < num_threads; ++i) {
        sem.release();
    }

    for (auto& t : threads) {
        t.join();
    }

    vector<result> Results;
    for (auto& tv : perThreadResults) {
        Results.insert(Results.end(),
            std::make_move_iterator(tv.begin()),
            std::make_move_iterator(tv.end()));
    }

    std::sort(Results.begin(), Results.end(),
        [](const result& a, const result& b) {
            return a.id < b.id;
        });

    vector<string> texts;
    for (int i = 0; i < Results.size(); ++i) {
        texts.push_back(Results[i].extractedText);
    }
        
    if (!imageQueue.empty()) {
        while (!imageQueue.empty()) {
            imageQueue.pop();
        }
    }
    fin = false;
    perThreadResults.clear();
    global_id = 1;
    return texts;
}

int main() {
    std::string server_address("0.0.0.0:50051");
    OCRChannel service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    server->Wait();
    return 0;
}