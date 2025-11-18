#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
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

using namespace std;
namespace fs = filesystem;

struct result {
    int id = 0;
    string filename;
    string extractedText;
    long long processingTime = 0;
};

bool fin = false;
float STTotalDuration = 0;
float MTTotalDuration = 0;
mutex csvMutex;
mutex queueMutex;
counting_semaphore<> sem(0);
queue<string> imageQueue;
vector<vector<result>> perThreadResults ;
atomic<int> global_id{ 1 };

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
        string path;
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

            path = imageQueue.front();
            imageQueue.pop();
        }

        Pix* image = pixRead(path.c_str());
        if (!image) {
            cout << "Image could not be processed...";
            continue;
        }
        auto start = chrono::high_resolution_clock::now();
        
        int width = pixGetWidth(image);
        int height = pixGetHeight(image);
        Pix* scaled = (width < 1000 || height < 1000) ? pixScale(image, 1.5, 1.5) : pixClone(image);
        Pix* gray = pixConvertRGBToGray(scaled, 0.0f, 0.0f, 0.0f);
        Pix* gamma = pixGammaTRC(nullptr, gray, 1.2f, 0, 255);

        ocr->SetImage(gamma);
        ocr->SetSourceResolution(300);

        output = ocr->GetUTF8Text();

        auto end = chrono::high_resolution_clock::now();
        auto OCRElapsed = chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        result temp;
        temp.id = global_id++;
        temp.filename = fs::path(path).filename().string();
        temp.extractedText = (output ? string(output) : "OCR Failure");
        temp.processingTime = OCRElapsed;
        delete[] output;

        thisThreadResults.push_back(move(temp));

        pixDestroy(&image);
        pixDestroy(&scaled);
        pixDestroy(&gray);
        pixDestroy(&gamma);
    }
    perThreadResults[id] = move(thisThreadResults);

    ocr->End();
    delete ocr;
    ocr = nullptr;
}

void singleThread(queue<string> copyQueue) {
    tesseract::TessBaseAPI* ocr = new tesseract::TessBaseAPI();

    if (ocr->Init(NULL, "eng", tesseract::OEM_LSTM_ONLY)) {
        std::cerr << "Could not initialize Tesseract for Single Thread " <<  endl;
        delete ocr;
        return;
    }

    ocr->SetPageSegMode(tesseract::PSM_AUTO);
    ocr->SetVariable("preserve_interword_spaces", "1");
    size_t size = copyQueue.size();

    for (int i = 0; i < size; i++) {
        string path;
        char* output = nullptr;

        path = copyQueue.front();
        copyQueue.pop();   

        Pix* image = pixRead(path.c_str());
        if (!image) {
            cout << "Image could not be processed...";
            continue;
        }  

        auto start = chrono::high_resolution_clock::now();

        Pix* resized = pixScale(image, 2.0, 2.0);
        Pix* gray = pixConvertRGBToGray(resized, 0.0, 0.0, 0.0);

        ocr->SetImage(gray);
        ocr->SetSourceResolution(300);

        output = ocr->GetUTF8Text();

        auto end = chrono::high_resolution_clock::now();
        auto OCRElapsed = chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        STTotalDuration += OCRElapsed;

        string text = (output ? string(output) : "OCR Failure");
        delete[] output;

        pixDestroy(&image);
        pixDestroy(&resized);
        pixDestroy(&gray);
    }
    ocr->End();
    delete ocr;
    ocr = nullptr;
}

// D:\Desktop\School\STDISCM\PS3\images
// D:\Desktop\School\STDISCM\HW2\dataset
int main() {

    {
        ofstream csv("output.csv", ios::out | ios::binary);
        csv << "\xEF\xBB\xBF";  // UTF-8 BOM
        csv << "ID, Image, Text, ProcessingTime(ms)\n";
    }

    string dir;
    cout << "Input the directory of images to process: ";
    cin >> dir;
    
    queue<string> copyQueue;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            string ext = entry.path().extension().string();
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tif" || ext == ".bmp") {
                copyQueue.push(entry.path().string());
            }
        }
    }
    
    singleThread(copyQueue);

    int num_threads = 2;
    perThreadResults.resize(num_threads);

    vector<thread> threads;
    threads.reserve(num_threads);
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(workerThread, i);
    }

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            string ext = entry.path().extension().string();
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tif" || ext == ".bmp") {
                lock_guard<mutex> lock(queueMutex);
                imageQueue.push(entry.path().string());
                sem.release();
            }
        }
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

    ofstream csv("output.csv", ios::app | ios::binary);
    for (int i = 0; i < Results.size(); i++) {
        for (auto& c : Results[i].extractedText) {
            if (c == '\n' || c == '\r') {
                c = ' ';
            }
        }
        MTTotalDuration += Results[i].processingTime;
        csv << Results[i].id << ","
            << Results[i].filename << ","
            << quoted(Results[i].extractedText) << ","
            << Results[i].processingTime << "ms\n";
    }
    csv << "Total Duration for Single Thread" << "," << STTotalDuration << " ms\n";
    csv << "Total Duration for Multi Thread" << "," << MTTotalDuration << " ms\n";
    csv << "Performance Gain from Multithreading" << "," << (STTotalDuration/MTTotalDuration) * 100 << "%";

    return 0;
}
