#include <grpcpp/grpcpp.h>
#include "ocr.pb.h"
#include "ocr.grpc.pb.h"
#include "clientwindow.h"
#include <QPushButton>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QBuffer>
#include <QDebug>
#include <QFrame>
#include <Qdir>
#include <QScrollArea>
#include <QLabel>
#include <QFileInfoList>

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderWriter;
using ocrservice::OCRService;
using ocrservice::image;
using ocrservice::imageList;
using ocrservice::response;

QFrame* canvas;
struct results {
    std::string filename;
    std::string text;
};

ocrservice::image qimageToProto(const QImage& img, const QString& filename, const QString& extension) {
    ocrservice::image protoImg;
    protoImg.set_filename(filename.toStdString());
    protoImg.set_format(extension.toStdString()); 

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, extension.toUpper().toUtf8().constData());
    protoImg.set_imgdata(bytes.constData(), bytes.size());

    protoImg.set_height(img.height());
    protoImg.set_width(img.width());
    protoImg.set_frame(0); // optional frame number
    return protoImg;
}

void ClientWindow::createImageCard(const QString& imageId, const QString& filename, const QImage& image) {
    ImageCardWidgets w;
    w.card = new QWidget();
    QVBoxLayout* v = new QVBoxLayout(w.card);
    v->setSpacing(4);
    v->setContentsMargins(4, 4, 4, 4);

    // Thumbnail
    w.thumbnail = new QLabel();
    w.thumbnail->setFixedSize(160, 120);
    QPixmap pm = QPixmap::fromImage(image).scaled(
        w.thumbnail->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation
    );
    w.thumbnail->setPixmap(pm);
    v->addWidget(w.thumbnail);

    // Filename
    w.filename = new QLabel(filename);
    v->addWidget(w.filename);

    // Status
    w.status = new QLabel("Processing...");
    v->addWidget(w.status);

    // Progress bar (0 → 100)
    w.progress = new QProgressBar();
    w.progress->setRange(0, 100);
    w.progress->setValue(0);
    v->addWidget(w.progress);

    // OCR text
    w.result = new QLabel("");
    w.result->setWordWrap(true);
    v->addWidget(w.result);

    // Add to grid
    int count = imageCards.size();
    int row = count / 4;
    int col = count % 4;

    gridLayout->addWidget(w.card, row, col);
    imageCards.insert(imageId, w);
}


void sendImages(QVector<QImage>& images, QStringList& filenames, QStringList& extensions) {
    std::string ip = "192.168.68.105:";
    std::string address = ip + "50051";
    auto stub = ocrservice::OCRService::NewStub(
        grpc::CreateChannel(address, grpc::InsecureChannelCredentials())
    );

    ClientContext context;
    std::shared_ptr<ClientReaderWriter<imageList, response>> stream(
        stub->OCRRequest(&context));

    ocrservice::imageList batch;
    for (int i = 0; i < images.size(); ++i) {
        auto* imgProto = batch.add_images();
        *imgProto = qimageToProto(images[i], filenames[i], extensions[i]);
    }

    if (!stream->Write(batch)) {
        qDebug() << "Failed to send batch!";
        return;
    }
	std::cout << "Images sent for processing..." << std::endl;
    stream->WritesDone();
    std::string last_filename;
    std::vector<results> OCRs;
    ocrservice::response res;

    int currentIndex = 0;

    while (stream->Read(&res)) {

        // For each OCR result text returned
        for (const auto& text : res.inferences()) {

            QString out = QString::fromStdString(text);
            qDebug() << "OCR result:" << out;

            QString id = QString::number(currentIndex);

            if (ClientWindow::instance->imageCards.contains(id)) {
                auto& card = ClientWindow::instance->imageCards[id];
                card.progress->setValue(100);
                card.status->setText("Done");
                card.result->setText(out);
            }

            currentIndex++;
        }
    }

    grpc::Status status = stream->Finish();
    if (!status.ok()) {
        qDebug() << "gRPC Error:" << QString::fromStdString(status.error_message());
    }
}


void ClientWindow::openDirectoryDialog() {
    QStringList extensions;
    QStringList toUpload = QFileDialog::getOpenFileNames(
        this,
        "Select Images to Process",
        "",
        "Images (*.png *.jpg *.jpeg *.bmp)"
    );

    images.clear();
    filenames.clear();
    extensions.clear();

    int index = 0;
    for (const QString& path : toUpload) {
        QImage img(path);

        if (!img.isNull()) {
            images.append(img);
            filenames << QFileInfo(path).fileName();
            extensions << QFileInfo(path).suffix();

            // image card creation
            QString imageId = QString::number(index++);
            createImageCard(imageId, QFileInfo(path).fileName(), img);

        }
        else {
            qDebug() << "Failed to load image:" << path;
        }
    }

    sendImages(images, filenames, extensions);
}

ClientWindow::ClientWindow(QWidget* parent) : QMainWindow(parent) {

    // Register the instance
    ClientWindow::instance = this;

    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout* layout = new QVBoxLayout(central);
    layout->setContentsMargins(30, 30, 30, 30);
    layout->setSpacing(20);

    button = new QPushButton("Upload Images", this);
    button->setMinimumHeight(60);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(button, 0, Qt::AlignTop);

    canvas = new QFrame(this);
    canvas->setStyleSheet("background-color: #333333;");
    canvas->setFrameShape(QFrame::Box);
    canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QScrollArea* scrollArea = new QScrollArea(canvas);
    scrollArea->setWidgetResizable(true);

    QWidget* canvasContainer = new QWidget();
    QGridLayout* gridLayout = new QGridLayout(canvasContainer);
    gridLayout->setSpacing(10);
    gridLayout->setContentsMargins(10, 10, 10, 10);

    scrollArea->setWidget(canvasContainer);

    // Add scroll area to your main canvas layout
    QVBoxLayout* canvasLayout = new QVBoxLayout(canvas);
    canvasLayout->addWidget(scrollArea);
    canvas->setLayout(canvasLayout);

    layout->addWidget(canvas);

    connect(button, &QPushButton::clicked, this, &ClientWindow::openDirectoryDialog);

}

ClientWindow* ClientWindow::instance = nullptr;

