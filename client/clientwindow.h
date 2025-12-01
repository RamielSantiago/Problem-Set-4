
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
    static ClientWindow* instance;
    QMap<QString, ImageCardWidgets> imageCards;

    QProgressBar* globalProgress;
    int totalImages = 0;
    int processedImages = 0;

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

