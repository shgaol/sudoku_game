#ifndef SUDOKUWIDGET_H
#define SUDOKUWIDGET_H

#include <QWidget>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QElapsedTimer>

#include "sudokugame.h"

class QFrame;
class QPushButton;
class QCheckBox;

/**
 * @brief 数独游戏主界面。
 *
 * 使用 9x9 的 QLineEdit 网格，提供"新游戏 / 清空 / 生成盘面 / 提示"等功能，
 * 自动检查（无"检查"按钮）＋ 自动填写"最后空格"（仅围绕所填数字
 * 所在的行/列/3x3 宫格，其他区域即使只剩 1 格也不自动填）。
 * 初始数字（出题给出）只读，玩家填写的数字可自由修改或删除；
 * 自动填写的数字不是永久保留——玩家修改任意数字时全部取消（再重新自动填写），
 * 删除任意数字时只取消最近一次自动填写的数字。
 * 带 3x3 宫格粗边框；点击/聚焦格子会高亮其所在行、列与宫格，辅助判断冲突。
 * 简单难度下可选"直接提示"：勾选时选中可编辑格子弹出候选数字面板
 * （根据数独规则过滤），取消勾选则不弹（仅简单难度显示该选项）。
 * 右键空格可弹"唯一解提示"（行列唯一或宫格内唯一），并统计无效右键次数。
 */
class SudokuWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SudokuWidget(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onNewGame();
    void onHint();
    void onClear();
    /// 把当前棋盘生成 9x9 文字并复制到剪贴板（供外部分析）
    void onCopyBoard();

private:
    void setupUi();
    void applyPuzzleToGrid();
    void readGridToGame();
    /// 自动检查：标红错误数字；全部填对则弹完成提示并自动开新局。
    /// 注意：自动填写由 textChanged 按 填写/修改/删除 分别控制，
    /// 这里只做错误检查与完成判定
    void checkForErrors();
    /// 自动填写：填写数字后，仅检查该数字所在的行、列、3x3 宫格——
    /// 若其中某区域只剩 1 个空格则填入答案；
    /// 其他行/列/宫格即使只剩 1 个空格也不自动填写。
    /// 每次自动填写开始时清空"最近一次"标记（m_autoFilledRound），
    /// 本次填入的数字同时标记为累计（m_autoFilled）与最近一次（m_autoFilledRound）：
    /// 修改数字时全部取消，删除数字时只取消最近一次的
    void autoFillLastCells(int seedRow, int seedCol);
    /// 取消所有自动填写的数字（修改数字时用，清空格子）；
    /// exceptRow/exceptCol 指定的格子（正在被玩家修改的格子）
    /// 仅撤销"自动填写"标记、保留玩家输入，不清空
    void cancelAutoFilled(int exceptRow, int exceptCol);
    /// 只取消"最近一次自动填写"的数字（删除数字时用，清空格子）；
    /// exceptRow/exceptCol 指定的格子（正在被玩家删除的格子）
    /// 仅撤销"自动填写"标记，不清空
    void cancelAutoFilledRound(int exceptRow, int exceptCol);
    /// 向格子 (r, c) 填入答案（自动填写用，避免触发递归检查），
    /// 并标记为自动填写的数字（累计 + 最近一次）
    void fillCell(int row, int col);

    /// 生成某个格子的样式表（粗/细边框 + 颜色 + 高亮 + 错误标红）
    QString buildCellStyle(int row, int col, bool given, bool highlight,
                           bool error) const;
    /// 判断某个 3x3 宫格是否已全部填满（9 格都有数字）
    bool isBoxFilled(int boxRow, int boxCol) const;
    /// 判断 (row, col) 所在的行或列是否"填满且没有错误"（全对）
    bool isRowOrColFilledCorrect(int row, int col) const;
    /// 刷新单个格子的样式
    void refreshCell(int row, int col);
    /// 刷新全部格子样式
    void refreshAll();
    /// 在 (row, col) 格子旁弹出 1~9 数字选择面板；
    /// forceNum > 0 时只显示该数字（唯一解提示）
    void showNumberPopup(int row, int col, int forceNum = 0);
    /// 关闭数字面板，并统计"右键弹出但未选择"的无效右键
    void hideNumberPopup();
    /// 统计"右键弹出但未选择"（m_popupFromRightClick && !m_popupFilled）
    void countUnselectedPopup();
    /// 刷新侧边栏无效右键计数显示
    void updateInvalidLabel();
    /// 当前是否处于"简单"难度
    bool isEasyMode() const;

    SudokuGame m_game;

    QLineEdit *m_cells[9][9];
    QWidget   *m_boardHost = nullptr;  // 棋盘宿主（占满左侧空间，棋盘在其中居中）
    QFrame    *m_gridFrame = nullptr;  // 棋盘容器（resizeEvent 中计算正方形尺寸）
    QComboBox *m_difficulty;
    QCheckBox *m_directHint = nullptr; // "直接提示"开关（仅简单难度显示）
    QLabel    *m_helpLabel = nullptr;  // 侧边栏操作/规则说明
    QLabel    *m_difficultyLabel = nullptr; // 侧边栏"当前难度"显示
    QLabel    *m_invalidLabel = nullptr; // 侧边栏"无效右键次数"实时显示
    QLabel    *m_statusLabel;

    QFrame *m_popup = nullptr;      // 简单模式的数字选择面板
    QPushButton *m_popupButtons[9]; // 面板上的 1~9 按钮
    int    m_popupRow = -1;         // 面板当前对应的格子行
    int    m_popupCol = -1;         // 面板当前对应的格子列

    int m_highlightRow = -1;   // 当前高亮格子所在行，-1 表示无
    int m_highlightCol = -1;   // 当前高亮格子所在列，-1 表示无
    bool m_error[9][9];        // 检查时标记的填错格子（红色显示）
    bool m_autoFilled[9][9];   // 累计：标记所有"自动填写"过的格子（修改数字时全部取消）
    bool m_autoFilledRound[9][9]; // 最近一次：标记最近一次自动填写填入的格子（删除数字时只取消这些）
    QElapsedTimer m_timer;     // 游戏计时：新游戏开始计时，完成时结算
    bool m_loadingGrid = false; // 程序正在加载棋盘（setText 时不触发自动检查）
    int  m_invalidRightClicks = 0; // 无效右键次数（右键但可选项不是一个）
    bool m_popupFromRightClick = false; // 当前面板是否由右键触发
    bool m_popupFilled = false;        // 面板弹出后是否点选了数字
    bool m_rightClickPendingInvalid = false; // 右键不唯一，待定如何计数
};

#endif // SUDOKUWIDGET_H
