#include <QMainWindow>
#include <QString>

class QPushButton;
class ClientWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ClientWindow(QWidget* parent = nullptr);
    void openDirectoryDialog();
    void createImageCard(const QString& imageId, const QString& filename, const QImage& image);
    static ClientWindow* instance;  

private slots:
    void openDirectoryDialog();

private:
    QVector<QImage> images;
    QStringList filenames;
    QGridLayout* gridLayout;
    QMap<QString, ImageCardWidgets> imageCards;
    QPushButton* button;
};


struct ImageCardWidgets {
    QWidget* card;
    QLabel* thumbnail;
    QLabel* filename;
    QLabel* status;
    QLabel* result;
    QProgressBar* progress;
};
