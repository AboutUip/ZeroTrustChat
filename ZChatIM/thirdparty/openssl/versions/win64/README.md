# 兼容落点：`versions/win64/current`

若不想用 **`prebuilt/windows-x64/openssl/`**，可把安装树放在：

```
versions/win64/current/
  include/openssl/ssl.h
  lib/libcrypto.lib
```

与历史 **`versions/current`** 相同结构，仅多一层 **`win64`** 便于区分平台制品。
