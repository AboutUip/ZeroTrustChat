# JNI 接口清单

## 一、认证模块

| 接口 | 输入 | 输出 | 说明 |
|------|------|------|------|
| auth | userId, token | sessionId/null | 用户认证 |
| verifySession | sessionId | active/invalid | 验证会话 |
| destroySession | sessionId | true/false | 销毁会话 |

## 二、消息模块

| 接口 | 输入 | 输出 | 说明 |
|------|------|------|------|
| storeMessage | sessionId, data | msgId/null | 存储消息 |
| retrieveMessage | msgId | data/null | 获取消息 |
| deleteMessage | msgId | true/false | 删除消息 |
| listMessages | userId, count | array | 获取消息列表 |

## 三、用户数据模块

| 接口 | 输入 | 输出 | 说明 |
|------|------|------|------|
| storeUserData | userId, type, data | result | 存储用户数据 |
| getUserData | userId, type | data/null | 获取用户数据 |
| deleteUserData | userId, type | result | 删除用户数据 |

## 四、好友模块

| 接口 | 输入 | 输出 | 说明 |
|------|------|------|------|
| sendFriendRequest | fromUserId, toUserId | requestId/null | 发送好友请求 |
| respondFriendRequest | requestId, accept | result | 响应好友请求 |
| deleteFriend | userId, friendId | result | 删除好友 |
| getFriends | userId | array | 获取好友列表 |

## 五、群组模块

| 接口 | 输入 | 输出 | 说明 |
|------|------|------|------|
| createGroup | creatorId, name | groupId/null | 创建群组 |
| inviteMember | groupId, userId | result | 邀请成员 |
| removeMember | groupId, userId | result | 移除成员 |
| leaveGroup | groupId, userId | result | 退出群组 |
| getGroupMembers | groupId | array | 获取成员列表 |
| updateGroupKey | groupId | result | 更新群密钥 |

## 六、文件模块

| 接口 | 输入 | 输出 | 说明 |
|------|------|------|------|
| storeFileChunk | fileId, chunkIndex, data | result | 存储文件分片 |
| getFileChunk | fileId, chunkIndex | data/null | 获取文件分片 |
| completeFile | fileId, sha256 | result | 完成文件传输 |
| cancelFile | fileId | result | 取消传输 |

## 七、安全模块

| 接口 | 输入 | 输出 | 说明 |
|------|------|------|------|
| emergencyWipe | - | - | 紧急全量销毁 |
| getStatus | - | status | 获取系统状态 |
| rotateKeys | - | result | 密钥轮换 |

## 八、错误码

| 错误码 | 说明 |
|--------|------|
| E_SUCCESS | 成功 |
| E_AUTH_FAILED | 认证失败 |
| E_AUTH_LOCKED | 账户已锁定 |
| E_AUTH_RATE_LIMIT | 频率超限 |
| E_INVALID_SESSION | 无效会话 |
| E_SESSION_EXPIRED | 会话过期 |
| E_NOT_FOUND | 资源不存在 |
| E_PERMISSION_DENIED | 权限拒绝 |
| E_INVALID_DATA | 数据无效 |
| E_STORAGE_FAILED | 存储失败 |
| E_ENCRYPT_FAILED | 加密失败 |
| E_DECRYPT_FAILED | 解密失败 |
