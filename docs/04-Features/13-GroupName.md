# 群组名称修改技术规范

> ZSP：**`GROUP_NAME_UPDATE` = 0x1C**（非 `0x10` `GROUP_UPDATE`）。可选 TLV **`0x20` GroupName**（[`02-ZSP-Protocol.md`](../01-Architecture/02-ZSP-Protocol.md)）。持久化 **`mm2_group_display`**（[`03-Storage.md`](../02-Core/03-Storage.md) 第2.5节）。JNI：`updateGroupName`→`GroupNameManager`→`MM2`；`getGroupName` 仅 caller（[`01-JNI.md`](../06-Appendix/01-JNI.md)）。

---

## 一、功能

```
群主/管理员可修改群名称
```

---

## 二、消息流程（ZSP）

```
修改群名称:
1. 发送 GROUP_NAME_UPDATE（MessageType = 0x1C）
2. 载荷内可带 TLV 0x20（GroupName）等扩展
3. MM1 验证权限与签名（JNI 路径）
4. MM2 持久化群名元数据（**`mm2_group_display`**）
5. 广播通知所有成员
```

---

## 三、TLV扩展（可选）

```
TLV 扩展（与 第五节 MessageType 独立枚举）:

Type: 0x20 (GroupName)
Length: 可变
Value:
┌─────────────────────────────────────┐
│  GroupId: 群ID                      │
│  GroupName: 新群名 (UTF-8)          │
└─────────────────────────────────────┘
```

---

## 四、权限

```
可修改者（与 C++ 一致）:
- 群主（role=2）
- 管理员（role=1）

限制（native 当前实现）:
- 名称: 非空，UTF-8 字节数 ≤2048（与 CreateGroup / SqliteMetadataDb 一致）
- 敏感词 / 修改频率: 未在 MM1 强制；可由上层或未来 MM1 策略补充（JNI 仍传入 nowMs 供扩展）
```

---

## 五、存储（当前 C++）

| 项 | 实现 |
|----|------|
| 位置 | 元数据 SQLite 表 **`mm2_group_display`**（**非** `.zdb` 消息块；与 **`group_data`→ZGK1** 独立） |
| 字段 | **`group_id`**（16B）、**`name`**（UTF-8）、**`updated_s`**（Unix **秒**）、**`updated_by`**（16B） |

**与 `group_data`**：**`03-Storage.md` 第2.4节** 的 **`group_data`** 仅索引群密钥等大块；**群显示名**不写入该表。

---

## 六、相关文档

| 文档 | 用途 |
|------|------|
| [02-ZSP-Protocol.md](../01-Architecture/02-ZSP-Protocol.md) | **0x1C**、TLV **0x20** |
| [01-JNI.md](../06-Appendix/01-JNI.md) | `updateGroupName` / `getGroupName` |
| [Implementation-Status.md](../../ZChatIM/docs/Implementation-Status.md) | MM2 / SQLite 实现状态 |
| [03-Storage.md](../02-Core/03-Storage.md) | **`mm2_group_display`**、**`group_data`** |
