# Gradle 离线包（本机构建用）

## 用哪种 zip？

| 文件 | 说明 |
|------|------|
| `gradle-8.14.4-bin.zip` | 体积较小，只含运行 Gradle 所需内容 |
| `gradle-8.14.4-all.zip` | 含文档与更多资源，体积更大；**可与 Wrapper 配合使用** |
| `gradle-8.14.4-src.zip` | **仅源码**，不能当作发行包给 Wrapper 用；IDE 有时单独要它做脚本跳转 |

`Client/Android/gradle/wrapper/gradle-wrapper.properties` 里的 `distributionUrl` 必须指向 **`bin` 或 `all` 的 zip**（二选一），且路径、文件名与磁盘上**完全一致**。

官方目录：  
https://services.gradle.org/distributions/

---

## 方式一：只放 zip，让 Gradle 自己解压（推荐）

1. 把 `gradle-8.14.4-all.zip`（或 `bin`）放到本目录。  
2. 确认 `gradle-wrapper.properties` 里 `distributionUrl` 的**文件名**与你放的文件相同。  
3. 在 `Client/Android` 下执行 `gradlew.bat`，首次会把 zip 解压到用户目录下的  
   `%USERPROFILE%\.gradle\wrapper\dists\...`  
   **不必**自己解压到 Android Studio 里。

---

## 方式二：手动解压，再在 Android Studio 里指定「本地 Gradle」

适合你想固定使用某一目录里的 Gradle，而不走 Wrapper 里的 zip。

1. 把 `gradle-8.14.4-all.zip` 解压到任意目录，例如：  
   `D:\Dev\gradle\gradle-8.14.4`  
2. 确认该目录下有 `bin\gradle.bat`（Windows）。  
3. Android Studio：**File → Settings**（macOS：**Android Studio → Settings**）  
   → **Build, Execution, Deployment → Build Tools → Gradle**  
4. **Gradle distribution** 选择 **Local installation** / **Specified location**（不同版本文案略有差异）。  
5. 路径选到解压后的**根目录**（包含 `bin` 的那一层），例如：  
   `D:\Dev\gradle\gradle-8.14.4`  
6. **Apply → OK**，再 **Sync Project with Gradle Files**。

说明：若项目仍使用 **Gradle Wrapper**（`gradlew`），多数情况下仍会按 `gradle-wrapper.properties` 去解析发行包；指定「本地 Gradle」主要影响 IDE 使用的 Gradle 主目录，以你当前 Android Studio 版本界面为准。

---

## 路径与克隆位置

若仓库不在 `D:\Project\Algorithm\ZerOS-Chat`，请修改  
`Client/Android/gradle/wrapper/gradle-wrapper.properties` 中 `distributionUrl` 的 `file:///` 路径。

---

## 关于 IDE 仍下载 `gradle-*-src.zip`

多为 Kotlin DSL 脚本索引；**不影响** `./gradlew :app:assembleDebug` 是否成功。使用 **all** 发行包有时能减轻相关提示；若仍报错，可在 Gradle 设置里关闭「下载源码」类选项，或暂时忽略该日志。
