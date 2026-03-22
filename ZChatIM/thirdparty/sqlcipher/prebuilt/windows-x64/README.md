# Windows x64 — SQLCipher **amalgamation** 落点（可选）

在 **OpenSSL 安装树就绪** 之后，你在本机生成好的 **`sqlite3.c` / `sqlite3.h`** 可放于此，便于与 OpenSSL 制品**分开管理**：

```
prebuilt/windows-x64/amalgamation/
  sqlite3.c
  sqlite3.h
```

CMake 规则：**若**上述两个文件都存在，则 **优先**使用该目录作为 SQLCipher 源码；否则回退到与 **`sqlcipher/README.md` 同级**的 **`sqlite3.c` / `sqlite3.h`**。

生成 amalgamation 的方式仍见上级 **`../README.md`**（`nmake ... sqlite3.c` 或 Unix `make sqlite3.c`）。

大文件默认 **不入 Git**（见仓库根 **`.gitignore`**）。
