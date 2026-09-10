#ifndef SUDOKUGAME_H
#define SUDOKUGAME_H

#include <vector>
#include <utility>
#include <random>

/**
 * @brief 数独核心逻辑类：负责生成谜题、求解、验证。
 *
 * 棋盘用 9x9 的 int 数组表示，0 表示空格，1~9 表示数字。
 */
class SudokuGame
{
public:
    // 难度标识（值仅作下拉框数据使用；实际空格数由 digHoles 按宫格控制）
    enum class Difficulty {
        Easy = 35,
        Medium = 50,
        Hard = 60
    };

    SudokuGame();

    /// 生成一局新的谜题（难度可选）
    void newGame(Difficulty diff = Difficulty::Medium);

    /// 返回当前谜题（只读视图，0 表示空格）
    const std::vector<std::vector<int>>& puzzle() const { return m_puzzle; }

    /// 返回完整解（用于"提示/答案"）
    const std::vector<std::vector<int>>& solution() const { return m_solution; }

    /// 判断某个格子是否是谜题给出的初始数字（不可修改）
    bool isGiven(int row, int col) const { return m_given[row][col]; }

    /// 尝试在 (row, col) 填入 value；不合法返回 false
    bool setValue(int row, int col, int value);

    /// 检查当前玩家填写的内容是否与答案一致（全对返回 true）
    bool check() const;

    /// 获取 (row, col) 的提示（返回答案值，调用前请判断 isGiven）
    int hint(int row, int col) const { return m_solution[row][col]; }

    /// 根据数独规则，返回 (row, col) 处所有可能填入的数字。
    /// includeBox=true 时同时排除同行、同列、同宫已出现的数字；
    /// includeBox=false 时只排除同行、同列的数字（唯一判定用，更宽松）。
    /// 结果升序排列。
    std::vector<int> candidates(int row, int col, bool includeBox = true) const;

    /// 宫格内唯一（hidden single in box）：
    /// 若存在某个数字 num 在 (row, col) 所在的 3x3 宫格中
    /// 只有 (row, col) 一个位置可放，返回 num；否则返回 0。
    int boxHiddenSingle(int row, int col) const;

private:
    std::vector<std::vector<int>> m_solution;  // 完整解
    std::vector<std::vector<int>> m_puzzle;    // 玩家当前棋盘
    std::vector<std::vector<bool>> m_given;    // 初始给出的格子

    // 随机数引擎：用系统时钟时间做种子。
    // 注意：MinGW 下 std::random_device 可能是确定性的（每次返回相同值），
    // 导致每次生成的谜题都一样，因此改用时间种子。
    std::mt19937 m_rng;

    /// 用回溯法求一个完整解，成功返回 true
    bool solve(std::vector<std::vector<int>>& board, int pos = 0);

    /// 检查 board 中数字是否满足数独规则
    bool isValid(const std::vector<std::vector<int>>& board, int row, int col, int num) const;

    /// 挖洞生成谜题：按难度控制每个 3x3 小宫格内的空格数——
    /// 简单：以 3 个为主，偶尔 1 个宫格为 4 个（约 30% 概率）；
    /// 中等：以 3 个为主，至少 2 个宫格为 4 个（2~3 个）；
    /// 困难：以 3 个为主，至少 2 个宫格为 4 个，
    ///       且至少 1 个、至多 3 个宫格为 5 个。
    /// 挖洞位置按"整盘 行+列 双重均衡"选择：每次优先挖"所在行 + 所在列
    /// 已挖数最少"的位置，空格在整个盘面均匀分布（每行/每列空格数尽量均匀、
    /// 相差最多 1），不会出现某些行/列空出很多而其他行/列几乎没有的集中情况，
    /// 并保证有解
    void digHoles(Difficulty diff);
};

#endif // SUDOKUGAME_H
