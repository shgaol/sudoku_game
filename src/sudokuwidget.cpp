#include "sudokuwidget.h"

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QCheckBox>
#include <QRegularExpressionValidator>
#include <QMessageBox>
#include <QFrame>
#include <QEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QClipboard>

SudokuWidget::SudokuWidget(QWidget *parent)
    : QWidget(parent)
    , m_difficulty(nullptr)
    , m_statusLabel(nullptr)
{
    for (int r = 0; r < 9; ++r) {
        for (int c = 0; c < 9; ++c) {
            m_cells[r][c] = nullptr;
            m_error[r][c] = false;
            m_autoFilled[r][c] = false;
            m_autoFilledRound[r][c] = false;
        }
    }

    // 监听全局鼠标/键盘事件：用于关闭数字选择面板
    qApp->installEventFilter(this);

    setupUi();
    onNewGame();
}

void SudokuWidget::setupUi()
{
    setWindowTitle(QStringLiteral("数独游戏"));
    setMinimumSize(640, 640);

    auto *mainLayout = new QVBoxLayout(this);

    // ---- 中间：左侧棋盘 + 右侧控制栏 ----
    auto *middleLayout = new QHBoxLayout;

    // 棋盘宿主：占满左侧空间，棋盘（正方形）在其中居中
    m_boardHost = new QWidget(this);
    auto *hostLayout = new QGridLayout(m_boardHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);

    // 9x9 网格：格子间无间距，紧密相连
    m_gridFrame = new QFrame(m_boardHost);
    m_gridFrame->setFrameShape(QFrame::StyledPanel);
    auto *gridLayout = new QGridLayout(m_gridFrame);
    gridLayout->setSpacing(0);
    gridLayout->setContentsMargins(0, 0, 0, 0);

    // 每个格子只允许输入 1~9
    QRegularExpressionValidator *validator =
        new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[1-9]")), this);

    for (int r = 0; r < 9; ++r) {
        for (int c = 0; c < 9; ++c) {
            auto *cell = new QLineEdit(m_gridFrame);
            cell->setValidator(validator);
            cell->setAlignment(Qt::AlignCenter);
            cell->setMaxLength(1);
            // 禁用默认右键菜单，避免干扰"右键弹唯一提示"
            cell->setContextMenuPolicy(Qt::NoContextMenu);
            // 最小尺寸保护；实际大小由 resizeEvent 设为正方形
            cell->setMinimumSize(30, 30);

            QFont font = cell->font();
            font.setPointSize(14);
            font.setBold(true);
            cell->setFont(font);

            m_cells[r][c] = cell;
            gridLayout->addWidget(cell, r, c);

            // 注意：不再给格子单独安装事件过滤器——
            // 构造函数已通过 qApp->installEventFilter(this) 全局过滤，
            // 事件会先经过全局过滤器再到控件过滤器，重复安装会导致
            // 同一事件被处理两次（如无效右键计数翻倍）

            // 玩家输入数字后：清除该格标红，并自动检查（重新计算）。
            // 规则：
            //   初始数据（isGiven）只读，玩家数据可自由修改/删除；
            //   自动填写的数字不是永久保留——
            //     修改任意数字：全部取消，再按唯一性规则重新自动填写；
            //     删除任意数字：只取消最近一次自动填写的数字，不重新自动填写；
            //   在空格填写数字：按唯一性规则自动填写
            connect(cell, &QLineEdit::textChanged, this,
                    [this, r, c](const QString &text) {
                        // 程序加载/自动填写/批量清空时跳过
                        if (m_loadingGrid)
                            return;

                        // 模型尚未同步：m_game.puzzle()[r][c] 仍是"改动前"的值，
                        // 据此区分 填写(0→数字) / 修改(数字→数字) / 删除(数字→0)
                        const int oldVal = m_game.puzzle()[r][c];
                        const int newVal = text.isEmpty() ? 0 : text.toInt();

                        if (oldVal != newVal) {
                            if (newVal == 0) {
                                // 玩家删除数字：只取消"最近一次自动填写"的数字
                                // （正在删除的格子本身除外），不自动填写
                                cancelAutoFilledRound(r, c);
                            } else if (oldVal != 0) {
                                // 玩家修改数字：先取消所有自动填写的数字
                                // （正在修改的格子除外），
                                // 再按唯一性规则自动填写
                                cancelAutoFilled(r, c);
                                autoFillLastCells(r, c);
                            } else {
                                // 玩家在空格填写数字：按唯一性规则自动填写
                                autoFillLastCells(r, c);
                            }
                        }

                        if (m_error[r][c]) {
                            m_error[r][c] = false;
                            refreshCell(r, c);
                        }
                        checkForErrors();
                    });
        }
    }

    // 棋盘居中于宿主
    hostLayout->addWidget(m_gridFrame, 0, 0, Qt::AlignCenter);

    // 右侧控制栏：新游戏 / 清空 / 难度 / 直接提示
    auto *sideLayout = new QVBoxLayout;
    sideLayout->setSpacing(10);
    sideLayout->setContentsMargins(0, 0, 0, 0);

    auto *newButton  = new QPushButton(QStringLiteral("新游戏"), this);
    auto *clearButton = new QPushButton(QStringLiteral("清空"), this);
    auto *copyButton = new QPushButton(QStringLiteral("生成盘面"), this);
    copyButton->setToolTip(QStringLiteral("把当前棋盘以 9 行文字（0=空格）复制到剪贴板"));

    m_difficulty = new QComboBox(this);
    m_difficulty->addItem(QStringLiteral("简单"), static_cast<int>(SudokuGame::Difficulty::Easy));
    m_difficulty->addItem(QStringLiteral("中等"), static_cast<int>(SudokuGame::Difficulty::Medium));
    m_difficulty->addItem(QStringLiteral("困难"), static_cast<int>(SudokuGame::Difficulty::Hard));

    // "直接提示"开关：仅简单难度显示，默认不打勾
    m_directHint = new QCheckBox(QStringLiteral("直接提示"), this);
    m_directHint->setChecked(false);
    m_directHint->setToolTip(QStringLiteral("勾选后，点击可编辑格子会弹出候选数字面板"));

    sideLayout->addWidget(newButton);
    sideLayout->addWidget(clearButton);
    sideLayout->addWidget(copyButton);
    sideLayout->addSpacing(8);
    sideLayout->addWidget(new QLabel(QStringLiteral("难度:"), this));
    sideLayout->addWidget(m_difficulty);
    sideLayout->addWidget(m_directHint);

    // 操作与规则说明（显示在侧边栏空余区域）
    m_helpLabel = new QLabel(this);
    m_helpLabel->setWordWrap(true);
    m_helpLabel->setTextFormat(Qt::PlainText);
    m_helpLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #555555; font-size: 12px; }"));
    m_helpLabel->setText(QStringLiteral(
        "操作说明：\n"
        "· 左键点空格：键盘输入数字\n"
        "· 初始数字：只读，不能修改或删除\n"
        "· 玩家填写的数字：可修改，也可删除（Backspace/Delete）\n"
        "· 右键点空格：唯一解提示（行列唯一/宫格内唯一）\n"
        "· 勾选\"直接提示\"（简单难度）：点格子弹候选面板\n"
        "· 提示按钮：自动填第一个空格\n"
        "· 生成盘面：复制文字棋盘到剪贴板\n"
        "· 自动检查：填错数字标红（空格不算错）\n"
        "· 自动填写：填数字后，其所在行/列/3x3宫只剩1个空格时自动填答案\n"
        "· 自动填写的数字：修改任意数字时全部取消；删除任意数字时只取消最近一次的\n"
        "\n"
        "难度规则（每宫格空格数）：\n"
        "· 简单：3 个为主，偶尔 1 宫格 4 个\n"
        "· 中等：3 个为主，至少 2 宫格 4 个\n"
        "· 困难：3 个为主，至少 2 宫格 4 个 + 至少 1、至多 3 宫格 5 个\n"
        "\n"
        "完成提示：\n"
        "· 宫格填满变绿色；行/列填满且全对也变绿色\n"
        "· 全部填对弹窗显示用时与无效右键次数\n"
        "· 无效右键：右键不唯一，或弹出面板未选数字"));
    sideLayout->addWidget(m_helpLabel);

    // 当前难度显示
    m_difficultyLabel = new QLabel(
        QStringLiteral("当前难度：%1").arg(m_difficulty->currentText()), this);
    m_difficultyLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #005ac8; font-weight: bold; }"));
    sideLayout->addWidget(m_difficultyLabel);

    // 无效右键次数（实时显示，位于难度显示下面）
    m_invalidLabel = new QLabel(QStringLiteral("无效右键：0"), this);
    m_invalidLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #a05000; font-weight: bold; }"));
    sideLayout->addWidget(m_invalidLabel);

    sideLayout->addStretch();   // 其余空间留白，控件靠上

    auto *sideWidget = new QWidget(this);
    sideWidget->setLayout(sideLayout);
    sideWidget->setFixedWidth(180);   // 侧边栏固定宽度（足够显示完整文字）

    connect(newButton,   &QPushButton::clicked, this, &SudokuWidget::onNewGame);
    connect(clearButton, &QPushButton::clicked, this, &SudokuWidget::onClear);
    connect(copyButton,  &QPushButton::clicked, this, &SudokuWidget::onCopyBoard);

    // 切换难度：更新"直接提示"可见性、显示当前难度，并自动按新难度开新游戏
    connect(m_difficulty, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                m_directHint->setVisible(isEasyMode());
                // 更新"当前难度"显示
                if (m_difficultyLabel) {
                    m_difficultyLabel->setText(
                        QStringLiteral("当前难度：%1")
                            .arg(m_difficulty->currentText()));
                }
                // 按新难度自动开始新游戏（内部会重置计时/无效次数/隐藏面板）
                onNewGame();
            });

    // 取消勾选"直接提示"时，立即关闭候选面板
    connect(m_directHint, &QCheckBox::toggled, this, [this](bool checked) {
        if (!checked && m_popup && m_popup->isVisible())
            hideNumberPopup();
    });

    middleLayout->addWidget(m_boardHost, 1);
    middleLayout->addWidget(sideWidget);

    mainLayout->addLayout(middleLayout, 1);

    // ---- 底部：状态栏（左）+ 提示按钮（右下角）----
    auto *bottomLayout = new QHBoxLayout;
    m_statusLabel = new QLabel(QStringLiteral("点击\"新游戏\"开始"), this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    auto *hintButton = new QPushButton(QStringLiteral("提示"), this);
    bottomLayout->addWidget(m_statusLabel, 1);
    bottomLayout->addWidget(hintButton);
    connect(hintButton, &QPushButton::clicked, this, &SudokuWidget::onHint);

    mainLayout->addLayout(bottomLayout);
}

void SudokuWidget::applyPuzzleToGrid()
{
    // 新游戏时清除高亮、错误标记和自动填写标记
    m_highlightRow = -1;
    m_highlightCol = -1;
    for (int r = 0; r < 9; ++r) {
        for (int c = 0; c < 9; ++c) {
            m_error[r][c] = false;
            m_autoFilled[r][c] = false;
            m_autoFilledRound[r][c] = false;
        }
    }

    // 程序加载棋盘：setText 不触发自动检查
    m_loadingGrid = true;

    const auto &puzzle = m_game.puzzle();
    for (int r = 0; r < 9; ++r) {
        for (int c = 0; c < 9; ++c) {
            int v = puzzle[r][c];
            QLineEdit *cell = m_cells[r][c];
            cell->setText(v == 0 ? QString() : QString::number(v));

            // 初始数字不可编辑
            cell->setReadOnly(m_game.isGiven(r, c));
            refreshCell(r, c);
        }
    }

    m_loadingGrid = false;
}

QString SudokuWidget::buildCellStyle(int row, int col, bool given, bool highlight,
                                     bool error) const
{
    // 宫格边界线用粗线，内部用细线
    const auto line = [](bool thick) {
        return thick ? QStringLiteral("3px solid #333333")
                     : QStringLiteral("1px solid #b8b8b8");
    };

    const int top    = (row % 3 == 0) ? 3 : 1;
    const int bottom = (row % 3 == 2) ? 3 : 1;
    const int left   = (col % 3 == 0) ? 3 : 1;
    const int right  = (col % 3 == 2) ? 3 : 1;

    // 背景：高亮 > 初始数字 > 普通
    QString bg = highlight ? QStringLiteral("#fff2b3")
                           : (given ? QStringLiteral("#e8e8e8")
                                    : QStringLiteral("#ffffff"));
    // 字体：错误标红 > 宫格填满/行列填满且全对 柔和绿 > 初始数字深灰 > 玩家填写蓝色
    QString fg;
    if (error)
        fg = QStringLiteral("#cc0000");
    else if (isBoxFilled(row / 3, col / 3)
             || isRowOrColFilledCorrect(row, col))
        fg = QStringLiteral("#2e7d32");   // 宫格填满，或所在行/列填满且全对
    else
        fg = given ? QStringLiteral("#303030")
                   : QStringLiteral("#005ac8");

    return QStringLiteral(
               "QLineEdit {"
               "  background-color: %1;"
               "  color: %2;"
               "  border-top: %3;"
               "  border-bottom: %4;"
               "  border-left: %5;"
               "  border-right: %6;"
               "}")
        .arg(bg)
        .arg(fg)
        .arg(line(top == 3))
        .arg(line(bottom == 3))
        .arg(line(left == 3))
        .arg(line(right == 3));
}

void SudokuWidget::refreshCell(int row, int col)
{
    bool given = m_game.isGiven(row, col);

    // 与当前格子同行、同列或同宫时高亮
    bool hl = (m_highlightRow >= 0)
              && (row == m_highlightRow || col == m_highlightCol
                  || (row / 3 == m_highlightRow / 3 && col / 3 == m_highlightCol / 3));

    m_cells[row][col]->setStyleSheet(
        buildCellStyle(row, col, given, hl, m_error[row][col]));
}

bool SudokuWidget::isBoxFilled(int boxRow, int boxCol) const
{
    for (int r = boxRow * 3; r < boxRow * 3 + 3; ++r) {
        for (int c = boxCol * 3; c < boxCol * 3 + 3; ++c) {
            if (m_game.puzzle()[r][c] == 0)
                return false;
        }
    }
    return true;
}

bool SudokuWidget::isRowOrColFilledCorrect(int row, int col) const
{
    const auto &puzzle = m_game.puzzle();
    const auto &solution = m_game.solution();

    // 行：9 格全部填满且与答案一致
    bool rowOk = true;
    for (int c = 0; c < 9; ++c) {
        if (puzzle[row][c] == 0 || puzzle[row][c] != solution[row][c]) {
            rowOk = false;
            break;
        }
    }
    if (rowOk)
        return true;

    // 列：9 格全部填满且与答案一致
    for (int r = 0; r < 9; ++r) {
        if (puzzle[r][col] == 0 || puzzle[r][col] != solution[r][col])
            return false;
    }
    return true;
}

void SudokuWidget::refreshAll()
{
    for (int r = 0; r < 9; ++r)
        for (int c = 0; c < 9; ++c)
            refreshCell(r, c);
}

void SudokuWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    // 所有控件随窗口大小按比例缩放
    // 基准：初始窗口约 640x640，比例取宽高缩放的较小值并限制范围
    const qreal scale = qBound(0.6, qMin(width() / 640.0, height() / 640.0), 3.0);

    // 数独格子保持正方形且紧密相连：间距为 0，
    // 边长 = 棋盘宿主可用空间较小值 / 9，棋盘整体固定为 9x9 格的正方形并居中
    if (m_boardHost && m_gridFrame) {
        const int availW = m_boardHost->width();
        const int availH = m_boardHost->height();
        const int cellSize = qMax(30, qMin((availW - 4) / 9, (availH - 4) / 9));
        const int boardSize = cellSize * 9;

        m_gridFrame->setFixedSize(boardSize, boardSize);
        for (int r = 0; r < 9; ++r)
            for (int c = 0; c < 9; ++c)
                m_cells[r][c]->setFixedSize(cellSize, cellSize);

        // 格子数字字体：随格子实际大小调整
        const int cellFontSize = qBound(8, qRound(cellSize * 0.33), 60);
        for (int r = 0; r < 9; ++r) {
            for (int c = 0; c < 9; ++c) {
                QFont f = m_cells[r][c]->font();
                f.setPointSize(cellFontSize);
                m_cells[r][c]->setFont(f);
            }
        }
    }

    // 按钮 / 下拉框 / 复选框 / 状态栏字体
    const int ctrlFontSize = qBound(8, qRound(9.0 * scale), 40);
    const auto buttons = findChildren<QPushButton *>();
    for (QPushButton *btn : buttons) {
        // 跳过数字选择面板里的按钮（其尺寸固定，不参与缩放）
        if (m_popup && m_popup->isAncestorOf(btn))
            continue;
        QFont f = btn->font();
        f.setPointSize(ctrlFontSize);
        btn->setFont(f);
    }
    if (m_difficulty) {
        QFont f = m_difficulty->font();
        f.setPointSize(ctrlFontSize);
        m_difficulty->setFont(f);
    }
    if (m_directHint) {
        QFont f = m_directHint->font();
        f.setPointSize(ctrlFontSize);
        m_directHint->setFont(f);
    }
    if (m_helpLabel) {
        QFont f = m_helpLabel->font();
        f.setPointSize(ctrlFontSize);
        m_helpLabel->setFont(f);
    }
    if (m_difficultyLabel) {
        QFont f = m_difficultyLabel->font();
        f.setPointSize(ctrlFontSize);
        m_difficultyLabel->setFont(f);
    }
    if (m_invalidLabel) {
        QFont f = m_invalidLabel->font();
        f.setPointSize(ctrlFontSize);
        m_invalidLabel->setFont(f);
    }
    if (m_statusLabel) {
        QFont f = m_statusLabel->font();
        f.setPointSize(ctrlFontSize);
        m_statusLabel->setFont(f);
    }
}

bool SudokuWidget::eventFilter(QObject *obj, QEvent *event)
{
    // ---- 全局处理：数字面板点击外部或按 ESC 时关闭 ----
    if (m_popup && m_popup->isVisible()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const QPoint gp = me->globalPosition().toPoint();
#else
            const QPoint gp = me->globalPos();
#endif
            // 点击位置是否落在某个格子上（不关闭，交给下面逻辑移动面板）
            bool clickedCell = false;
            for (int r = 0; r < 9 && !clickedCell; ++r)
                for (int c = 0; c < 9 && !clickedCell; ++c)
                    if (obj == m_cells[r][c])
                        clickedCell = true;

            const QRect popupGlobal(m_popup->mapToGlobal(QPoint(0, 0)),
                                    m_popup->size());
            if (!clickedCell && !popupGlobal.contains(gp))
                hideNumberPopup();
        } else if (event->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            if (ke->key() == Qt::Key_Escape)
                hideNumberPopup();
        }
    }

    // ---- 鼠标按下格子：右键弹唯一提示 / 直接提示候选面板 ----
    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        int row = -1, col = -1;
        for (int r = 0; r < 9 && row < 0; ++r)
            for (int c = 0; c < 9 && row < 0; ++c)
                if (m_cells[r][c] == obj) {
                    row = r;
                    col = c;
                }

        if (row >= 0) {
            // 先同步最新输入到游戏数据，确保唯一判定
            // 始终基于"当前棋盘所有显示数字"的最新状态
            readGridToGame();

            m_highlightRow = row;
            m_highlightCol = col;
            refreshAll();

            if (m_game.isGiven(row, col))
                return QWidget::eventFilter(obj, event);

            const bool isRightClick = (me->button() == Qt::RightButton);

            // 右键空格：先尝试唯一提示
            if (isRightClick && m_game.puzzle()[row][col] == 0) {
                // "简单唯一"两种任一成立即提示（帮助玩家省去计算）：
                //   ① 行+列候选只剩 1 个（不含宫格排除）
                //   ② 该格所在 9 宫格内，某数字只有这一个位置可放
                int sole = 0;
                const auto cands = m_game.candidates(row, col, false);
                if (cands.size() == 1)
                    sole = cands[0];
                else
                    sole = m_game.boxHiddenSingle(row, col);

                if (sole > 0) {
                    m_popupFromRightClick = true;   // 右键弹面板
                    m_popupFilled = false;
                    showNumberPopup(row, col, sole);
                    return QWidget::eventFilter(obj, event);
                }

                // 不唯一：若随后会弹候选面板，交由"弹出未选"统计；
                // 否则计一次"可选项不是一个"
                m_rightClickPendingInvalid = true;
            } else {
                m_rightClickPendingInvalid = false;
            }

            // 直接提示候选面板：受"直接提示"勾选控制（仅简单难度显示该开关）
            if (isEasyMode() && m_directHint->isChecked()) {
                m_popupFromRightClick = isRightClick;   // 右键弹出的面板未选也统计
                m_popupFilled = false;
                m_rightClickPendingInvalid = false;     // 将弹面板，归入"弹出未选"
                showNumberPopup(row, col);
            } else if (m_rightClickPendingInvalid) {
                // 右键不唯一且未弹任何面板 → 计一次
                m_rightClickPendingInvalid = false;
                ++m_invalidRightClicks;
                updateInvalidLabel();
            }
        }
    }

    // ---- 格子聚焦：仅刷新高亮（键盘 Tab 导航时也生效）----
    if (event->type() == QEvent::FocusIn) {
        for (int r = 0; r < 9; ++r) {
            for (int c = 0; c < 9; ++c) {
                if (m_cells[r][c] == obj) {
                    m_highlightRow = r;
                    m_highlightCol = c;
                    refreshAll();
                    break;
                }
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

bool SudokuWidget::isEasyMode() const
{
    return m_difficulty->currentData().toInt()
           == static_cast<int>(SudokuGame::Difficulty::Easy);
}

void SudokuWidget::showNumberPopup(int row, int col, int forceNum)
{
    // 懒创建弹出面板：3x3 数字按钮
    // 注意：使用 Qt::Tool 而非 Qt::Popup——Popup 会进入模态事件循环，
    // 关闭后焦点返回格子会再次触发 FocusIn，造成无限闪烁/卡死。
    if (!m_popup) {
        m_popup = new QFrame(this,
                             Qt::Tool | Qt::FramelessWindowHint
                                 | Qt::WindowStaysOnTopHint);
        m_popup->setFrameShape(QFrame::StyledPanel);

        auto *layout = new QGridLayout(m_popup);
        layout->setSpacing(2);
        layout->setContentsMargins(4, 4, 4, 4);

        for (int i = 0; i < 9; ++i) {
            const int num = i + 1;
            auto *btn = new QPushButton(QString::number(num), m_popup);
            btn->setFixedSize(36, 36);
            // 注意：按钮只创建一次，格子的行列记录在 m_popupRow/m_popupCol 中，
            // 每次弹出时更新，避免按钮写错格子
            connect(btn, &QPushButton::clicked, this, [this, num]() {
                const int r = m_popupRow;
                const int c = m_popupCol;
                if (r < 0 || c < 0 || m_game.isGiven(r, c))
                    return;
                // 只改文字，不预先改模型：让 textChanged 统一处理
                // （区分填写/修改、取消旧自动填写、重新自动填写、检查）
                m_cells[r][c]->setText(QString::number(num));
                m_popupFilled = true;   // 选择了数字，不算"弹出未选"
                hideNumberPopup();
            });
            layout->addWidget(btn, i / 3, i % 3);
            m_popupButtons[i] = btn;
        }
    }

    m_popupRow = row;
    m_popupCol = col;

    // 根据数独规则计算该格可能的候选数字
    const auto cands = m_game.candidates(row, col);

    // forceNum > 0（唯一解提示）时只显示该数字；否则显示全部候选
    std::vector<int> display;
    if (forceNum > 0)
        display.push_back(forceNum);
    else
        display = cands;

    // 只显示候选数字按钮，其余隐藏
    for (int i = 0; i < 9; ++i) {
        bool show = false;
        for (int v : display) {
            if (v == i + 1) {
                show = true;
                break;
            }
        }
        m_popupButtons[i]->setVisible(show);
    }

    // 没有可显示的数字时不弹面板（不统计"弹出未选"）
    if (display.empty()) {
        m_popup->hide();
        return;
    }

    // 定位到格子的右下方（顶层窗口使用屏幕坐标）
    QPoint pos = m_cells[row][col]->mapToGlobal(QPoint(0, 0));
    pos += QPoint(m_cells[row][col]->width() + 6, 0);

    // 已显示且位置未变：保持不动，避免闪烁
    if (m_popup->isVisible()) {
        if (m_popup->pos() != pos)
            m_popup->move(pos);
        m_popup->raise();
        return;
    }
    m_popup->move(pos);
    m_popup->show();
    m_popup->raise();
}

void SudokuWidget::hideNumberPopup()
{
    if (m_popup) {
        // 先统计"右键弹出但未选择"，再隐藏
        countUnselectedPopup();
        m_popup->hide();
    }
}

void SudokuWidget::countUnselectedPopup()
{
    if (m_popupFromRightClick && !m_popupFilled) {
        // 右键弹出面板但没有选择数字就关闭 → 无效右键
        ++m_invalidRightClicks;
        updateInvalidLabel();
    }
    m_popupFromRightClick = false;
    m_popupFilled = false;
}

void SudokuWidget::updateInvalidLabel()
{
    if (m_invalidLabel) {
        m_invalidLabel->setText(
            QStringLiteral("无效右键：%1").arg(m_invalidRightClicks));
    }
}

void SudokuWidget::readGridToGame()
{
    for (int r = 0; r < 9; ++r) {
        for (int c = 0; c < 9; ++c) {
            if (m_game.isGiven(r, c))
                continue;
            const QString text = m_cells[r][c]->text();
            int v = text.isEmpty() ? 0 : text.toInt();
            m_game.setValue(r, c, v);
        }
    }
}

void SudokuWidget::onNewGame()
{
    // 隐藏可能打开的数字选择面板（未选择的右键面板也会统计）
    if (m_popup && m_popup->isVisible())
        hideNumberPopup();

    int holes = m_difficulty->currentData().toInt();
    m_game.newGame(static_cast<SudokuGame::Difficulty>(holes));
    applyPuzzleToGrid();

    // 重置统计，开始计时
    m_invalidRightClicks = 0;
    m_popupFromRightClick = false;
    m_popupFilled = false;
    m_rightClickPendingInvalid = false;
    updateInvalidLabel();
    m_timer.restart();

    m_statusLabel->setText(QStringLiteral("新游戏已生成，难度：%1")
                           .arg(m_difficulty->currentText()));
}

void SudokuWidget::fillCell(int row, int col)
{
    if (row < 0 || row > 8 || col < 0 || col > 8)
        return;
    if (m_game.isGiven(row, col))
        return;

    // 程序填入，避免 textChanged 触发递归检查
    m_loadingGrid = true;
    m_game.setValue(row, col, m_game.hint(row, col));
    m_cells[row][col]->setText(QString::number(m_game.hint(row, col)));
    m_loadingGrid = false;

    // 标记为"自动填写的数字"（累计 + 最近一次）：
    // 修改任意数字时累计的全部取消；删除任意数字时只取消最近一次的
    m_autoFilled[row][col] = true;
    m_autoFilledRound[row][col] = true;
}

/// 取消所有自动填写的数字（修改数字时用，清空格子）。
/// exceptRow/exceptCol 指定的格子（正在被玩家修改的格子）仅撤销
/// "自动填写"标记、保留玩家输入，不清空。
void SudokuWidget::cancelAutoFilled(int exceptRow, int exceptCol)
{
    m_loadingGrid = true;
    for (int r = 0; r < 9; ++r) {
        for (int c = 0; c < 9; ++c) {
            if (!m_autoFilled[r][c])
                continue;
            m_autoFilled[r][c] = false;
            m_autoFilledRound[r][c] = false;
            if (r == exceptRow && c == exceptCol)
                continue;   // 正在被玩家操作的格子：保留玩家输入
            m_game.setValue(r, c, 0);
            m_cells[r][c]->clear();
        }
    }
    m_loadingGrid = false;
}

/// 只取消"最近一次自动填写"的数字（删除数字时用，清空格子）。
/// exceptRow/exceptCol 指定的格子（正在被玩家删除的格子）仅撤销
/// "自动填写"标记，不清空（其文字已被删除变空）。
void SudokuWidget::cancelAutoFilledRound(int exceptRow, int exceptCol)
{
    m_loadingGrid = true;
    for (int r = 0; r < 9; ++r) {
        for (int c = 0; c < 9; ++c) {
            if (!m_autoFilledRound[r][c])
                continue;
            m_autoFilledRound[r][c] = false;
            m_autoFilled[r][c] = false;
            if (r == exceptRow && c == exceptCol)
                continue;   // 正在被玩家操作的格子：保留玩家输入
            m_game.setValue(r, c, 0);
            m_cells[r][c]->clear();
        }
    }
    m_loadingGrid = false;
}

void SudokuWidget::autoFillLastCells(int seedRow, int seedCol)
{
    // 只检查"刚填写的数字"所在的三个区域：所在行、所在列、所在 3x3 宫格。
    // 其他行/列/宫格即使只剩 1 个空格也不自动填写。
    // 循环检查这三个区域，直到都不再有"最后空格"为止。
    // 每次自动填写开始时清空"最近一次"标记——本次填入的数字同时标记为
    // 累计（m_autoFilled，修改数字时全部取消）与最近一次
    // （m_autoFilledRound，删除数字时只取消这些）
    for (int r = 0; r < 9; ++r)
        for (int c = 0; c < 9; ++c)
            m_autoFilledRound[r][c] = false;

    bool any = true;
    while (any) {
        any = false;
        readGridToGame();
        const auto &puzzle = m_game.puzzle();

        // 种子所在行只剩 1 个空格
        {
            int ec = -1, cnt = 0;
            for (int c = 0; c < 9; ++c) {
                if (puzzle[seedRow][c] == 0) {
                    ec = c;
                    ++cnt;
                }
            }
            if (cnt == 1) {
                any = true;
                fillCell(seedRow, ec);
            }
        }

        // 种子所在列只剩 1 个空格
        {
            int er = -1, cnt = 0;
            for (int r = 0; r < 9; ++r) {
                if (puzzle[r][seedCol] == 0) {
                    er = r;
                    ++cnt;
                }
            }
            if (cnt == 1) {
                any = true;
                fillCell(er, seedCol);
            }
        }

        // 种子所在 3x3 宫格只剩 1 个空格
        {
            const int br = (seedRow / 3) * 3;
            const int bc = (seedCol / 3) * 3;
            int er = -1, ec = -1, cnt = 0;
            for (int r = br; r < br + 3; ++r) {
                for (int c = bc; c < bc + 3; ++c) {
                    if (puzzle[r][c] == 0) {
                        er = r;
                        ec = c;
                        ++cnt;
                    }
                }
            }
            if (cnt == 1) {
                any = true;
                fillCell(er, ec);
            }
        }
    }
}

void SudokuWidget::checkForErrors()
{
    readGridToGame();

    // 注意：自动填写不在这里进行——由 textChanged 按
    // 填写/修改/删除分别控制（删除时不自动填写，避免删除被还原）

    // 清空上次的错误标记
    for (int r = 0; r < 9; ++r)
        for (int c = 0; c < 9; ++c)
            m_error[r][c] = false;

    // 只把"已填写但与答案不同"的数字视为错误；空格不算错误
    const auto &puzzle = m_game.puzzle();
    const auto &solution = m_game.solution();
    int errorCount = 0;
    for (int r = 0; r < 9; ++r) {
        for (int c = 0; c < 9; ++c) {
            if (puzzle[r][c] != 0 && puzzle[r][c] != solution[r][c]) {
                m_error[r][c] = true;
                ++errorCount;
            }
        }
    }

    refreshAll();

    if (errorCount > 0) {
        m_statusLabel->setText(
            QStringLiteral("发现 %1 处错误（已用红色标出）").arg(errorCount));
        return;
    }

    // 没有错误：判断是否已全部填完
    bool filled = true;
    for (int r = 0; r < 9 && filled; ++r)
        for (int c = 0; c < 9 && filled; ++c)
            if (puzzle[r][c] == 0)
                filled = false;

    if (filled) {
        // 先统计/关闭可能还开着的"右键弹出未选择"面板，
        // 确保完成提示中的无效右键次数准确
        hideNumberPopup();

        // 结算用时
        const qint64 elapsedMs = m_timer.elapsed();
        const int totalSecs = static_cast<int>(elapsedMs / 1000);
        const int minutes = totalSecs / 60;
        const int seconds = totalSecs % 60;

        QString timeText;
        if (minutes > 0)
            timeText = QStringLiteral("%1 分 %2 秒").arg(minutes).arg(seconds);
        else
            timeText = QStringLiteral("%1 秒").arg(seconds);

        QMessageBox::information(
            this, QStringLiteral("完成"),
            QStringLiteral("恭喜！全部填写正确，用时 %1！\n"
                           "本次无效右键次数：%2 次\n"
                           "点击确定开始新游戏。")
                .arg(timeText)
                .arg(m_invalidRightClicks));

        // 点确定后自动生成新游戏
        onNewGame();
        return;
    }

    m_statusLabel->setText(QStringLiteral("目前没有错误，继续加油！"));
}

void SudokuWidget::onHint()
{
    // 找到第一个为空的格子填入提示；没有空格则提示已满
    // 只改文字，不预先改模型：textChanged 统一处理（含自动填写）
    readGridToGame();
    const auto &puzzle = m_game.puzzle();
    for (int r = 0; r < 9; ++r) {
        for (int c = 0; c < 9; ++c) {
            if (!m_game.isGiven(r, c) && puzzle[r][c] == 0) {
                int v = m_game.hint(r, c);
                m_cells[r][c]->setText(QString::number(v));
                m_statusLabel->setText(QStringLiteral("已提示 (行%1, 列%2)：%3")
                                       .arg(r + 1).arg(c + 1).arg(v));
                return;
            }
        }
    }
    m_statusLabel->setText(QStringLiteral("所有格子都已填满。"));
}

void SudokuWidget::onClear()
{
    // 整盘清空属于批量操作（仅玩家填写的内容，含自动填写的数字），
    // 置 loading 标志避免逐个清空时触发 textChanged 的逐格处理
    m_loadingGrid = true;
    for (int r = 0; r < 9; ++r) {
        for (int c = 0; c < 9; ++c) {
            if (!m_game.isGiven(r, c)) {
                m_cells[r][c]->clear();
                m_autoFilled[r][c] = false;
                m_autoFilledRound[r][c] = false;
            }
        }
    }
    m_loadingGrid = false;
    readGridToGame();
    m_statusLabel->setText(QStringLiteral("已清空玩家填写的内容。"));
}

void SudokuWidget::onCopyBoard()
{
    // 同步最新输入，生成 9 行文字盘面（0 = 空格）
    readGridToGame();
    const auto &puzzle = m_game.puzzle();

    QString text;
    for (int r = 0; r < 9; ++r) {
        for (int c = 0; c < 9; ++c)
            text += QChar('0' + puzzle[r][c]);
        if (r < 8)
            text += QLatin1Char('\n');
    }

    QApplication::clipboard()->setText(text);
    m_statusLabel->setText(QStringLiteral("盘面已复制到剪贴板（9 行文字）"));
}
