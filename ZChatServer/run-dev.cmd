@echo off
REM JDK 24+：与 pom.xml profile jdk24-jvm-warnings 一致（JNI System::load + Netty 堆外内存）
cd /d "%~dp0"
mvn -q -DskipTests spring-boot:run
