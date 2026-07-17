# 硬件与外壳（v7.1）

| 文件 | 说明 |
|------|------|
| [`case/`](case/) | 卡扣外壳源码 + STL（**106×140×70 mm**） |
| [`case/PRINT.md`](case/PRINT.md) | 给商家打印说明 |
| [`case/COMPONENTS.md`](case/COMPONENTS.md) | 配件尺寸与杜邦占位 |
| [`case/saoti-guajian-3d-print-v7.1.zip`](case/saoti-guajian-3d-print-v7.1.zip) | 前/后壳交付包 |
| [`WIRING.md`](WIRING.md) / [`wiring_all.png`](wiring_all.png) | 全系统接线 |
| [`ASSEMBLY.md`](ASSEMBLY.md) | 历史装配长文（部分引脚可能过时，以 `firmware/include/pins.h` 为准） |
| [`给店家-接线说明.txt`](给店家-接线说明.txt) | 店家简版 |

生成外壳：

```bash
cd case
python3 generate_case.py
python3 generate_packed_assembly.py   # 可选：配件就位预览
```

材料：**PETG**；装配：卡扣，免胶免螺丝。
