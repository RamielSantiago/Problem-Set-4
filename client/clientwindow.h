
#include <QMainWindow>
#include <QString>
#include <QMap>
#include <QVector>
#include <QStringList>
#include <QGridLayout>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QImage>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "ocr.pb.h"
#include "ocr.grpc.pb.h"


class QPushButton;

struct ImageCardWidgets {
    QWidget* card;
    QLabel* thumbnail;
    QLabel* filename;
    QLabel* status;
    QLabel* result;
};

class ClientWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ClientWindow(QWidget* parent = nullptr);
    void createImageCard(const QString& imageId, const QString& filename, const QImage& image);
    void sendImages(QVector<QImage>& images, QStringList& filenames, QStringList& extensions);
    static ClientWindow* instance;
    QMap<QString, ImageCardWidgets> imageCards;

    QProgressBar* globalProgress;
    int totalImages = 0;
    int processedImages = 0;
    QStringList ocrResults;


    std::string ip = "192.168.68.107:";
    std::string address = ip + "50051";
    std::unique_ptr<ocrservice::OCRService::Stub> stub;

private slots:
    void openDirectoryDialog();

private:
    QVector<QImage> images;
    QStringList filenames;
    QStringList extensions;
    QGridLayout* gridLayout;
    QPushButton* button;
    QWidget* canvasContainer;
};