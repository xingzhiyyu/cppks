#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QDateEdit>
#include <QComboBox>
#include <QLabel>
#include <QTableWidget>
#include <QList>
#include <QMap>
#include <QSet>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

// Train mock structure
struct Train {
    QString id;
    QString from;
    QString to;
    QString depTime;
    QString arrTime;
    QString duration;
    int priceBusiness;
    int priceFirst;
    int priceSecond;
    int seatsBusiness;
    int seatsFirst;
    int seatsSecond;
};

// Passenger mock structure
struct Passenger {
    QString name;
    QString idCard;
    QString seatClass;
    QString seatNo;
};

struct UserAccount {
    QString username;
    QString password;
};

struct Order {
    QString code;
    QString username;
    QString passengerName;
    QString idCard;
    QString trainId;
    QString from;
    QString to;
    QString date;
    QString depTime;
    QString arrTime;
    QString seatClass;
    QString seatNo;
    QString status;
    int fare;
    int refundFee;
    int changeFee;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onSearchClicked();
    void onSwapStations();
    void onBookSelectedTrain();
    void onBookTrain(const QString &trainId);
    void onAddPassenger();
    void onRemovePassenger();
    void onSeatCellClicked(int row, int column);
    void onConfirmBooking();
    void onResetBooking();
    void onLogin();
    void onRegister();
    void onLogout();
    void onRefundTicket();
    void onChangeTicket();
    void onAccountCenter();
    void onDirectTicket();
    void onExportManifest();


private:
    Ui::MainWindow *ui;

    // State Variables
    QList<Train> allTrains;
    QList<Train> filteredTrains;
    Train selectedTrain;
    QList<Passenger> passengers;
    QSet<QString> selectedSeats;
    QMap<QString, UserAccount> accounts;
    QList<Order> orders;
    QMap<QString, QSet<QString>> soldSeatsByRun;
    QString currentUsername;
    int activeStep;

    int selectedTrainRow = -1;
    int selectedOrderRow = -1;
    int selectedAccountOrderRow = -1;
    QString selectedCarriageNo;

    // Global Stylesheet string
    QString getStylesheet();

    // UI Initializers
    void initMockData();
    void setupUiLayout();
    void updateStepProgress();

    // Layout Helpers
    void renderTrainList(const QList<Train> &trains);
    void setupCabinGrid();
    void setupCarriageMap();
    void updateCarriageSelection(const QString &seatClass);
    void updateSummary();
    void renderVirtualTickets();
    void updateAccountUi();
    void renderCurrentUserOrders();
    void renderOrdersIntoTable(QTableWidget *table, int *selectedRow);
    int selectedOrderIndex() const;
    QString runKey(const QString &date, const QString &trainId) const;
    int fareForSeatClass(const Train &train, const QString &seatClass) const;
    bool isAfterDeparture(const Order &order) const;
    bool findTrainById(const QString &trainId, Train *train) const;
    QString autoSeatForRun(const QString &date, const QString &trainId, const QString &seatClass) const;
    QStringList seatColumnsForClass(const QString &seatClass) const;
    QString seatClassForCarriage(const QString &carriageNo) const;
    void refreshSeatColors();
    void applyOrderRowHighlight(QTableWidget *table, int selectedRow);
};
#endif // MAINWINDOW_H
