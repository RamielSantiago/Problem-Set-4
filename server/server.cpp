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
};

bool fin = false;
mutex queueMutex;
counting_semaphore<> sem(0);
queue<string> imageQueue;
vector<vector<result>> perThreadResults;
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

        int width = pixGetWidth(image);
        int height = pixGetHeight(image);
        Pix* scaled = (width < 1000 || height < 1000) ? pixScale(image, 1.5, 1.5) : pixClone(image);
        Pix* gray = pixConvertRGBToGray(scaled, 0.0f, 0.0f, 0.0f);
        Pix* gamma = pixGammaTRC(nullptr, gray, 1.2f, 0, 255);

        ocr->SetImage(gamma);
        ocr->SetSourceResolution(300);

        output = ocr->GetUTF8Text();

        result temp;
        temp.id = global_id++;
        temp.filename = fs::path(path).filename().string();
        temp.extractedText = (output ? string(output) : "OCR Failure");
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

int queueFeed() {
    string dir;
    cout << "Input the directory of images to process: ";
    cin >> dir;

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
    return 0;
}

int main() {
	return 0;
}