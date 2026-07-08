#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStandardItemModel>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QHash>
#include <QDate>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

// Helper function to obscure ID card numbers
static QString obscureIdCard(const QString &id) {
    if (id.length() < 8) return id;
    return id.left(4) + "**********" + id.right(4);
}

static QTableWidgetItem *readonlyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

static bool trainHasTickets(const Train &train)
{
    const bool isHighSpeed = train.id.startsWith('G') || train.id.startsWith('D');
    return (train.seatsBusiness + train.seatsFirst + train.seatsSecond) > 0 || !isHighSpeed;
}

static bool trainSupportsSeatClass(const Train &train, const QString &seatClass)
{
    const bool isHighSpeed = train.id.startsWith('G') || train.id.startsWith('D');
    if (isHighSpeed) {
        if (seatClass == "商务座") return train.priceBusiness > 0 && train.seatsBusiness > 0;
        if (seatClass == "一等座") return train.priceFirst > 0 && train.seatsFirst > 0;
        if (seatClass == "二等座") return train.priceSecond > 0 && train.seatsSecond > 0;
        return false;
    }

    if (seatClass == "软卧") return train.priceFirst > 0 && train.seatsFirst > 0;
    if (seatClass == "硬卧") return train.priceSecond > 0 && train.seatsSecond > 0;
    if (seatClass == "硬座") return train.priceSecond > 80 && train.seatsSecond > 0;
    return false;
}

static bool trainDefinesSeatClass(const Train &train, const QString &seatClass)
{
    const bool isHighSpeed = train.id.startsWith('G') || train.id.startsWith('D');
    if (isHighSpeed) {
        if (seatClass == "商务座") return train.priceBusiness > 0;
        if (seatClass == "一等座") return train.priceFirst > 0;
        if (seatClass == "二等座") return train.priceSecond > 0;
        return false;
    }

    if (seatClass == "软卧") return train.priceFirst > 0;
    if (seatClass == "硬卧") return train.priceSecond > 0;
    if (seatClass == "硬座") return train.priceSecond > 80;
    return false;
}

static QStringList carriagesForSeatClass(const Train &train, const QString &seatClass)
{
    const bool isHighSpeed = train.id.startsWith('G') || train.id.startsWith('D');
    if (isHighSpeed) {
        if (seatClass == "商务座" && trainSupportsSeatClass(train, seatClass)) return {"01车"};
        if (seatClass == "一等座" && trainSupportsSeatClass(train, seatClass)) return {"02车", "03车"};
        if (seatClass == "二等座" && trainSupportsSeatClass(train, seatClass)) return {"04车", "05车", "06车"};
        return {};
    }

    if (seatClass == "软卧" && trainSupportsSeatClass(train, seatClass)) return {"01车"};
    if (seatClass == "硬卧" && trainSupportsSeatClass(train, seatClass)) return {"02车", "03车"};
    if (seatClass == "硬座" && trainSupportsSeatClass(train, seatClass)) return {"04车", "05车"};
    return {};
}

static QStringList seatColumnsForSeatClassName(const QString &seatClass)
{
    if (seatClass == "商务座") {
        return {"A", "F"};
    }
    if (seatClass == "一等座") {
        return {"A", "C", "D", "F"};
    }
    if (seatClass == "软卧") {
        return {"上铺1", "下铺1"};
    }
    if (seatClass == "硬卧") {
        return {"上铺1", "中铺1", "下铺1", "上铺2", "下铺2"};
    }
    return {"A", "B", "C", "D", "F"};
}

static int seatCapacityForClass(const Train &train, const QString &seatClass)
{
    return carriagesForSeatClass(train, seatClass).size() * 6 * seatColumnsForSeatClassName(seatClass).size();
}

static int availableSeatsForClass(const Train &train,
                                  const QString &seatClass,
                                  const QSet<QString> &soldSeats,
                                  const QSet<QString> &reservedSeats = {})
{
    const QStringList carriages = carriagesForSeatClass(train, seatClass);
    if (carriages.isEmpty()) {
        return 0;
    }

    int unavailable = 0;
    const auto countSeat = [&carriages, &unavailable](const QString &seatNo) {
        if (carriages.contains(seatNo.section(' ', 0, 0))) {
            ++unavailable;
        }
    };
    for (const QString &seatNo : soldSeats) {
        countSeat(seatNo);
    }
    for (const QString &seatNo : reservedSeats) {
        countSeat(seatNo);
    }

    return qMax(0, seatCapacityForClass(train, seatClass) - unavailable);
}

static int totalAvailableSeatsForRun(const Train &train, const QSet<QString> &soldSeats)
{
    const bool isHighSpeed = train.id.startsWith('G') || train.id.startsWith('D');
    const QStringList classes = isHighSpeed
        ? QStringList{"商务座", "一等座", "二等座"}
        : QStringList{"软卧", "硬卧", "硬座"};

    int total = 0;
    for (const QString &seatClass : classes) {
        total += availableSeatsForClass(train, seatClass, soldSeats);
    }
    return total;
}

static QString trainSeatSummary(const Train &train, const QSet<QString> &soldSeats)
{
    const bool isHighSpeed = train.id.startsWith('G') || train.id.startsWith('D');
    const auto formatSeat = [](const QString &name, int price, int count) {
        if (price == 0) return QString();
        const QString countStr = (count > 9) ? "有票" : (count > 0 ? QString("%1张").arg(count) : "无票");
        return QString("%1 ¥%2 (%3)").arg(name).arg(price).arg(countStr);
    };

    QStringList seats;
    const QString seat1 = formatSeat(isHighSpeed ? "商务座" : "软卧",
                                     isHighSpeed ? train.priceBusiness : train.priceFirst,
                                     availableSeatsForClass(train, isHighSpeed ? "商务座" : "软卧", soldSeats));
    const QString seat2 = formatSeat(isHighSpeed ? "一等座" : "硬卧",
                                     isHighSpeed ? train.priceFirst : train.priceSecond,
                                     availableSeatsForClass(train, isHighSpeed ? "一等座" : "硬卧", soldSeats));
    const QString seat3 = formatSeat(isHighSpeed ? "二等座" : "硬座",
                                     isHighSpeed ? train.priceSecond : train.priceSecond - 80,
                                     availableSeatsForClass(train, isHighSpeed ? "二等座" : "硬座", soldSeats));
    if (!seat1.isEmpty()) seats << seat1;
    if (!seat2.isEmpty()) seats << seat2;
    if (!seat3.isEmpty()) seats << seat3;
    return seats.join(" / ");
}

static void setRowColors(QTableWidget *table, int selectedRow)
{
    for (int r = 0; r < table->rowCount(); ++r) {
        const bool unavailable = table->item(r, 0) && table->item(r, 0)->data(Qt::UserRole + 2).toBool();
        bool isSelected = (r == selectedRow);
        QColor bg = unavailable ? QColor("#e5e7eb") : (isSelected ? QColor("#9ca3af") : QColor("#ffffff"));
        QColor fg = unavailable ? QColor("#9ca3af") : (isSelected ? QColor("#181586") : QColor("#111827"));
        for (int c = 0; c < table->columnCount(); ++c) {
            QTableWidgetItem *item = table->item(r, c);
            if (item) {
                item->setBackground(bg);
                item->setForeground(fg);
            }
        }
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , activeStep(0)
{
    ui->setupUi(this);
    
    // Set window attributes
    setWindowTitle("星轨快线 - 智能购票系统");
    resize(1080, 750);
    setMinimumSize(1020, 700);

    initMockData();
    setupUiLayout();
    
    // Apply Stylesheet
    setStyleSheet(getStylesheet());
    
    // Set initial page
    ui->stackedWidget->setCurrentIndex(0);
    ui->timeLabel->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    auto *clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, [this]() {
        ui->timeLabel->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    });
    clockTimer->start(1000);
    updateStepProgress();
    renderTrainList(allTrains);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::initMockData()
{
    // Generate beautiful sample train data
    allTrains = {
        {"G101", "北京南", "上海虹桥", "07:00", "12:18", "5时18分", 1748, 930, 553, 5, 14, 45},
        {"G105", "北京南", "上海虹桥", "09:05", "14:35", "5时30分", 1748, 930, 553, 0, 8, 120},
        {"G3", "北京南", "上海虹桥", "14:00", "18:38", "4时38分", 1748, 930, 553, 2, 22, 0},
        {"D311", "北京南", "上海虹桥", "21:16", "09:08", "11时52分", 0, 850, 320, 0, 6, 24},
        
        {"G12", "上海虹桥", "北京南", "10:00", "14:40", "4时40分", 1748, 930, 553, 8, 15, 66},
        {"G18", "上海虹桥", "北京南", "16:30", "21:15", "4时45分", 1748, 930, 553, 1, 0, 95},
        
        {"G79", "北京西", "广州南", "10:00", "18:02", "8时02分", 2910, 1495, 963, 6, 12, 50},
        {"G80", "广州南", "北京西", "12:15", "20:25", "8时10分", 2910, 1495, 963, 3, 5, 80},
        {"G980","北京西", "香港西九龙", "10:00", "18:45", "8时45分", 2945, 1500, 980, 3, 7, 180},
        {"K105", "北京", "深圳东", "23:56", "05:08", "29时12分", 0, 480, 255, 0, 18, 40},
        {"Z13", "北京", "广州", "20:15", "16:20", "20时05分", 0, 450, 240, 0, 10, 35}
    };
}

void MainWindow::setupUiLayout()
{
    // Setup signals and slots
    connect(ui->swapBtn, &QPushButton::clicked, this, &MainWindow::onSwapStations);
    connect(ui->searchBtn, &QPushButton::clicked, this, &MainWindow::onSearchClicked);
    connect(ui->directTicketBtn, &QPushButton::clicked, this, &MainWindow::onDirectTicket);
    connect(ui->manifestBtn, &QPushButton::clicked, this, &MainWindow::onExportManifest);
    connect(ui->bookSelectedBtn, &QPushButton::clicked, this, &MainWindow::onBookSelectedTrain);
    connect(ui->trainTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        onBookSelectedTrain();
    });
    connect(ui->addPassengerBtn, &QPushButton::clicked, this, &MainWindow::onAddPassenger);
    connect(ui->removePassengerBtn, &QPushButton::clicked, this, &MainWindow::onRemovePassenger);
    connect(ui->seatTable, &QTableWidget::cellClicked, this, &MainWindow::onSeatCellClicked);
    connect(ui->carriageTable, &QTableWidget::cellClicked, this, [this](int, int column) {
        QTableWidgetItem *item = ui->carriageTable->item(0, column);
        if (!item || item->data(Qt::UserRole + 2).toBool()) {
            return;
        }
        const QString seatClass = item->data(Qt::UserRole).toString();
        const QString previousCarriageNo = selectedCarriageNo;
        selectedCarriageNo = item->data(Qt::UserRole + 1).toString();
        if (previousCarriageNo != selectedCarriageNo) {
            selectedSeats.clear();
        }
        const int idx = ui->newPassengerSeatClass->findText(seatClass);
        if (idx >= 0) {
            ui->newPassengerSeatClass->setCurrentIndex(idx);
        }
        updateCarriageSelection(seatClass);
        setupCabinGrid();
        updateSummary();
    });
    connect(ui->newPassengerSeatClass, &QComboBox::currentTextChanged, this, [this]() {
        if (ui->stackedWidget->currentIndex() == 1) {
            selectedSeats.clear();
            updateCarriageSelection(ui->newPassengerSeatClass->currentText());
            setupCabinGrid();
            updateSummary();
        }
    });
    connect(ui->backBtn, &QPushButton::clicked, this, &MainWindow::onResetBooking);
    connect(ui->confirmBookingBtn, &QPushButton::clicked, this, &MainWindow::onConfirmBooking);
    connect(ui->doneBtn, &QPushButton::clicked, this, &MainWindow::onResetBooking);
    connect(ui->loginBtn, &QPushButton::clicked, this, &MainWindow::onLogin);
    connect(ui->registerBtn, &QPushButton::clicked, this, &MainWindow::onRegister);
    connect(ui->logoutBtn, &QPushButton::clicked, this, &MainWindow::onLogout);
    connect(ui->accountCenterBtn, &QPushButton::clicked, this, &MainWindow::onAccountCenter);
    connect(ui->refundBtn, &QPushButton::clicked, this, &MainWindow::onRefundTicket);
    connect(ui->changeTicketBtn, &QPushButton::clicked, this, &MainWindow::onChangeTicket);
    connect(ui->accountRefundBtn, &QPushButton::clicked, this, &MainWindow::onRefundTicket);
    connect(ui->accountChangeTicketBtn, &QPushButton::clicked, this, &MainWindow::onChangeTicket);
    connect(ui->accountBackHomeBtn, &QPushButton::clicked, this, &MainWindow::onResetBooking);

    // Setup input limits
    ui->dateInput->setDisplayFormat("yyyy-MM-dd");
    ui->dateInput->setCalendarPopup(true);
    ui->dateInput->setButtonSymbols(QAbstractSpinBox::NoButtons);
    ui->dateInput->setDate(QDate::currentDate());
    ui->dateInput->setMinimumDate(QDate::currentDate());
    ui->dateInput->setMaximumDate(QDate::currentDate().addYears(1));

    ui->newPassengerId->setValidator(new QRegularExpressionValidator(QRegularExpression("^[0-9xX]{0,18}$"), this));

    ui->stepProgressLabel->setMinimumWidth(0);
    ui->stepProgressLabel->setMaximumWidth(QWIDGETSIZE_MAX);
    ui->stepProgressLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui->stepProgressLabel->setWordWrap(false);

    ui->trainTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->trainTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->trainTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->trainTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    ui->trainTable->setSelectionMode(QAbstractItemView::NoSelection);
    ui->trainTable->setFocusPolicy(Qt::NoFocus);

    // Table sizing is tuned in code; static columns and behavior live in mainwindow.ui.
    ui->passengerTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->passengerTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->passengerTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->passengerTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

    ui->seatTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->seatTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    ui->seatTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->seatTable->setFixedHeight(352);
    ui->seatTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->seatTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->seatTable->setShowGrid(true);
    ui->carriageTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->carriageTable->verticalHeader()->setDefaultSectionSize(74);
    ui->carriageTable->setRowHeight(0, 74);
    ui->carriageTable->setCursor(Qt::PointingHandCursor);

    ui->ticketTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->ticketTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->ticketTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->ticketTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    ui->ticketTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->ticketTable->setSelectionMode(QAbstractItemView::NoSelection);
    ui->ticketTable->setFocusPolicy(Qt::NoFocus);
    ui->ticketTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    ui->accountOrderTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->accountOrderTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->accountOrderTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->accountOrderTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    ui->accountOrderTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->accountOrderTable->setSelectionMode(QAbstractItemView::NoSelection);
    ui->accountOrderTable->setFocusPolicy(Qt::NoFocus);
    ui->accountOrderTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    updateAccountUi();
    connect(ui->trainTable, &QTableWidget::cellClicked, this, [this](int row, int) {
        QTableWidgetItem *idItem = ui->trainTable->item(row, 0);
        if (!idItem || idItem->data(Qt::UserRole + 2).toBool()) {
            return;
        }
        selectedTrainRow = row;
        applyOrderRowHighlight(ui->trainTable, selectedTrainRow);
    });
    connect(ui->ticketTable, &QTableWidget::cellClicked, this, [this](int row, int) {
        selectedOrderRow = row;
        applyOrderRowHighlight(ui->ticketTable, selectedOrderRow);
    });
    connect(ui->accountOrderTable, &QTableWidget::cellClicked, this, [this](int row, int) {
        selectedAccountOrderRow = row;
        applyOrderRowHighlight(ui->accountOrderTable, selectedAccountOrderRow);
    });
}

void MainWindow::updateStepProgress()
{
    QString progressHtml;
    if (activeStep == 0) {
        progressHtml = "<font color='#4f46e5'><b>1. 搜索车次</b></font> <font color='#d1d5db'>───</font> <font color='#9ca3af'>2. 乘客选座</font> <font color='#d1d5db'>───</font> <font color='#9ca3af'>3. 电子车票</font>";
    } else if (activeStep == 1) {
        progressHtml = "<font color='#9ca3af'>1. 搜索车次</font> <font color='#d1d5db'>───</font> <font color='#4f46e5'><b>2. 乘客选座</b></font> <font color='#d1d5db'>───</font> <font color='#9ca3af'>3. 电子车票</font>";
    } else {
        progressHtml = "<font color='#9ca3af'>1. 搜索车次</font> <font color='#d1d5db'>───</font> <font color='#9ca3af'>2. 乘客选座</font> <font color='#d1d5db'>───</font> <font color='#4f46e5'><b>3. 电子车票</b></font>";
    }
    ui->stepProgressLabel->setText(progressHtml);
}

void MainWindow::onSwapStations()
{
    QString from = ui->fromInput->text();
    ui->fromInput->setText(ui->toInput->text());
    ui->toInput->setText(from);
}

void MainWindow::onSearchClicked()
{
    QString from = ui->fromInput->text().trimmed();
    QString to = ui->toInput->text().trimmed();
    int typeIdx = ui->trainTypeCombo->currentIndex();

    filteredTrains.clear();
    
    for (const auto &train : allTrains) {
        if (!train.from.contains(from) || !train.to.contains(to)) {
            continue;
        }

        if (typeIdx == 1) {
            if (!train.id.startsWith('G') && !train.id.startsWith('D')) continue;
        } else if (typeIdx == 2) {
            if (train.id.startsWith('G') || train.id.startsWith('D')) continue;
        }

        filteredTrains.append(train);
    }

    ui->resultsTitleLabel->setText(QString("查询结果: %1 ➔ %2 (%3 个符合车次)").arg(from).arg(to).arg(filteredTrains.size()));
    renderTrainList(filteredTrains);
}

void MainWindow::renderTrainList(const QList<Train> &trains)
{
    filteredTrains = trains;
    ui->trainTable->setRowCount(trains.size());
    ui->bookSelectedBtn->setEnabled(false);
    const QString date = ui->dateInput->date().toString("yyyy-MM-dd");

    for (int row = 0; row < trains.size(); ++row) {
        const Train &train = trains[row];
        const QSet<QString> soldSeats = soldSeatsByRun.value(runKey(date, train.id));
        const int availableCount = totalAvailableSeatsForRun(train, soldSeats);
        const bool available = trainHasTickets(train) && availableCount > 0;
        auto *idItem = readonlyItem(train.id);
        idItem->setData(Qt::UserRole, train.id);
        idItem->setData(Qt::UserRole + 2, !available);
        ui->trainTable->setItem(row, 0, idItem);
        ui->trainTable->setItem(row, 1, readonlyItem(QString("%1\n%2").arg(train.depTime, train.from)));
        ui->trainTable->setItem(row, 2, readonlyItem(QString("%1\n%2").arg(train.arrTime, train.to)));
        ui->trainTable->setItem(row, 3, readonlyItem(train.duration));
        ui->trainTable->setItem(row, 4, readonlyItem(QString("%1 / 剩余座位 %2").arg(trainSeatSummary(train, soldSeats)).arg(availableCount)));

        auto *statusItem = readonlyItem(available ? "可预订" : "已售罄");
        statusItem->setForeground(available ? QBrush(QColor("#0f766e")) : QBrush(QColor("#ef4444")));
        ui->trainTable->setItem(row, 5, statusItem);
        ui->trainTable->setRowHeight(row, 58);

        if (!available) {
            for (int c = 0; c < ui->trainTable->columnCount(); ++c) {
                QTableWidgetItem *item = ui->trainTable->item(row, c);
                if (item) {
                    item->setBackground(QBrush(QColor("#e5e7eb")));
                    item->setForeground(QBrush(QColor("#9ca3af")));
                    item->setToolTip("该车次已售罄");
                }
            }
        }
    }

    selectedTrainRow = -1;
    for (int row = 0; row < ui->trainTable->rowCount(); ++row) {
        QTableWidgetItem *idItem = ui->trainTable->item(row, 0);
        if (idItem && !idItem->data(Qt::UserRole + 2).toBool()) {
            selectedTrainRow = row;
            break;
        }
    }

    if (selectedTrainRow >= 0) {
        ui->bookSelectedBtn->setEnabled(true);
        for (int c = 0; c < ui->trainTable->columnCount(); ++c) {
            QTableWidgetItem *item = ui->trainTable->item(selectedTrainRow, c);
            if (item) {
                item->setBackground(QBrush(QColor("#9ca3af")));
                item->setForeground(QBrush(QColor("#181586")));
            }
        }
    }
}

void MainWindow::onBookSelectedTrain()
{
    const int row = selectedTrainRow;
    if (row < 0 || row >= ui->trainTable->rowCount()) {
        QMessageBox::information(this, "请选择车次", "请先在车次表中选择一个可预订车次。");
        return;
    }

    const QString trainId = ui->trainTable->item(row, 0)->data(Qt::UserRole).toString();
    if (ui->trainTable->item(row, 0)->data(Qt::UserRole + 2).toBool()) {
        QMessageBox::warning(this, "车票售罄", "该车次已售罄，请选择其他车次。");
        return;
    }
    for (const auto &train : filteredTrains) {
        if (train.id == trainId && !trainHasTickets(train)) {
            QMessageBox::warning(this, "车票售罄", "该车次已售罄，请选择其他车次。");
            return;
        }
    }

    onBookTrain(trainId);
}

void MainWindow::onBookTrain(const QString &trainId)
{
    for (const auto &train : allTrains) {
        if (train.id == trainId) {
            selectedTrain = train;
            break;
        }
    }

    passengers.clear();
    selectedSeats.clear();
    ui->passengerTable->setRowCount(0);
    ui->newPassengerName->clear();
    ui->newPassengerId->clear();

    ui->newPassengerSeatClass->clear();
    auto *seatClassModel = qobject_cast<QStandardItemModel *>(ui->newPassengerSeatClass->model());
    int firstAvailableSeatClass = -1;
    const auto addSeatClassOption = [this, seatClassModel, &firstAvailableSeatClass](const QString &seatClass) {
        if (!trainDefinesSeatClass(selectedTrain, seatClass)) {
            return;
        }

        const bool available = trainSupportsSeatClass(selectedTrain, seatClass);
        ui->newPassengerSeatClass->addItem(available ? seatClass : QString("%1（无票）").arg(seatClass), seatClass);
        const int idx = ui->newPassengerSeatClass->count() - 1;
        if (!available && seatClassModel) {
            if (auto *item = seatClassModel->item(idx)) {
                item->setEnabled(false);
                item->setForeground(QBrush(QColor("#9ca3af")));
            }
        }
        if (available && firstAvailableSeatClass < 0) {
            firstAvailableSeatClass = idx;
        }
    };

    bool isHighSpeed = selectedTrain.id.startsWith('G') || selectedTrain.id.startsWith('D');
    if (isHighSpeed) {
        addSeatClassOption("二等座");
        addSeatClassOption("一等座");
        addSeatClassOption("商务座");
    } else {
        addSeatClassOption("硬座");
        addSeatClassOption("硬卧");
        addSeatClassOption("软卧");
    }

    if (firstAvailableSeatClass < 0) {
        QMessageBox::warning(this, "车票售罄", "该车次所有席别均无可售座位。");
        return;
    }
    ui->newPassengerSeatClass->setCurrentIndex(firstAvailableSeatClass);

    activeStep = 1;
    updateStepProgress();

    setupCarriageMap();
    setupCabinGrid();
    updateSummary();

    ui->stackedWidget->setCurrentIndex(1);
}

void MainWindow::setupCabinGrid()
{
    QStringList cols = seatColumnsForClass(ui->newPassengerSeatClass->currentText());
    const QString date = ui->dateInput->date().toString("yyyy-MM-dd");
    const QSet<QString> soldSeats = soldSeatsByRun.value(runKey(date, selectedTrain.id));
    updateCarriageSelection(ui->newPassengerSeatClass->currentText());
    QString carriageNo = selectedCarriageNo.isEmpty() ? "05车" : selectedCarriageNo;

    ui->seatTable->clearContents();
    ui->seatTable->setColumnCount(cols.size());
    ui->seatTable->setHorizontalHeaderLabels(cols);
    for (int row = 1; row <= 6; ++row) {
        ui->seatTable->setVerticalHeaderItem(row - 1, readonlyItem(QString("第 %1 排").arg(row)));
        for (int col = 0; col < cols.size(); ++col) {
            if (cols[col] == "过道") {
                auto *aisleItem = readonlyItem("过道");
                aisleItem->setTextAlignment(Qt::AlignCenter);
                aisleItem->setForeground(QBrush(QColor("#6e6d81")));
                aisleItem->setBackground(QBrush(QColor("#f8fafc")));
                aisleItem->setData(Qt::UserRole + 1, true);
                ui->seatTable->setItem(row - 1, col, aisleItem);
            } else {
                QString letter = cols[col];
                QString seatId = QString("%1 %2%3").arg(carriageNo).arg(row).arg(letter);

                bool occupied = soldSeats.contains(seatId);

                auto *seatItem = readonlyItem(occupied ? "X" : letter);
                seatItem->setTextAlignment(Qt::AlignCenter);
                seatItem->setData(Qt::UserRole, seatId);
                seatItem->setData(Qt::UserRole + 1, occupied);
                if (occupied) {
                    seatItem->setToolTip("已占用");
                    seatItem->setForeground(QBrush(QColor("#9ca3af")));
                    seatItem->setBackground(QBrush(QColor("#e5e7eb")));
                } else {
                    seatItem->setToolTip(seatId);
                    seatItem->setForeground(QBrush(QColor("#1f2937")));
                    seatItem->setBackground(QBrush(QColor("#f3f4f6")));
                }
                ui->seatTable->setItem(row - 1, col, seatItem);
            }
        }
    }
}

void MainWindow::setupCarriageMap()
{
    struct Carriage {
        QString no;
        QString seatClass;
        Carriage(const QString &carriageNo, const QString &carriageSeatClass)
            : no(carriageNo), seatClass(carriageSeatClass) {}
    };

    QList<Carriage> carriages;
    const bool isHighSpeed = selectedTrain.id.startsWith('G') || selectedTrain.id.startsWith('D');
    if (isHighSpeed) {
        if (trainDefinesSeatClass(selectedTrain, "商务座")) {
            carriages.append(Carriage("01车", "商务座"));
        }
        if (trainDefinesSeatClass(selectedTrain, "一等座")) {
            carriages.append(Carriage("02车", "一等座"));
            carriages.append(Carriage("03车", "一等座"));
        }
        if (trainDefinesSeatClass(selectedTrain, "二等座")) {
            carriages.append(Carriage("04车", "二等座"));
            carriages.append(Carriage("05车", "二等座"));
            carriages.append(Carriage("06车", "二等座"));
        }
    } else {
        if (trainDefinesSeatClass(selectedTrain, "软卧")) {
            carriages.append(Carriage("01车", "软卧"));
        }
        if (trainDefinesSeatClass(selectedTrain, "硬卧")) {
            carriages.append(Carriage("02车", "硬卧"));
            carriages.append(Carriage("03车", "硬卧"));
        }
        if (trainDefinesSeatClass(selectedTrain, "硬座")) {
            carriages.append(Carriage("04车", "硬座"));
            carriages.append(Carriage("05车", "硬座"));
        }
    }

    ui->carriageTable->setColumnCount(carriages.size());
    ui->carriageTable->setRowCount(1);
    const QString date = ui->dateInput->date().toString("yyyy-MM-dd");
    const QSet<QString> soldSeats = soldSeatsByRun.value(runKey(date, selectedTrain.id));
    for (int col = 0; col < carriages.size(); ++col) {
        const Carriage &carriage = carriages[col];
        auto *item = readonlyItem(QString("%1\n%2").arg(carriage.no, carriage.seatClass));
        item->setTextAlignment(Qt::AlignCenter);
        item->setData(Qt::UserRole, carriage.seatClass);
        item->setData(Qt::UserRole + 1, carriage.no);
        item->setData(Qt::UserRole + 2, availableSeatsForClass(selectedTrain, carriage.seatClass, soldSeats) <= 0);
        ui->carriageTable->setItem(0, col, item);
    }

    selectedCarriageNo.clear();
    updateCarriageSelection(ui->newPassengerSeatClass->currentText());
}

void MainWindow::updateCarriageSelection(const QString &seatClass)
{
    bool selectedCarriageMatchesClass = false;
    QString firstMatchingCarriageNo;
    for (int col = 0; col < ui->carriageTable->columnCount(); ++col) {
        QTableWidgetItem *item = ui->carriageTable->item(0, col);
        if (!item) {
            continue;
        }

        if (item->data(Qt::UserRole).toString() == seatClass) {
            if (item->data(Qt::UserRole + 2).toBool()) {
                continue;
            }
            if (firstMatchingCarriageNo.isEmpty()) {
                firstMatchingCarriageNo = item->data(Qt::UserRole + 1).toString();
            }
            if (item->data(Qt::UserRole + 1).toString() == selectedCarriageNo) {
                selectedCarriageMatchesClass = true;
            }
        }
    }

    if (!selectedCarriageMatchesClass) {
        selectedCarriageNo = firstMatchingCarriageNo;
    }

    for (int col = 0; col < ui->carriageTable->columnCount(); ++col) {
        QTableWidgetItem *item = ui->carriageTable->item(0, col);
        if (!item) {
            continue;
        }

        const bool selected = item->data(Qt::UserRole + 1).toString() == selectedCarriageNo;
        const bool unavailable = item->data(Qt::UserRole + 2).toBool();
        item->setBackground(QBrush(unavailable ? QColor("#e5e7eb") : (selected ? QColor("#9ca3af") : QColor("#f9fafb"))));
        item->setForeground(QBrush(unavailable ? QColor("#9ca3af") : (selected ? QColor("#181586") : QColor("#374151"))));
        item->setToolTip(unavailable ? "该席别无票" : QString());
    }

    ui->cabinTitle->setText(QString("%1 %2选座图").arg(selectedCarriageNo, seatClass));
}

void MainWindow::onSeatCellClicked(int row, int column)
{
    QTableWidgetItem *item = ui->seatTable->item(row, column);
    if (!item || item->data(Qt::UserRole + 1).toBool()) {
        return;
    }

    QString seatId = item->data(Qt::UserRole).toString();
    bool checked = selectedSeats.contains(seatId);

    if (!checked) {
        if (selectedSeats.size() >= passengers.size()) {
            QMessageBox::warning(this, "选座受限", "所选座位数量不能多于已添加的乘客人数！");
        } else {
            const int passengerIndex = selectedSeats.size();
            selectedSeats.insert(seatId);
            if (passengerIndex >= 0 && passengerIndex < passengers.size()) {
                const QString carriageNo = seatId.section(' ', 0, 0);
                const QString seatClass = seatClassForCarriage(carriageNo);
                if (!seatClass.isEmpty()) {
                    passengers[passengerIndex].seatClass = seatClass;
                    ui->passengerTable->setItem(passengerIndex, 2, new QTableWidgetItem(seatClass));
                }
            }
            item->setBackground(QBrush(QColor("#0ea5e9")));
            item->setForeground(QBrush(Qt::white));
        }
    } else {
        selectedSeats.remove(seatId);
        item->setBackground(QBrush(QColor("#f3f4f6")));
        item->setForeground(QBrush(QColor("#1f2937")));
    }

    updateSummary();
}

void MainWindow::onAddPassenger()
{
    QString name = ui->newPassengerName->text().trimmed();
    QString id = ui->newPassengerId->text().trimmed();
    QString seatClass = ui->newPassengerSeatClass->currentText();

    if (name.isEmpty()) {
        QMessageBox::warning(this, "添加失败", "乘客姓名不能为空！");
        return;
    }
    if (id.length() != 18) {
        QMessageBox::warning(this, "添加失败", "请输入合法的 18 位身份证号码！");
        return;
    }
    if (passengers.size() >= 5) {
        QMessageBox::warning(this, "添加失败", "单笔订单最多支持 5 名乘客！");
        return;
    }

    for (const auto &p : passengers) {
        if (p.idCard == id) {
            QMessageBox::warning(this, "添加失败", "该身份证号已存在于乘客列表中！");
            return;
        }
    }

    Passenger p;
    p.name = name;
    p.idCard = id;
    p.seatClass = seatClass;
    p.seatNo = "未选座";
    
    passengers.append(p);

    int row = ui->passengerTable->rowCount();
    ui->passengerTable->insertRow(row);
    ui->passengerTable->setItem(row, 0, new QTableWidgetItem(p.name));
    ui->passengerTable->setItem(row, 1, new QTableWidgetItem(obscureIdCard(p.idCard)));
    ui->passengerTable->setItem(row, 2, new QTableWidgetItem(p.seatClass));
    ui->passengerTable->setItem(row, 3, new QTableWidgetItem(p.seatNo));
    ui->passengerTable->selectRow(row);

    ui->newPassengerName->clear();
    ui->newPassengerId->clear();

    updateSummary();
}

void MainWindow::onRemovePassenger()
{
    const int row = ui->passengerTable->currentRow();
    if (row < 0 || row >= passengers.size()) {
        QMessageBox::information(this, "请选择乘客", "请先在乘客表中选择要删除的乘客。");
        return;
    }

    passengers.removeAt(row);
    ui->passengerTable->removeRow(row);

    if (!selectedSeats.isEmpty()) {
        selectedSeats.remove(*selectedSeats.begin());
    }

    for (int r = 0; r < ui->seatTable->rowCount(); ++r) {
        for (int c = 0; c < ui->seatTable->columnCount(); ++c) {
            QTableWidgetItem *item = ui->seatTable->item(r, c);
            if (!item || item->data(Qt::UserRole + 1).toBool()) {
                continue;
            }

            const QString seatId = item->data(Qt::UserRole).toString();
            if (selectedSeats.contains(seatId)) {
                item->setBackground(QBrush(QColor("#0ea5e9")));
                item->setForeground(QBrush(Qt::white));
            } else {
                item->setBackground(QBrush(QColor("#f3f4f6")));
                item->setForeground(QBrush(QColor("#1f2937")));
            }
        }
    }

    updateSummary();
}

void MainWindow::updateSummary()
{
    QStringList sortedSeats = selectedSeats.values();
    sortedSeats.sort();

    int totalCost = 0;
    bool isHighSpeed = selectedTrain.id.startsWith('G') || selectedTrain.id.startsWith('D');

    for (int pIdx = 0; pIdx < passengers.size(); ++pIdx) {
        QString seatNo = (pIdx < sortedSeats.size()) ? sortedSeats[pIdx] : "未选座";
        passengers[pIdx].seatNo = seatNo;
        if (seatNo != "未选座") {
            const QString carriageNo = seatNo.section(' ', 0, 0);
            const QString classFromSeat = seatClassForCarriage(carriageNo);
            if (!classFromSeat.isEmpty()) {
                passengers[pIdx].seatClass = classFromSeat;
                ui->passengerTable->setItem(pIdx, 2, new QTableWidgetItem(classFromSeat));
            }
        }
        
        ui->passengerTable->setItem(pIdx, 3, new QTableWidgetItem(seatNo));

        int fare = 0;
        QString sClass = passengers[pIdx].seatClass;
        if (sClass == "商务座" || sClass == "软卧") {
            fare = isHighSpeed ? selectedTrain.priceBusiness : selectedTrain.priceFirst;
        } else if (sClass == "一等座" || sClass == "硬卧") {
            fare = isHighSpeed ? selectedTrain.priceFirst : selectedTrain.priceSecond;
        } else {
            fare = isHighSpeed ? selectedTrain.priceSecond : selectedTrain.priceSecond - 80;
        }
        totalCost += fare;
    }

    for (int row = 0; row < ui->seatTable->rowCount(); ++row) {
        for (int col = 0; col < ui->seatTable->columnCount(); ++col) {
            QTableWidgetItem *item = ui->seatTable->item(row, col);
            if (!item || item->data(Qt::UserRole + 1).toBool()) {
                continue;
            }

            const QString seatId = item->data(Qt::UserRole).toString();
            if (selectedSeats.contains(seatId)) {
                item->setBackground(QBrush(QColor("#0ea5e9")));
                item->setForeground(QBrush(Qt::white));
            } else {
                item->setBackground(QBrush(QColor("#f3f4f6")));
                item->setForeground(QBrush(QColor("#1f2937")));
            }
        }
    }

    ui->summaryPriceLabel->setText(QString("总计: ¥%1").arg(totalCost));
    ui->summaryTrainLabel->setText(QString("当前车次: %1 | 乘客: %2 人 | 已选座: %3/%4")
        .arg(selectedTrain.id)
        .arg(passengers.size())
        .arg(selectedSeats.size())
        .arg(passengers.size()));
}

void MainWindow::onConfirmBooking()
{
    if (currentUsername.isEmpty()) {
        QMessageBox::information(this, "请先登录", "请先登录账号，出票后才能退票和改签。");
        onLogin();
        if (currentUsername.isEmpty()) {
            return;
        }
    }

    if (passengers.isEmpty()) {
        QMessageBox::warning(this, "支付失败", "请至少添加一名乘车乘客！");
        return;
    }

    if (selectedSeats.size() < passengers.size()) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "席位确认", "还有乘客未分配具体座位，系统将自动分配剩余座位。是否确认？",
                                      QMessageBox::Yes|QMessageBox::No);
        if (reply == QMessageBox::No) {
            return;
        }

        int seatAssigned = selectedSeats.size();
        while (seatAssigned < passengers.size()) {
            const QString seatId = autoSeatForRun(ui->dateInput->date().toString("yyyy-MM-dd"),
                                                  selectedTrain.id,
                                                  passengers.value(seatAssigned).seatClass);
            if (seatId.isEmpty()) {
                QMessageBox::warning(this, "余票不足", "当前班次已无可售座位。");
                return;
            }
            selectedSeats.insert(seatId);
            ++seatAssigned;
        }
        updateSummary();
    }

    const QString date = ui->dateInput->date().toString("yyyy-MM-dd");
    QSet<QString> &soldSeats = soldSeatsByRun[runKey(date, selectedTrain.id)];
    for (const QString &seatId : selectedSeats) {
        if (soldSeats.contains(seatId)) {
            QMessageBox::warning(this, "座位已售", QString("%1 已经售出，请重新选择座位。").arg(seatId));
            setupCabinGrid();
            selectedSeats.clear();
            updateSummary();
            return;
        }
    }

    activeStep = 2;
    updateStepProgress();

    for (const auto &p : passengers) {
        soldSeats.insert(p.seatNo);
        Order order;
        order.code = QString("SG-%1-%2").arg(selectedTrain.id, QString::number(orders.size() + 1).rightJustified(4, '0'));
        order.username = currentUsername;
        order.passengerName = p.name;
        order.idCard = p.idCard;
        order.trainId = selectedTrain.id;
        order.from = selectedTrain.from;
        order.to = selectedTrain.to;
        order.date = ui->dateInput->date().toString("yyyy-MM-dd");
        order.depTime = selectedTrain.depTime;
        order.arrTime = selectedTrain.arrTime;
        order.seatClass = p.seatClass;
        order.seatNo = p.seatNo;
        order.status = "已出票";
        order.fare = fareForSeatClass(selectedTrain, p.seatClass);
        order.refundFee = 0;
        order.changeFee = 0;
        orders.append(order);
    }

    renderVirtualTickets();
    ui->stackedWidget->setCurrentIndex(2);
}

void MainWindow::renderVirtualTickets()
{
    renderCurrentUserOrders();
}

void MainWindow::onResetBooking()
{
    passengers.clear();
    selectedSeats.clear();
    activeStep = 0;
    updateStepProgress();
    ui->resultsTitleLabel->setText("今日热门车次推荐");
    renderTrainList(allTrains);
    ui->stackedWidget->setCurrentIndex(0);
}

void MainWindow::onLogin()
{
    bool ok = false;
    const QString username = QInputDialog::getText(this, "账号登录", "用户名:", QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || username.isEmpty()) {
        return;
    }

    const QString password = QInputDialog::getText(this, "账号登录", "密码:", QLineEdit::Password, QString(), &ok);
    if (!ok) {
        return;
    }

    if (!accounts.contains(username) || accounts.value(username).password != password) {
        QMessageBox::warning(this, "登录失败", "用户名或密码错误。");
        return;
    }

    currentUsername = username;
    updateAccountUi();
    renderCurrentUserOrders();
    QMessageBox::information(this, "登录成功", QString("欢迎回来，%1。").arg(currentUsername));
}

void MainWindow::onRegister()
{
    bool ok = false;
    const QString username = QInputDialog::getText(this, "注册账号", "用户名:", QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || username.isEmpty()) {
        return;
    }
    if (accounts.contains(username)) {
        QMessageBox::warning(this, "注册失败", "这个用户名已经存在。");
        return;
    }

    const QString password = QInputDialog::getText(this, "注册账号", "密码:", QLineEdit::Password, QString(), &ok);
    if (!ok || password.isEmpty()) {
        return;
    }

    accounts.insert(username, UserAccount{username, password});
    currentUsername = username;
    updateAccountUi();
    QMessageBox::information(this, "注册成功", QString("账号 %1 已创建并登录。").arg(currentUsername));
}

void MainWindow::onLogout()
{
    currentUsername.clear();
    selectedOrderRow = -1;
    selectedAccountOrderRow = -1;
    updateAccountUi();
    renderCurrentUserOrders();
}

void MainWindow::onAccountCenter()
{
    if (currentUsername.isEmpty()) {
        QMessageBox::information(this, "请先登录", "请先登录或注册账号后查看账号中心。");
        onLogin();
        if (currentUsername.isEmpty()) {
            return;
        }
    }

    activeStep = 2;
    updateStepProgress();
    renderCurrentUserOrders();
    ui->stackedWidget->setCurrentWidget(ui->pageAccount);
}

void MainWindow::onRefundTicket()
{
    const int orderIndex = selectedOrderIndex();
    if (orderIndex < 0) {
        QMessageBox::information(this, "请选择订单", "请先在电子车票表中选择要退票的订单。");
        return;
    }
    if (orders[orderIndex].status.startsWith("已退票")) {
        QMessageBox::information(this, "无法退票", "这张票已经退票。");
        return;
    }

    const auto reply = QMessageBox::question(this, "确认退票", "确定要退掉选中的车票吗？",
                                             QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    Order &order = orders[orderIndex];
    order.refundFee = order.fare * (isAfterDeparture(order) ? 50 : 20) / 100;
    soldSeatsByRun[runKey(order.date, order.trainId)].remove(order.seatNo);
    order.status = QString("已退票 手续费¥%1").arg(order.refundFee);
    selectedOrderRow = -1;
    selectedAccountOrderRow = -1;
    renderCurrentUserOrders();
    renderTrainList(filteredTrains.isEmpty() ? allTrains : filteredTrains);
}

void MainWindow::onChangeTicket()
{
    const int orderIndex = selectedOrderIndex();
    if (orderIndex < 0) {
        QMessageBox::information(this, "请选择订单", "请先在电子车票表中选择要改签的订单。");
        return;
    }
    if (orders[orderIndex].status.startsWith("已退票")) {
        QMessageBox::information(this, "无法改签", "已退票订单不能改签。");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("选择改签班次");
    auto *layout = new QVBoxLayout(&dialog);
    auto *table = new QTableWidget(&dialog);
    table->setColumnCount(7);
    table->setHorizontalHeaderLabels({"日期", "车次", "出发", "到达", "时间", "余座", "票价"});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(table);

    const Order &oldOrder = orders[orderIndex];
    int optionRow = 0;
    int firstAvailableRow = -1;
    for (int day = 0; day < 3; ++day) {
        const QString date = QDate::currentDate().addDays(day).toString("yyyy-MM-dd");
        for (const auto &train : allTrains) {
            if (train.id == oldOrder.trainId || train.to != oldOrder.to) {
                continue;
            }

            const bool supportsClass = trainSupportsSeatClass(train, oldOrder.seatClass);
            const QString trialSeat = autoSeatForRun(date, train.id, oldOrder.seatClass);
            const bool canChange = supportsClass && !trialSeat.isEmpty();
            const QSet<QString> soldSeats = soldSeatsByRun.value(runKey(date, train.id));
            const int availableCount = supportsClass ? availableSeatsForClass(train, oldOrder.seatClass, soldSeats) : 0;
            table->insertRow(optionRow);
            auto *dateItem = readonlyItem(date);
            dateItem->setData(Qt::UserRole, train.id);
            dateItem->setData(Qt::UserRole + 2, !canChange);
            table->setItem(optionRow, 0, dateItem);
            table->setItem(optionRow, 1, readonlyItem(train.id));
            table->setItem(optionRow, 2, readonlyItem(train.from));
            table->setItem(optionRow, 3, readonlyItem(train.to));
            table->setItem(optionRow, 4, readonlyItem(QString("%1 - %2").arg(train.depTime, train.arrTime)));
            table->setItem(optionRow, 5, readonlyItem(canChange ? QString::number(availableCount) : "无票"));
            table->setItem(optionRow, 6, readonlyItem(canChange ? QString("¥%1").arg(fareForSeatClass(train, oldOrder.seatClass)) : "不可改签"));
            if (!canChange) {
                const QString reason = supportsClass ? "该席别无余票" : "该班次不支持原订单席别";
                for (int c = 0; c < table->columnCount(); ++c) {
                    QTableWidgetItem *item = table->item(optionRow, c);
                    if (item) {
                        item->setBackground(QBrush(QColor("#e5e7eb")));
                        item->setForeground(QBrush(QColor("#9ca3af")));
                        item->setToolTip(reason);
                    }
                }
            } else if (firstAvailableRow < 0) {
                firstAvailableRow = optionRow;
            }
                ++optionRow;
        }
    }

    if (optionRow == 0) {
        QMessageBox::information(this, "暂无可改签班次", "3 天内没有同一目的地的其他班次。");
        return;
    }

    if (firstAvailableRow >= 0) {
        table->selectRow(firstAvailableRow);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || table->currentRow() < 0) {
        return;
    }

    const int chosenRow = table->currentRow();
    if (table->item(chosenRow, 0)->data(Qt::UserRole + 2).toBool()) {
        QMessageBox::warning(this, "改签失败", "该班次当前不可改签，请选择未置灰的班次。");
        return;
    }
    const QString newDate = table->item(chosenRow, 0)->text();
    const QString newTrainId = table->item(chosenRow, 1)->text();
    for (const auto &train : allTrains) {
        if (train.id == newTrainId) {
            if (!trainSupportsSeatClass(train, orders[orderIndex].seatClass)) {
                QMessageBox::warning(this, "改签失败", "该班次不支持原订单席别，不能改签。");
                return;
            }
            QString newSeat = autoSeatForRun(newDate, train.id, orders[orderIndex].seatClass);
            if (newSeat.isEmpty()) {
                QMessageBox::warning(this, "改签失败", "该班次刚刚已无可售座位。");
                return;
            }
            Order &order = orders[orderIndex];
            soldSeatsByRun[runKey(order.date, order.trainId)].remove(order.seatNo);
            soldSeatsByRun[runKey(newDate, train.id)].insert(newSeat);
            order.changeFee = order.fare * (isAfterDeparture(order) ? 20 : 0) / 100;
            orders[orderIndex].trainId = train.id;
            orders[orderIndex].from = train.from;
            orders[orderIndex].to = train.to;
            orders[orderIndex].date = newDate;
            orders[orderIndex].depTime = train.depTime;
            orders[orderIndex].arrTime = train.arrTime;
            orders[orderIndex].seatNo = newSeat;
            orders[orderIndex].fare = fareForSeatClass(train, order.seatClass);
            orders[orderIndex].status = order.changeFee > 0
                ? QString("已改签 手续费¥%1").arg(order.changeFee)
                : "已改签";
            break;
        }
    }

    selectedOrderRow = -1;
    selectedAccountOrderRow = -1;
    renderCurrentUserOrders();
    renderTrainList(filteredTrains.isEmpty() ? allTrains : filteredTrains);
}

void MainWindow::updateAccountUi()
{
    const bool loggedIn = !currentUsername.isEmpty();
    ui->accountStatusLabel->setText(loggedIn ? QString("当前账号: %1").arg(currentUsername) : "未登录");
    ui->loginBtn->setEnabled(!loggedIn);
    ui->registerBtn->setEnabled(!loggedIn);
    ui->logoutBtn->setEnabled(loggedIn);
    ui->accountCenterBtn->setEnabled(loggedIn);
    ui->refundBtn->setEnabled(loggedIn);
    ui->changeTicketBtn->setEnabled(loggedIn);
    ui->accountRefundBtn->setEnabled(loggedIn);
    ui->accountChangeTicketBtn->setEnabled(loggedIn);

    if (!loggedIn) {
        ui->accountSummaryLabel->setText("请先登录账号");
    }
}

void MainWindow::renderCurrentUserOrders()
{
    renderOrdersIntoTable(ui->ticketTable, &selectedOrderRow);
    renderOrdersIntoTable(ui->accountOrderTable, &selectedAccountOrderRow);

    if (currentUsername.isEmpty()) {
        ui->accountSummaryLabel->setText("请先登录账号");
        return;
    }

    int total = 0;
    int active = 0;
    int refunded = 0;
    for (const auto &order : orders) {
        if (order.username == currentUsername) {
            ++total;
            if (order.status.startsWith("已退票")) {
                ++refunded;
            } else {
                ++active;
            }
        }
    }

    ui->accountSummaryLabel->setText(QString("账号: %1    全部订单: %2    有效车票: %3    已退票: %4")
        .arg(currentUsername)
        .arg(total)
        .arg(active)
        .arg(refunded));
}

void MainWindow::renderOrdersIntoTable(QTableWidget *table, int *selectedRow)
{
    table->setRowCount(0);
    if (currentUsername.isEmpty()) {
        if (selectedRow) {
            *selectedRow = -1;
        }
        return;
    }

    int row = 0;
    for (int i = 0; i < orders.size(); ++i) {
        const Order &order = orders[i];
        if (order.username != currentUsername) {
            continue;
        }

        table->insertRow(row);
        auto *nameItem = readonlyItem(order.passengerName);
        nameItem->setData(Qt::UserRole, i);
        table->setItem(row, 0, nameItem);
        table->setItem(row, 1, readonlyItem(order.trainId));
        table->setItem(row, 2, readonlyItem(QString("%1 → %2").arg(order.from, order.to)));
        table->setItem(row, 3, readonlyItem(order.date));
        table->setItem(row, 4, readonlyItem(QString("%1 - %2").arg(order.depTime, order.arrTime)));
        table->setItem(row, 5, readonlyItem(order.seatClass));
        table->setItem(row, 6, readonlyItem(order.seatNo));
        table->setItem(row, 7, readonlyItem(order.code));
        table->setItem(row, 8, readonlyItem(order.status));
        table->setRowHeight(row, 44);
        ++row;
    }

    if (selectedRow && (*selectedRow < 0 || *selectedRow >= row)) {
        *selectedRow = -1;
    }
}

int MainWindow::selectedOrderIndex() const
{
    const bool inAccountPage = ui->stackedWidget->currentWidget() == ui->pageAccount;
    QTableWidget *table = inAccountPage ? ui->accountOrderTable : ui->ticketTable;
    const int row = inAccountPage ? selectedAccountOrderRow : selectedOrderRow;
    if (row < 0 || row >= table->rowCount()) {
        return -1;
    }

    QTableWidgetItem *item = table->item(row, 0);
    if (!item) {
        return -1;
    }
    return item->data(Qt::UserRole).toInt();
}

QString MainWindow::runKey(const QString &date, const QString &trainId) const
{
    return date + "|" + trainId;
}

int MainWindow::fareForSeatClass(const Train &train, const QString &seatClass) const
{
    const bool isHighSpeed = train.id.startsWith('G') || train.id.startsWith('D');
    if (seatClass == "商务座" || seatClass == "软卧") {
        return isHighSpeed ? train.priceBusiness : train.priceFirst;
    }
    if (seatClass == "一等座" || seatClass == "硬卧") {
        return isHighSpeed ? train.priceFirst : train.priceSecond;
    }
    return isHighSpeed ? train.priceSecond : train.priceSecond - 80;
}

bool MainWindow::isAfterDeparture(const Order &order) const
{
    const QDate date = QDate::fromString(order.date, "yyyy-MM-dd");
    const QTime time = QTime::fromString(order.depTime, "HH:mm");
    return QDateTime::currentDateTime() >= QDateTime(date, time);
}

bool MainWindow::findTrainById(const QString &trainId, Train *train) const
{
    for (const auto &candidate : allTrains) {
        if (candidate.id == trainId) {
            if (train) {
                *train = candidate;
            }
            return true;
        }
    }
    return false;
}

QString MainWindow::autoSeatForRun(const QString &date, const QString &trainId, const QString &seatClass) const
{
    Train train;
    if (!findTrainById(trainId, &train) || !trainSupportsSeatClass(train, seatClass)) {
        return QString();
    }

    const QSet<QString> soldSeats = soldSeatsByRun.value(runKey(date, trainId));
    QStringList cols = seatColumnsForClass(seatClass);
    cols.removeAll("过道");
    const QStringList carriages = carriagesForSeatClass(train, seatClass);

    for (const QString &carriageNo : carriages) {
        for (int row = 1; row <= 6; ++row) {
            for (const QString &col : cols) {
                const QString seatId = QString("%1 %2%3").arg(carriageNo).arg(row).arg(col);
                if (!soldSeats.contains(seatId) && !selectedSeats.contains(seatId)) {
                    return seatId;
                }
            }
        }
    }
    return QString();
}

QStringList MainWindow::seatColumnsForClass(const QString &seatClass) const
{
    if (seatClass == "商务座") {
        return {"A", "过道", "F"};
    }
    if (seatClass == "一等座") {
        return {"A", "C", "过道", "D", "F"};
    }
    if (seatClass == "软卧") {
        return {"上铺1", "过道", "下铺1"};
    }
    if (seatClass == "硬卧") {
        return {"上铺1", "中铺1", "下铺1", "过道", "上铺2", "下铺2"};
    }
    return {"A", "B", "C", "过道", "D", "F"};
}

QString MainWindow::seatClassForCarriage(const QString &carriageNo) const
{
    for (int col = 0; col < ui->carriageTable->columnCount(); ++col) {
        QTableWidgetItem *item = ui->carriageTable->item(0, col);
        if (item && item->data(Qt::UserRole + 1).toString() == carriageNo) {
            return item->data(Qt::UserRole).toString();
        }
    }
    return QString();
}

void MainWindow::refreshSeatColors()
{
    for (int row = 0; row < ui->seatTable->rowCount(); ++row) {
        for (int col = 0; col < ui->seatTable->columnCount(); ++col) {
            QTableWidgetItem *item = ui->seatTable->item(row, col);
            if (!item || item->data(Qt::UserRole + 1).toBool()) {
                continue;
            }

            const QString seatId = item->data(Qt::UserRole).toString();
            if (selectedSeats.contains(seatId)) {
                item->setBackground(QBrush(QColor("#0ea5e9")));
                item->setForeground(QBrush(Qt::white));
            } else {
                item->setBackground(QBrush(QColor("#f3f4f6")));
                item->setForeground(QBrush(QColor("#1f2937")));
            }
        }
    }
}

void MainWindow::applyOrderRowHighlight(QTableWidget *table, int selectedRow)
{
    setRowColors(table, selectedRow);
}

void MainWindow::onDirectTicket()
{
    if (currentUsername.isEmpty()) {
        QMessageBox::information(this, "请先登录", "请先登录账号后再直接出票。");
        onLogin();
        if (currentUsername.isEmpty()) {
            return;
        }
    }

    QDialog dialog(this);
    dialog.setWindowTitle("直接出票");
    auto *layout = new QFormLayout(&dialog);
    auto *nameInput = new QLineEdit(&dialog);
    auto *idInput = new QLineEdit(&dialog);
    auto *dateInput = new QDateEdit(&dialog);
    auto *destinationInput = new QLineEdit(ui->toInput->text(), &dialog);
    auto *trainInput = new QLineEdit(&dialog);
    dateInput->setDisplayFormat("yyyy-MM-dd");
    dateInput->setCalendarPopup(true);
    dateInput->setButtonSymbols(QAbstractSpinBox::NoButtons);
    dateInput->setDate(ui->dateInput->date());
    dateInput->setMinimumDate(QDate::currentDate());
    dateInput->setMaximumDate(QDate::currentDate().addYears(1));
    idInput->setValidator(new QRegularExpressionValidator(QRegularExpression("^[0-9xX]{0,18}$"), idInput));
    layout->addRow("乘客姓名", nameInput);
    layout->addRow("身份证号", idInput);
    layout->addRow("发车日期", dateInput);
    layout->addRow("目的地", destinationInput);
    layout->addRow("班次号(可空)", trainInput);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString passengerName = nameInput->text().trimmed();
    const QString idCard = idInput->text().trimmed();
    const QString date = dateInput->date().toString("yyyy-MM-dd");
    const QString destination = destinationInput->text().trimmed();
    const QString requestedTrainId = trainInput->text().trimmed();
    if (passengerName.isEmpty() || idCard.length() != 18 || destination.isEmpty()) {
        QMessageBox::warning(this, "出票失败", "请完整填写乘客姓名、18 位身份证号和目的地。");
        return;
    }

    Train train;
    QString seatClass;
    QString seatNo;
    bool found = false;
    for (const auto &candidate : allTrains) {
        if (!requestedTrainId.isEmpty() && candidate.id != requestedTrainId) {
            continue;
        }
        if (candidate.to != destination) {
            continue;
        }

        const QString candidateSeatClass = (candidate.id.startsWith('G') || candidate.id.startsWith('D')) ? "二等座" : "硬座";
        const QString candidateSeatNo = autoSeatForRun(date, candidate.id, candidateSeatClass);
        if (candidateSeatNo.isEmpty()) {
            continue;
        }

        train = candidate;
        seatClass = candidateSeatClass;
        seatNo = candidateSeatNo;
        found = true;
        break;
    }

    if (!found) {
        QMessageBox::warning(this, "无票可售", "没有找到符合条件且有余座的班次。");
        return;
    }

    soldSeatsByRun[runKey(date, train.id)].insert(seatNo);
    Order order;
    order.code = QString("SG-%1-%2").arg(train.id, QString::number(orders.size() + 1).rightJustified(4, '0'));
    order.username = currentUsername;
    order.passengerName = passengerName;
    order.idCard = idCard;
    order.trainId = train.id;
    order.from = train.from;
    order.to = train.to;
    order.date = date;
    order.depTime = train.depTime;
    order.arrTime = train.arrTime;
    order.seatClass = seatClass;
    order.seatNo = seatNo;
    order.status = "已出票";
    order.fare = fareForSeatClass(train, order.seatClass);
    order.refundFee = 0;
    order.changeFee = 0;
    orders.append(order);

    renderCurrentUserOrders();
    renderTrainList(filteredTrains.isEmpty() ? allTrains : filteredTrains);
    QMessageBox::information(this, "出票成功", QString("已出票：%1 %2 %3").arg(train.id, passengerName, seatNo));
}

void MainWindow::onExportManifest()
{
    const QString date = ui->dateInput->date().toString("yyyy-MM-dd");
    bool ok = false;
    const QString trainId = QInputDialog::getText(this, "输出旅客登记表", "班次号:", QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || trainId.isEmpty()) {
        return;
    }

    Train train;
    if (!findTrainById(trainId, &train)) {
        QMessageBox::warning(this, "班次不存在", "没有找到指定班次。");
        return;
    }

    QString report = QString("旅客登记表\n日期: %1\n班次: %2  %3 → %4  %5\n\n")
        .arg(date, train.id, train.from, train.to, train.depTime);
    report += "姓名\t身份证号\t座位\t席别\t票价\t状态\n";

    int totalFare = 0;
    int count = 0;
    for (const auto &order : orders) {
        if (order.date == date && order.trainId == trainId && !order.status.startsWith("已退票")) {
            report += QString("%1\t%2\t%3\t%4\t¥%5\t%6\n")
                .arg(order.passengerName,
                     obscureIdCard(order.idCard),
                     order.seatNo,
                     order.seatClass)
                .arg(order.fare)
                .arg(order.status);
            totalFare += order.fare;
            ++count;
        }
    }

    report += QString("\n旅客人数: %1\n票款合计: ¥%2").arg(count).arg(totalFare);
    QMessageBox::information(this, "旅客登记表", report);
}

QString MainWindow::getStylesheet() {
    return R"(
        QMainWindow {
            background-color: #f3f4f6;
        }
        QWidget#centralwidget {
            background-color: #f3f4f6;
        }
        
        /* Step Navigation Bar */
        QFrame#headerFrame {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ffffff, stop:1 #fafafa);
            border-bottom: 1px solid #e5e7eb;
            min-height: 70px;
            max-height: 70px;
        }
        QLabel#titleLabel {
            color: #4f46e5;
            font-size: 20px;
            font-weight: bold;
            font-family: 'Microsoft YaHei', sans-serif;
        }
        QLabel#stepProgressLabel {
            color: #1f2937;
            font-size: 14px;
            font-weight: 500;
        }
        QLabel#timeLabel {
            color: #6b7280;
            font-size: 13px;
        }

        /* Sidebar Panels */
        QFrame#queryPanel {
            background-color: #ffffff;
            border-radius: 12px;
            border: 1px solid #e5e7eb;
        }
        QLabel#panelTitle {
            color: #1f2937;
            font-size: 16px;
            font-weight: bold;
        }
        QLabel#lblFrom, QLabel#lblTo, QLabel#lblDate, QLabel#lblType {
            color: #4b5563;
            font-size: 12px;
            font-weight: bold;
        }

        /* Input Controls */
        QLineEdit {
            background-color: #f9fafb;
            border: 1px solid #d1d5db;
            border-radius: 6px;
            padding: 8px 12px;
            color: #1f2937;
            font-size: 14px;
        }
        QLineEdit:focus {
            border: 1px solid #4f46e5;
            background-color: #ffffff;
        }
        QDateEdit {
            background-color: #f9fafb;
            border: 1px solid #d1d5db;
            border-radius: 6px;
            padding: 8px 30px 8px 12px;
            color: #1f2937;
            font-size: 14px;
        }
        QDateEdit:focus {
            border: 1px solid #4f46e5;
            background-color: #ffffff;
        }
        QDateEdit::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 26px;
            border: none;
            background: transparent;
        }
        QDateEdit::down-arrow {
            width: 10px;
            height: 10px;
        }
        QCalendarWidget {
            background-color: #ffffff;
            border: 1px solid #e5e7eb;
            border-radius: 8px;
            color: #1f2937;
            font-size: 13px;
        }
        QCalendarWidget QWidget#qt_calendar_navigationbar {
            background-color: #ffffff;
            border-bottom: 1px solid #eef2f7;
            min-height: 34px;
            padding: 2px 8px;
        }
        QCalendarWidget QToolButton {
            background-color: transparent;
            border: none;
            border-radius: 5px;
            color: #374151;
            font-size: 14px;
            font-weight: bold;
            padding: 4px 6px;
            margin: 1px;
            min-width: 28px;
            min-height: 26px;
            qproperty-iconSize: 14px 14px;
        }
        QCalendarWidget QToolButton:hover {
            background-color: #f3f4f6;
        }
        QCalendarWidget QToolButton#qt_calendar_monthbutton {
            min-width: 58px;
            color: #1f2937;
        }
        QCalendarWidget QToolButton#qt_calendar_yearbutton {
            min-width: 54px;
            color: #1f2937;
        }
        QCalendarWidget QToolButton::menu-indicator {
            image: none;
            width: 0px;
        }
        QCalendarWidget QMenu {
            background-color: #ffffff;
            border: 1px solid #e5e7eb;
            color: #1f2937;
        }
        QCalendarWidget QSpinBox {
            background-color: transparent;
            border: none;
            border-radius: 5px;
            padding: 3px 4px;
            color: #1f2937;
            font-size: 14px;
            font-weight: bold;
            selection-background-color: #ede9fe;
            selection-color: #4f46e5;
        }
        QCalendarWidget QSpinBox:hover {
            background-color: #f3f4f6;
        }
        QCalendarWidget QSpinBox::up-button,
        QCalendarWidget QSpinBox::down-button {
            width: 0px;
            border: none;
        }
        QCalendarWidget QSpinBox::up-arrow,
        QCalendarWidget QSpinBox::down-arrow {
            image: none;
            width: 0px;
            height: 0px;
        }
        QCalendarWidget QAbstractItemView {
            background-color: #ffffff;
            alternate-background-color: #fafafa;
            border: none;
            outline: none;
            selection-background-color: #4f46e5;
            selection-color: #ffffff;
            color: #374151;
        }
        QComboBox {
            background-color: #f9fafb;
            border: 1px solid #d1d5db;
            border-radius: 6px;
            padding: 8px 12px;
            color: #1f2937;
            font-size: 14px;
        }
        QComboBox:focus {
            border: 1px solid #4f46e5;
            background-color: #ffffff;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 20px;
            border-left-width: 0px;
        }
        QComboBox QAbstractItemView {
            background-color: #ffffff;
            border: 1px solid #e5e7eb;
            color: #1f2937;
            selection-background-color: #4f46e5;
            selection-color: #ffffff;
        }
        QComboBox QAbstractItemView::item:disabled {
            background-color: #e5e7eb;
            color: #9ca3af;
        }

        /* Buttons */
        QPushButton {
            background-color: #4f46e5;
            color: #ffffff;
            border: none;
            border-radius: 8px;
            padding: 10px 20px;
            font-size: 14px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #6366f1;
        }
        QPushButton:pressed {
            background-color: #4338ca;
        }
        QPushButton:disabled {
            background-color: #e5e7eb;
            color: #9ca3af;
        }
        QPushButton#swapBtn {
            background-color: transparent;
            border: 1px solid #e5e7eb;
            color: #4f46e5;
            border-radius: 16px;
            width: 32px;
            height: 32px;
            padding: 0;
        }
        QPushButton#swapBtn:hover {
            background-color: rgba(79, 70, 229, 0.08);
            border-color: #4f46e5;
        }
        QPushButton#secondaryBtn {
            background-color: transparent;
            border: 1px solid #d1d5db;
            color: #4b5563;
        }
        QPushButton#secondaryBtn:hover {
            background-color: rgba(0, 0, 0, 0.04);
            color: #1f2937;
        }
        QPushButton#addPassengerBtn {
            background-color: #0ea5e9;
            color: #ffffff;
            border-radius: 6px;
            padding: 8px 16px;
        }
        QPushButton#addPassengerBtn:hover {
            background-color: #38bdf8;
        }
        QPushButton#loginBtn,
        QPushButton#registerBtn,
        QPushButton#logoutBtn,
        QPushButton#accountCenterBtn {
            padding: 6px 12px;
            border-radius: 8px;
            font-size: 13px;
            min-height: 30px;
            max-height: 36px;
        }
        QPushButton#accountCenterBtn {
            padding-left: 14px;
            padding-right: 14px;
        }

        /* Table View */
        QTableWidget {
            background-color: #ffffff;
            border: 1px solid #e5e7eb;
            border-radius: 8px;
            gridline-color: #f3f4f6;
            color: #1f2937;
            font-size: 13px;
        }
        QTableWidget::item {
            padding: 5px;
        }
        QTableWidget::item:selected {
            background-color: rgba(127, 127, 127, 0.1);
            color: #7748e2;
        }
        QHeaderView::section {
            background-color: #f9fafb;
            color: #4b5563;
            padding: 6px;
            border: none;
            border-bottom: 1px solid #e5e7eb;
            font-weight: bold;
        }
        QLabel#trainMapTitle {
            color: #1f2937;
            font-size: 13px;
            font-weight: bold;
            margin-top: 6px;
        }
        QTableWidget#carriageTable {
            background-color: #f8fafc;
            border: 1px solid #e5e7eb;
            border-radius: 10px;
            gridline-color: #e5e7eb;
            font-size: 12px;
            font-weight: bold;
        }
        QTableWidget#carriageTable::item {
            padding: 8px;
        }

        /* Cabin Selection styling */
        QFrame#cabinFrame {
            background-color: #ffffff;
            border: 1px solid #e5e7eb;
            border-radius: 16px;
        }
        QLabel#cabinTitle {
            color: #4f46e5;
            font-size: 14px;
            font-weight: bold;
        }
        QLabel#seatLegendLabel,
        QLabel#availableLegendLabel,
        QLabel#selectedLegendLabel,
        QLabel#occupiedLegendLabel {
            color: #6b7280;
            font-size: 12px;
        }

        /* Booking Summary Bar */
        QFrame#summaryBar {
            background-color: #ffffff;
            border-top: 1px solid #e5e7eb;
            min-height: 60px;
        }
    )";
}
