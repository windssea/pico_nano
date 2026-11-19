# 三国华容道题库 / Three Kingdoms Klotski Bank

- `generate.py`：生成并运行精确 BFS 最短路径求解器，离线证明全部 60 关可解、计算最优步数并内嵌最优滑行解序列，输出 `puzzles.pnkl`（`PNKL` v1，带 IEEE CRC-32）与 `puzzles.json`。
- `test_bank.py`：独立二进制与规则回放校验，确认 3 档各 20 关（入门 / 进阶 / 名局）、无重叠、双空位与全部最优解直达出口 `(col=1, row=3)`。
- 全部规则、布局、名局题名与代码均为本项目原创整理，无外部专有素材。
