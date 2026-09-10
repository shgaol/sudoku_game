#include "sudokugame.h"

#include <algorithm>
#include <chrono>

SudokuGame::SudokuGame()
    : m_rng(static_cast<std::mt19937::result_type>(
          std::chrono::high_resolution_clock::now().time_since_epoch().count()))
{
    m_solution.assign(9, std::vector<int>(9, 0));
    m_puzzle.assign(9, std::vector<int>(9, 0));
    m_given.assign(9, std::vector<bool>(9, false));
}

void SudokuGame::newGame(Difficulty diff)
{
    // 1. 生成一个完整解
    m_solution.assign(9, std::vector<int>(9, 0));
    solve(m_solution);

    // 2. 拷贝为谜题
    m_puzzle = m_solution;
    m_given.assign(9, std::vector<bool>(9, false));

    // 3. 挖洞（按难度控制每个小宫格的空格数）
    digHoles(diff);
}

bool SudokuGame::setValue(int row, int col, int value)
{
    if (row < 0 || row > 8 || col < 0 || col > 8)
        return false;
    if (m_given[row][col])
        return false;               // 初始数字不可修改
    if (value < 0 || value > 9)
        return false;

    m_puzzle[row][col] = value;
    return true;
}

bool SudokuGame::check() const
{
    for (int r = 0; r < 9; ++r) {
        for (int c = 0; c < 9; ++c) {
            if (m_puzzle[r][c] != m_solution[r][c])
                return false;
        }
    }
    return true;
}

std::vector<int> SudokuGame::candidates(int row, int col, bool includeBox) const
{
    std::vector<int> result;
    if (row < 0 || row > 8 || col < 0 || col > 8)
        return result;

    for (int num = 1; num <= 9; ++num) {
        // 行 + 列排除
        bool ok = true;
        for (int i = 0; i < 9; ++i) {
            if (m_puzzle[row][i] == num || m_puzzle[i][col] == num) {
                ok = false;
                break;
            }
        }
        if (!ok)
            continue;

        // 宫格排除（可选）
        if (includeBox) {
            const int br = (row / 3) * 3;
            const int bc = (col / 3) * 3;
            for (int r = br; r < br + 3 && ok; ++r) {
                for (int c = bc; c < bc + 3; ++c) {
                    if (m_puzzle[r][c] == num) {
                        ok = false;
                        break;
                    }
                }
            }
        }

        if (ok)
            result.push_back(num);
    }
    return result;
}

int SudokuGame::boxHiddenSingle(int row, int col) const
{
    if (row < 0 || row > 8 || col < 0 || col > 8)
        return 0;
    if (m_puzzle[row][col] != 0)
        return 0;   // 只对空格判断

    const int br = (row / 3) * 3;
    const int bc = (col / 3) * 3;

    for (int num = 1; num <= 9; ++num) {
        // 数字 num 必须还没在宫格内出现
        bool inBox = false;
        for (int r = br; r < br + 3 && !inBox; ++r)
            for (int c = bc; c < bc + 3 && !inBox; ++c)
                if (m_puzzle[r][c] == num)
                    inBox = true;
        if (inBox)
            continue;

        // 统计 num 在宫格内的可放位置（行列宫均不冲突）
        int cnt = 0;
        int onlyR = -1, onlyC = -1;
        for (int r = br; r < br + 3; ++r) {
            for (int c = bc; c < bc + 3; ++c) {
                if (m_puzzle[r][c] != 0)
                    continue;
                if (isValid(m_puzzle, r, c, num)) {
                    ++cnt;
                    onlyR = r;
                    onlyC = c;
                }
            }
        }

        // 宫格内唯一且唯一位置就是 (row, col)
        if (cnt == 1 && onlyR == row && onlyC == col)
            return num;
    }

    return 0;
}

bool SudokuGame::isValid(const std::vector<std::vector<int>>& board,
                         int row, int col, int num) const
{
    for (int i = 0; i < 9; ++i) {
        if (board[row][i] == num || board[i][col] == num)
            return false;
    }
    int br = (row / 3) * 3;
    int bc = (col / 3) * 3;
    for (int r = br; r < br + 3; ++r) {
        for (int c = bc; c < bc + 3; ++c) {
            if (board[r][c] == num)
                return false;
        }
    }
    return true;
}

bool SudokuGame::solve(std::vector<std::vector<int>>& board, int pos)
{
    if (pos == 81)
        return true;

    int row = pos / 9;
    int col = pos % 9;

    if (board[row][col] != 0)
        return solve(board, pos + 1);

    // 随机顺序尝试 1~9，使每次生成的解不同
    std::vector<int> nums = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::shuffle(nums.begin(), nums.end(), m_rng);

    for (int num : nums) {
        if (isValid(board, row, col, num)) {
            board[row][col] = num;
            if (solve(board, pos + 1))
                return true;
            board[row][col] = 0;
        }
    }
    return false;
}

void SudokuGame::digHoles(Difficulty diff)
{
    // 按难度确定每个 3x3 小宫格的目标空格数：
    //   简单：以 3 个为主，偶尔 1 个宫格为 4 个空格（约 30% 概率）
    //   中等：以 3 个为主，至少 2 个宫格为 4 个空格（2~3 个）
    //   困难：以 3 个为主，至少 2 个宫格为 4 个空格，
    //         且至少 1 个、至多 3 个宫格为 5 个空格
    std::vector<int> boxes = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<int> target(9, 3);

    switch (diff) {
    case Difficulty::Easy: {
        // 偶尔 1 个宫格为 4（约 30% 概率），其余全 3
        std::uniform_int_distribution<int> chance(0, 99);
        if (chance(m_rng) < 30) {
            std::shuffle(boxes.begin(), boxes.end(), m_rng);
            target[boxes[0]] = 4;
        }
        break;
    }
    case Difficulty::Medium: {
        // 至少 2 个宫格为 4（随机 2~3 个）
        std::uniform_int_distribution<int> cnt(2, 3);
        const int n = cnt(m_rng);
        std::shuffle(boxes.begin(), boxes.end(), m_rng);
        for (int i = 0; i < n; ++i)
            target[boxes[i]] = 4;
        break;
    }
    case Difficulty::Hard: {
        // 至少 2 个宫格为 4（随机 2~3 个）
        std::shuffle(boxes.begin(), boxes.end(), m_rng);
        std::uniform_int_distribution<int> cnt4(2, 3);
        const int n4 = cnt4(m_rng);
        for (int i = 0; i < n4; ++i)
            target[boxes[i]] = 4;
        // 至少 1、至多 3 个宫格为 5（不与 4 的宫格重叠）
        std::uniform_int_distribution<int> cnt5(1, 3);
        const int n5 = cnt5(m_rng);
        for (int i = n4; i < n4 + n5; ++i)
            target[boxes[i]] = 5;
        break;
    }
    }

    // 逐宫格挖洞：空格位置按"整盘 行+列 双重均衡"选择——
    // 每次优先选"该格所在行已挖数 + 该格所在列已挖数"最小的位置（同分随机），
    // 让空格在整个盘面上均匀分布（每行/每列的空格数尽量均匀、相差最多 1），
    // 不会出现某些行/列空出 5~6 格、而其他行/列几乎没有空格的"集中"情况。
    // 行/列计数用"全局"的 9 行 9 列（而非宫格内局部），保证跨宫格也均衡
    std::vector<int> rowHoles(9, 0), colHoles(9, 0);   // 整盘每行/每列已挖数

    for (int b = 0; b < 9; ++b) {
        const int br = (b / 3) * 3;
        const int bc = (b % 3) * 3;

        std::vector<std::vector<bool>> tried(3, std::vector<bool>(3, false));

        int dug = 0;
        while (dug < target[b]) {
            // 在所有未尝试位置中找最小评分（所在全局行已挖 + 所在全局列已挖）
            int bestScore = 99;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    if (!tried[i][j])
                        bestScore = std::min(bestScore,
                                             rowHoles[br + i] + colHoles[bc + j]);
            if (bestScore == 99)
                break;   // 该宫格 9 个位置都已尝试过，尽力为止

            // 收集同分位置，随机选一个（保证随机性 + 整盘行列均衡）
            std::vector<std::pair<int, int>> picks;
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    if (!tried[i][j] && rowHoles[br + i] + colHoles[bc + j] == bestScore)
                        picks.push_back({i, j});
                }
            }
            std::uniform_int_distribution<int> pick(0,
                                                    static_cast<int>(picks.size()) - 1);
            const auto pos = picks[pick(m_rng)];
            const int ri = pos.first;
            const int ci = pos.second;

            tried[ri][ci] = true;
            const int r = br + ri;
            const int c = bc + ci;

            const int backup = m_puzzle[r][c];
            m_puzzle[r][c] = 0;

            // 挖掉后仍保证存在解（本题解本身即是）
            std::vector<std::vector<int>> test = m_puzzle;
            if (solve(test)) {
                m_given[r][c] = false;
                ++dug;
                ++rowHoles[r];   // 用全局行/列计数
                ++colHoles[c];
            } else {
                m_puzzle[r][c] = backup;   // 无解，还原换下一个位置
            }
        }
    }
}
