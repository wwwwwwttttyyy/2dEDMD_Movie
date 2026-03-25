"""
visualize.py  
用法: python visualize.py [snapshot_end.sph]
"""
import sys
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.collections as mc
import matplotlib.patches as mpatches

# ---- 读取快照文件 ----
filename = sys.argv[1] if len(sys.argv) > 1 else "snapshot_end.dat"

data = []
with open(filename) as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        data.append(line.split())

lx, ly, n = float(data[0][0]), float(data[0][1]), int(data[0][2])
rows = np.array(data[1:n+1], dtype=float)   # x y r [type]

# 折回盒子（outputSnapshot 输出的是绝对坐标）
x = rows[:, 0] % lx
y = rows[:, 1] % ly
r = rows[:, 2]
t = rows[:, 3].astype(int) if rows.shape[1] > 3 else np.zeros(n, dtype=int)

# ---- 生成 PBC 幽灵粒子 ----
# 对于跨越边界的粒子，在对面补一份副本
gx, gy, gr, gt = [x.copy()], [y.copy()], [r.copy()], [t.copy()]

for ox, oy in [(-lx,0),(lx,0),(0,-ly),(0,ly),(-lx,-ly),(-lx,ly),(lx,-ly),(lx,ly)]:
    # 只保留圆盘确实跨入盒子的幽灵
    mask = (x + ox + r > 0) & (x + ox - r < lx) & \
           (y + oy + r > 0) & (y + oy - r < ly)
    gx.append(x[mask] + ox)
    gy.append(y[mask] + oy)
    gr.append(r[mask])
    gt.append(t[mask])

gx = np.concatenate(gx)
gy = np.concatenate(gy)
gr = np.concatenate(gr)
gt = np.concatenate(gt)

# ---- 绘图 ----
fig, ax = plt.subplots(figsize=(7, 7 * ly / lx))

cmap   = plt.get_cmap("tab10")
colors = cmap(gt % 10)

circles = [mpatches.Circle((xi, yi), ri) for xi, yi, ri in zip(gx, gy, gr)]
col = mc.PatchCollection(circles, facecolors=colors, linewidths=0)
ax.add_collection(col)

ax.set_xlim(0, lx)
ax.set_ylim(0, ly)
ax.set_aspect("equal")
ax.axis("off")
ax.set_title(f"{filename}   N={n}   φ={np.sum(np.pi * r**2) / (lx * ly):.4f}", fontsize=10)

plt.tight_layout()
plt.savefig("snapshot.png", dpi=150, bbox_inches="tight")
plt.show()
print(f"Saved: snapshot.png")
