# 群组名称修改技术规范

> **协议修正（重要）**：群名称修改的 ZSP **消息类型**为 **`0x1C` `GROUP_NAME_UPDATE`**（**`02-ZSP-Protocol.md` 第五节**），**不是** **`0x10`**（**0x10** 为 **`GROUP_UPDATE`** 其它群资料更新）。  
> **可选 TLV**：载荷内可使用 **TLV Type `0x20` `GroupName`**（**`02-ZSP-Protocol.md` 第7.2节**）携带 **`groupId` / UTF-8 群名** 等；具体打包顺序以 **协议实现** 为准。  
> **JNI**：**`updateGroupName`** → MM1 **`GroupNameManager`** 校验后 → **`MM2::UpdateGroupName`**（**`01-JNI.md`** 路由摘要）。  
> **MM2**：**`UpdateGroupName` / `GetGroupName` 已落 SQLite 表 `mm2_group_display`**（与 **`group_data` 大块**独立，见 **`03-Storage.md` 第2.6节**、**`05` 第2.1节**）。**JNI / `GroupNameManager`** 仍待接通。

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
可修改者:
- 群主
- 管理员

限制:
- 名称长度: 1-50 字符
- 禁止敏感词
- 修改频率: 1次/分钟
```

---

## 五、存储（目标 vs 当前）

| 项 | 目标 | 当前 C++ |
|----|------|----------|
| 位置 | **`.zdb` + 索引** 或 MM1 扩展表 | **`MM2::UpdateGroupName` 未实现** |
| 字段 | `groupId`、`groupName`、`updateTime`、`updateBy` | 以未来 schema 为准 |

**与 `group_data` 表**：**`03-Storage.md` 第2.4节** 已有 **`group_data`** 索引表；群名是否映射到该表由实现定稿后更新 **第七节**。

---

## 六、相关文档

| 文档 | 用途 |
|------|------|
| [02-ZSP-Protocol.md](../01-Architecture/02-ZSP-Protocol.md) | **0x1C**、TLV **0x20** |
| [01-JNI.md](../06-Appendix/01-JNI.md) | `updateGroupName` / `getGroupName` |
| [05-ZChatIM-Implementation-Status.md](../02-Core/05-ZChatIM-Implementation-Status.md) | MM2 / SQLite 实现状态 |
| [03-Storage.md](../02-Core/03-Storage.md) | `group_data` |
