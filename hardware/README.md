# 硬件与外壳（v7.36）

| 文件 | 说明 |
|------|------|
| [`case/`](case/) | 卡扣外壳源码 + STL（**76×110×58 mm**） |
| [`case/PRINT.md`](case/PRINT.md) | 给商家打印说明 |
| [`case/COMPONENTS.md`](case/COMPONENTS.md) | 配件尺寸与舱位 |
| [`case/stl/saoti_front.stl`](case/stl/saoti_front.stl) / [`saoti_back.stl`](case/stl/saoti_back.stl) | **打印交付** |
| [`WIRING.md`](WIRING.md) / [`wiring_all.png`](wiring_all.png) | 全系统接线 |
| [`ASSEMBLY.md`](ASSEMBLY.md) | 装配长文（部分引脚可能过时，以 `firmware/include/pins.h` 为准） |
| [`给店家-接线说明.txt`](给店家-接线说明.txt) | 店家简版 |

生成外壳：

```bash
cd case
python3 generate_case.py
# 可选：配件就位预览
python3 generate_packed_assembly.py
```

材料：**PETG**；装配：卡扣，免胶免螺丝。
